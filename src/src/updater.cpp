// KrakenAV - updater.cpp
// 更新窗口：运行 freshclam.exe 子进程，实时捕获输出显示
#include "updater.h"
#include "engine.h"
#include <shlobj.h>

static HWND g_updWnd = nullptr;
static HWND g_updLog = nullptr;
static HWND g_updMain = nullptr;      // 主窗口
static HANDLE g_proc = nullptr;       // freshclam 进程
static std::atomic<bool> g_updRunning{false};
static std::atomic<bool> g_updKill{false};

enum { UM_LINE = WM_APP + 10, UM_REPLACE = WM_APP + 11, UM_DONE = WM_APP + 12 };
// UM_LINE/UM_REPLACE: lParam = new wchar_t[](调用方 delete[])
// UM_DONE: wParam = exit code

// 日志行缓冲（自维护，全量重绘 EDIT，规避 EDIT 消息追加的乱序边缘行为）
// 进度行（replaceLast）高频到达：节流到 150ms 重绘一次，普通新行立即重绘
static std::vector<std::wstring> g_updLines;
static const size_t UPD_MAX_LINES = 400;
static DWORD g_lastFlush = 0;
static bool g_flushPending = false;

static void FlushLines(HWND edit) {
    std::wstring all;
    for (auto& l : g_updLines) all += l + L"\r\n";
    SetWindowTextW(edit, all.c_str());
    SendMessageW(edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1); // 光标置底
    SendMessageW(edit, EM_SCROLL, SB_BOTTOM, 0);
    g_lastFlush = GetTickCount();
    g_flushPending = false;
}

static void AppendEditLine(HWND edit, const std::wstring& line, bool replaceLast) {
    if (replaceLast && !g_updLines.empty()) g_updLines.back() = line;
    else {
        g_updLines.push_back(line);
        if (g_updLines.size() > UPD_MAX_LINES) g_updLines.erase(g_updLines.begin());
    }
    if (replaceLast) {
        DWORD now = GetTickCount();
        if (now - g_lastFlush >= 150) {
            FlushLines(edit);
        } else if (!g_flushPending && g_updWnd) {
            g_flushPending = true;
            SetTimer(g_updWnd, 2, 160, nullptr);
        }
    } else {
        FlushLines(edit);
    }
}

static void PostLine(const std::wstring& s, bool replace) {
    wchar_t* b = new wchar_t[s.size() + 1];
    wcscpy(b, s.c_str());
    PostMessageW(g_updWnd, replace ? UM_REPLACE : UM_LINE, 0, (LPARAM)b);
}

// 配置文件目录：引擎目录为纯 ASCII 直接用；含中文（盘符映射不可用的兜底场景）
// 时改放到 ProgramData（系统目录路径恒为 ASCII），避免 freshclam 打不开中文配置
static std::wstring ConfDirForFreshclam() {
    if (!PathHasNonAscii(g_engineDir)) return g_engineDir + L"\\conf";
    wchar_t pd[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, pd)))
        return std::wstring(pd) + L"\\KrakenAV\\conf";
    return g_engineDir + L"\\conf";
}

static bool WriteFreshclamConf(std::wstring& confPath) {
    std::wstring confDir = ConfDirForFreshclam();
    EnsureDir(confDir);
    EnsureDir(EffectiveDbDir());
    confPath = confDir + L"\\freshclam.conf";
    FILE* f = _wfopen(confPath.c_str(), L"wb");
    if (!f) return false;
    auto esc = [](std::wstring p) { if (!p.empty() && p.back() == L'\\') p.pop_back(); return p; };
    std::string body =
        "DatabaseDirectory \"" + WToUtf8(esc(EffectiveDbDir())) + "\"\r\n"
        "DatabaseMirror database.clamav.net\r\n"
        "ReceiveTimeout 60\r\n"
        "ConnectTimeout 30\r\n"
        "LogSyslog no\r\n"
        "LogRotate no\r\n";
    fwrite(body.data(), 1, body.size(), f);
    fclose(f);
    return true;
}

static DWORD WINAPI FreshclamThread(LPVOID) {
    std::wstring conf;
    if (!WriteFreshclamConf(conf)) {
        PostLine(L"[x] Cannot write freshclam.conf", false);
        g_updRunning = false;
        PostMessageW(g_updWnd, UM_DONE, 1, 0);
        return 0;
    }
    std::wstring exe = g_engineDir + L"\\freshclam.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        PostLine(L"[x] freshclam.exe not found: " + exe, false);
        g_updRunning = false;
        PostMessageW(g_updWnd, UM_DONE, 1, 0);
        return 0;
    }

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        g_updRunning = false; PostMessageW(g_updWnd, UM_DONE, 1, 0); return 0;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = L"\"" + exe + L"\" --config-file=\"" + conf + L"\"";
    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);
    if (!CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, g_engineDir.c_str(), &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr);
        PostLine(L"[x] CreateProcess failed", false);
        g_updRunning = false; PostMessageW(g_updWnd, UM_DONE, 1, 0); return 0;
    }
    CloseHandle(wr);
    CloseHandle(pi.hThread);
    g_proc = pi.hProcess;

    // 读输出：'\r' 单独结尾 = 进度刷新（替换最后一行）；'\n' 或 "\r\n" = 正常换行
    std::string pending;
    char buf[4096];
    DWORD n = 0;
    auto emit = [&](std::string seg, bool replace) {
        if (seg.empty()) return;
        std::wstring w = Utf8ToW(seg);
        while (!w.empty() && (w.back() == L' ' || w.back() == L'\t')) w.pop_back();
        if (w.empty()) return;
        PostLine(w, replace);
    };
    while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0) {
        if (g_updKill) break;
        pending.append(buf, n);
        size_t pos = 0;
        while (pos < pending.size()) {
            size_t r = pending.find('\r', pos);
            size_t l = pending.find('\n', pos);
            if (r == std::string::npos && l == std::string::npos) break;
            if (l == std::string::npos || (r != std::string::npos && r < l)) {
                // '\r'：若紧跟 '\n' 则为 "\r\n" 行结束
                if (r + 1 < pending.size() && pending[r + 1] == '\n') {
                    emit(pending.substr(pos, r - pos), false);
                    pos = r + 2;
                } else {
                    emit(pending.substr(pos, r - pos), true); // 进度刷新
                    pos = r + 1;
                }
            } else {
                emit(pending.substr(pos, l - pos), false);
                pos = l + 1;
            }
        }
        pending.erase(0, pos);
    }
    if (!pending.empty()) emit(pending, false); // EOF 残留
    CloseHandle(rd);

    DWORD code = 1;
    if (g_updKill && g_proc) {
        TerminateProcess(g_proc, 1);
    }
    if (g_proc) { WaitForSingleObject(g_proc, 5000); GetExitCodeProcess(g_proc, &code); CloseHandle(g_proc); g_proc = nullptr; }
    g_updRunning = false;
    PostMessageW(g_updWnd, UM_DONE, (WPARAM)code, 0);
    return 0;
}

static LRESULT CALLBACK UpdProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_updLog = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            SX(16), SX(52), SX(560 - 32), SX(400 - 52 - 16 - 36), h, (HMENU)1, nullptr, nullptr);
        HFONT f = CreateFontW(-SX(14), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_MODERN,
                              L"Consolas");
        SendMessageW(g_updLog, WM_SETFONT, (WPARAM)f, TRUE);
        // 初始行进入行缓冲（后续全量重绘，顺序稳定）
        g_updLines.clear();
        g_updLines.push_back(std::wstring(L"[i] ") + STR(STR_UPD_RUNNING));
        if (PathHasNonAscii(g_engineDir)) g_updLines.push_back(STR(STR_LOG_PATH_HINT));
        std::wstring all;
        for (auto& l : g_updLines) all += l + L"\r\n";
        SetWindowTextW(g_updLog, all.c_str());
        CreateThread(nullptr, 0, FreshclamThread, nullptr, 0, nullptr);
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetBkColor(dc, RGB(0x10, 0x16, 0x1F));
        SetTextColor(dc, RGB(0xC8, 0xD2, 0xE0));
        static HBRUSH br = CreateSolidBrush(RGB(0x10, 0x16, 0x1F));
        return (LRESULT)br;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        static HBRUSH bg = CreateSolidBrush(RGB(0x1B, 0x24, 0x31));
        RECT top{ 0, 0, rc.right, SX(48) };
        FillRect(dc, &top, bg);
        SetBkColor(dc, RGB(0x1B, 0x24, 0x31));
        SetTextColor(dc, RGB(0xE8, 0xED, 0xF4));
        HFONT ft = CreateFontW(-SX(16), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                               0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        HFONT old = (HFONT)SelectObject(dc, ft);
        TextOutW(dc, SX(16), SX(14), STR(STR_UPD_TITLE), (int)wcslen(STR(STR_UPD_TITLE)));
        SelectObject(dc, old); DeleteObject(ft);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER: {
        if (w == 2) {                      // 进度行节流刷新
            KillTimer(h, 2);
            if (g_flushPending) FlushLines(g_updLog);
        }
        return 0;
    }
    case UM_LINE:
    case UM_REPLACE: {
        wchar_t* s = (wchar_t*)l;
        AppendEditLine(g_updLog, s ? s : L"", m == UM_REPLACE);
        delete[] s;
        return 0;
    }
    case UM_DONE: {
        bool ok = (w == 0);
        // 醒目的结束状态：标题更新 + 结束行（用户能明确看出流程已结束）
        AppendEditLine(g_updLog, L"", false);
        AppendEditLine(g_updLog,
            ok ? L"========================================"
               : L"----------------------------------------", false);
        AppendEditLine(g_updLog,
            ok ? std::wstring(L"[√] ") + STR(STR_UPD_DONE_OK)
               : std::wstring(L"[x] ") + STR(STR_UPD_DONE_FAIL), false);
        AppendEditLine(g_updLog,
            ok ? L"========================================"
               : L"----------------------------------------", false);
        SetWindowTextW(h, ok ? STR(STR_UPD_DONE_OK) : STR(STR_UPD_DONE_FAIL));
        if (ok && g_updMain) {
            EngineMarkDirty();                    // 病毒库已变，下次扫描重建引擎
            PostMessageW(g_updMain, WM_APP + 4, 0, 0); // 主窗口刷新底部病毒库信息
        }
        return 0;
    }
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        if (r == HTCLIENT && GET_Y_LPARAM(l) < SX(48) && GET_X_LPARAM(l) < SX(560 - 48)) return HTCAPTION;
        return r;
    }
    case WM_CLOSE:
    case WM_DESTROY: {
        g_updKill = true;
        if (g_proc) TerminateProcess(g_proc, 1);
        g_updWnd = nullptr; g_updLog = nullptr;
        if (m == WM_CLOSE) { DestroyWindow(h); }
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

void ShowUpdateWindow(HWND mainWnd) {
    g_updMain = mainWnd;
    if (g_updWnd) { ShowWindow(g_updWnd, SW_SHOWNORMAL); SetForegroundWindow(g_updWnd); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = UpdProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"KrakenUpdWnd";
        RegisterClassW(&wc);
        reg = true;
    }
    g_updKill = false;
    g_updWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"KrakenUpdWnd", L"KrakenAV Update",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, SX(560), SX(400),
        mainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowTextW(g_updWnd, STR(STR_UPD_TITLE));   // 标题跟随界面语言
    ShowWindow(g_updWnd, SW_SHOW);
}

bool UpdateRunning() { return g_updRunning; }
void UpdateForceClose() {
    if (g_updWnd) { g_updKill = true; if (g_proc) TerminateProcess(g_proc, 1); }
}
