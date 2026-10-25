// KrakenAV - engine.cpp
#include "engine.h"
#include <fstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <shellapi.h>
#include <shlobj.h>

ScanState g_scan;
std::mutex g_logMtx;
std::deque<std::wstring> g_logPending;

void PushLog(const std::wstring& line) {
    std::lock_guard<std::mutex> lk(g_logMtx);
    g_logPending.push_back(line);
    if (g_logPending.size() > 400) g_logPending.pop_front();
}

// UI 定时器：把待显示日志批量追加到 EDIT，并裁剪行数
void DrainLogToEdit(HWND edit) {
    std::deque<std::wstring> take;
    {
        std::lock_guard<std::mutex> lk(g_logMtx);
        take.swap(g_logPending);
    }
    if (take.empty()) return;
    // 先记录视图状态：用户上翻查看时不被新日志强拉到底
    SCROLLINFO si{ sizeof(si), SIF_POS | SIF_RANGE | SIF_PAGE };
    GetScrollInfo(edit, SB_VERT, &si);
    bool atBottom = si.nPos >= (int)(si.nMax - si.nPage - 2);
    LONGLONG firstVisible = SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);

    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    for (auto& l : take) {
        SendMessageW(edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        std::wstring nl = l + L"\r\n";
        SendMessageW(edit, EM_REPLACESEL, FALSE, (LPARAM)nl.c_str());
    }
    // 裁剪：最多保留 1500 行（记录从顶部移除的行数用于滚动补偿）
    LONGLONG removed = 0;
    LONGLONG lines = SendMessageW(edit, EM_GETLINECOUNT, 0, 0);
    if (lines > 1500) {
        LONGLONG keep = lines - 1000;
        LONGLONG idx = SendMessageW(edit, EM_LINEINDEX, (WPARAM)keep, 0);
        if (idx > 0) {
            removed = keep;
            SendMessageW(edit, EM_SETSEL, 0, idx);
            SendMessageW(edit, EM_REPLACESEL, FALSE, (LPARAM)L"");
        }
    }
    if (atBottom) {
        SendMessageW(edit, EM_SCROLL, SB_BOTTOM, 0);
    } else {
        // 恢复原视图位置（内容因追加/裁剪平移了 removed 行）
        LONGLONG nowFirst = SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
        LONGLONG delta = (firstVisible + removed) - nowFirst;
        if (delta != 0) SendMessageW(edit, EM_LINESCROLL, 0, (LPARAM)delta);
    }
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(edit, nullptr, FALSE);
}

// ---------------- 引擎 ----------------
static struct cl_engine* g_engine = nullptr;
static unsigned int g_sigCount = 0;
static bool g_libInited = false;
static bool g_engineDirty = true;
static std::mutex g_engineMtx;

// 病毒库加载进度回调
static cl_error_t SigloadProgress(unsigned long long completed, unsigned long long total, void*) {
    if (total > 0) g_scan.engineProgress = (int)(completed * 100 / total);
    return CL_SUCCESS;
}

bool EngineInitLibrary() {
    if (g_libInited) return true;
    int r = cl_init(CL_INIT_DEFAULT);
    g_libInited = (r == CL_SUCCESS);
    return g_libInited;
}

// ClamAV 1.5.4 的 cl_scanfile/cl_load 用 UTF-8→ANSI 转换打开文件，
// 中文路径会静默失败（文件打不开→当作干净），因此：
//  - 扫描：CreateFileW 拿句柄 → fmap + 自写 pread 回调（Unicode 安全）
//  - 毒库：目录含中文时重定向到 ProgramData 下的纯 ASCII 目录
//    （正常情况下程序已通过盘符映射以 ASCII 路径运行，此处仅兜底）
std::wstring EffectiveDbDir() {
    if (!PathHasNonAscii(g_dbDir)) return g_dbDir;
    wchar_t pd[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, pd))) {
        std::wstring d = std::wstring(pd) + L"\\KrakenAV\\db";
        EnsureDir(d);
        return d;
    }
    return g_dbDir;
}

// pread 回调：ReadFile + OVERLAPPED 偏移（无 CRT fd，MinGW/MSVC 句柄通用）
static off_t PreadCB(void* handle, void* buf, size_t count, off_t offset) {
    OVERLAPPED ov{};
    unsigned long long off = (unsigned long long)offset;
    ov.Offset = (DWORD)(off & 0xFFFFFFFFULL);
    ov.OffsetHigh = (DWORD)(off >> 32);
    DWORD rd = 0;
    if (!ReadFile((HANDLE)handle, buf, (DWORD)count, &rd, &ov)) return -1;
    return (off_t)rd;
}

cl_error_t ScanFileUnicode(const std::wstring& path, const char** virname,
                           unsigned long* scanned, struct cl_engine* eng,
                           struct cl_scan_options* so) {
    HANDLE h = CreateFileW(ToLongPath(path).c_str(), FILE_READ_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return CL_EOPEN;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return CL_ESTAT; }
    // 类型识别提示：只用扩展名（ASCII 校验）。
    // 不依赖完整路径/8.3 短路径——后者在禁用 8.3 的卷上不可用，会导致
    // 中文路径下 HTML/JS/VBS 等无 magic 的类型识别退化。
    std::string hint = "file";
    size_t dot = path.find_last_of(L".\\/");
    if (dot != std::wstring::npos && path[dot] == L'.') {
        std::wstring ext = ToLowerW(path.substr(dot));
        if (ext.size() <= 10 && !PathHasNonAscii(ext)) hint = "scan" + WToUtf8(ext);
    }
    cl_fmap_t* map = cl_fmap_open_handle((void*)h, 0, (size_t)sz.QuadPart, PreadCB, 0);
    if (!map) { CloseHandle(h); return CL_EMEM; }
    cl_error_t rc = cl_scanmap_callback(map, hint.c_str(), virname, scanned, eng, so, nullptr);
    cl_fmap_close(map);
    CloseHandle(h);
    return rc;
}

void EngineMarkDirty() { g_engineDirty = true; }

struct cl_engine* EngineRaw() { return g_engine; }

bool EngineEnsure(HWND /*notifyWnd*/) {
    std::lock_guard<std::mutex> lk(g_engineMtx);
    if (g_engine && !g_engineDirty) return true;
    if (g_engine) {
        cl_engine_free(g_engine);
        g_engine = nullptr;
    }
    if (!EngineInitLibrary()) return false;

    // 没有病毒库则不建引擎
    DbInfo db = GetDbInfo();
    if (!db.ok) return false;

    g_engine = cl_engine_new();
    if (!g_engine) return false;

    // 按设置设定扫描限制（0 = 不限制）
    if (g_set.skipBigFiles)  cl_engine_set_num(g_engine, CL_ENGINE_MAX_FILESIZE,  (long long)g_set.maxFileMB * 1024 * 1024);
    else                     cl_engine_set_num(g_engine, CL_ENGINE_MAX_FILESIZE,  0);
    if (g_set.skipBigArch)   cl_engine_set_num(g_engine, CL_ENGINE_MAX_SCANSIZE,  (long long)g_set.maxArchMB * 1024 * 1024);
    else                     cl_engine_set_num(g_engine, CL_ENGINE_MAX_SCANSIZE,  0);
    if (g_set.skipManyArch)  cl_engine_set_num(g_engine, CL_ENGINE_MAX_FILES,     g_set.maxArchFiles);
    else                     cl_engine_set_num(g_engine, CL_ENGINE_MAX_FILES,     0);
    if (g_set.skipDeepArch)  cl_engine_set_num(g_engine, CL_ENGINE_MAX_RECURSION, g_set.maxArchDepth);
    else                     cl_engine_set_num(g_engine, CL_ENGINE_MAX_RECURSION, 0);

    cl_engine_set_clcb_sigload_progress(g_engine, SigloadProgress, nullptr);

    std::string dbDir8 = WToUtf8(EffectiveDbDir());
    unsigned int sigs = 0;
    cl_error_t r = cl_load(dbDir8.c_str(), g_engine, &sigs, CL_DB_STDOPT);
    if ((r != CL_SUCCESS || sigs == 0) && !g_scan.cancel) {
        // 瞬时竞争防御：病毒库刚被 freshclam 写入/杀软实时扫描占用时重试一次
        Sleep(1500);
        r = cl_load(dbDir8.c_str(), g_engine, &sigs, CL_DB_STDOPT);
    }
    if (r != CL_SUCCESS || sigs == 0) {
        PushLog(std::wstring(L"[!] cl_load failed: ") + Utf8ToW(cl_strerror(r)));
        // 病毒库文件可能被本机其他杀毒软件实时隔离
        namespace fs = std::filesystem;
        std::error_code ec;
        int n = 0;
        for (auto& e : fs::directory_iterator(EffectiveDbDir(), ec))
            if (!ec) n++;
        if (n < 3) {
            PushLog(std::wstring(L"[!] ") + STR(STR_LOG_KAV_HINT));
        }
        cl_engine_free(g_engine); g_engine = nullptr;
        return false;
    }
    g_sigCount = sigs;
    r = cl_engine_compile(g_engine);
    if (r != CL_SUCCESS) {
        cl_engine_free(g_engine); g_engine = nullptr;
        return false;
    }
    g_engineDirty = false;
    g_scan.engineProgress = -1;
    return true;
}

void EngineShutdown() {
    std::lock_guard<std::mutex> lk(g_engineMtx);
    if (g_engine) { cl_engine_free(g_engine); g_engine = nullptr; }
}

// ---------------- 病毒库信息 ----------------
static const wchar_t* MONTHS[] = {L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",
                                  L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};

DbInfo GetDbInfo() {
    DbInfo info;
    namespace fs = std::filesystem;
    std::error_code ec;
    FILETIME best{}; bool has = false;
    for (auto& e : fs::directory_iterator(EffectiveDbDir(), ec)) {
        auto ext = ToLowerW(e.path().extension().wstring());
        if (ext != L".cvd" && ext != L".cld") continue;
        HANDLE hf = CreateFileW(e.path().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) continue;
        char head[512] = {};
        DWORD rd = 0;
        ReadFile(hf, head, 511, &rd, nullptr);
        CloseHandle(hf);
        if (rd < 32 || strncmp(head, "ClamAV-VDB:", 11) != 0) continue;
        // 格式: ClamAV-VDB:09 Oct 2026 06-25 +0000:版本号:特征数:flevel:md5:builder:ts
        std::string h = head;
        auto f1 = h.find(':', 11);           // 日期字段结束
        if (f1 == std::string::npos) continue;
        std::string dateField = h.substr(11, f1 - 11);   // "09 Oct 2026 06-25 +0000"
        auto f2 = h.find(':', f1 + 1);
        auto f3 = f2 == std::string::npos ? std::string::npos : h.find(':', f2 + 1);
        std::string sigField = h.substr(f2 + 1, f3 == std::string::npos ? std::string::npos : f3 - f2 - 1);
        info.sigs += _strtoui64(sigField.c_str(), nullptr, 10);
        int day = 0, year = 0; char mon[8] = {};
        sscanf(dateField.c_str(), "%d %7s %d", &day, mon, &year);
        int mi = 0;
        for (int i = 0; i < 12; i++) if (_stricmp(mon, WToUtf8(MONTHS[i]).c_str()) == 0) { mi = i + 1; break; }
        if (day > 0 && mi > 0 && year > 0) {
            FILETIME cand{}; SYSTEMTIME st{ (WORD)year, (WORD)mi, 0, (WORD)day };
            SystemTimeToFileTime(&st, &cand);
            if (!has || CompareFileTime(&cand, &best) > 0) {
                best = cand; has = true;
                wchar_t b[24]; swprintf(b, 24, L"%04d-%02d-%02d", year, mi, day);
                info.dateStr = b;
            }
        }
    }
    info.ok = has;
    if (!has && info.sigs > 0) { info.ok = true; info.dateStr = L"?"; }
    return info;
}

// ---------------- 快速扫描目标 ----------------
static void AddKnown(wchar_t* path, std::vector<std::wstring>& v) {
    if (path && *path) { std::wstring p = path; CoTaskMemFree(path); if (!p.empty()) v.push_back(p); }
}

std::vector<std::wstring> QuickScanRoots() {
    std::vector<std::wstring> v;
    PWSTR p = nullptr;
    // 下载 / 桌面 / 文档 / Roaming / 临时
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p))) AddKnown(p, v);
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p))) AddKnown(p, v);
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p))) AddKnown(p, v);
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) AddKnown(p, v);
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) AddKnown(p, v);
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &p))) AddKnown(p, v);
    // 用户与公共启动项
    wchar_t startup[MAX_PATH + 40];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, startup))) {
        v.push_back(startup);
    }
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_STARTUP, nullptr, 0, startup))) {
        v.push_back(startup);
    }
    // 去掉重复与不存在的
    std::vector<std::wstring> out;
    for (auto& s : v) {
        DWORD at = GetFileAttributesW(s.c_str());
        if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) continue;
        bool dup = false;
        for (auto& o : out) if (!_wcsicmp(o.c_str(), s.c_str())) { dup = true; break; }
        if (!dup) out.push_back(s);
    }
    return out;
}

// ---------------- 威胁处理 ----------------
// 隔离区留档（含 XOR 混淆），写 manifest；返回生成的隔离文件路径，失败返回空
static std::wstring QuarantineCopy(const std::wstring& path, const std::wstring& tag,
                                   const std::wstring& vn) {
    EnsureDir(g_quarDir);
    std::wstring base = path.substr(path.find_last_of(L"\\/") + 1);
    wchar_t rnd[16]; swprintf(rnd, 16, L"%04X", (unsigned)(GetTickCount() & 0xFFFF));
    std::wstring dst = g_quarDir + L"\\" + GetTimeStamp() + L"_" + tag + L"_" + rnd +
                       L"_" + base + L".quarantine";
    if (!CopyFileW(path.c_str(), dst.c_str(), TRUE)) return L"";
    XorFileInPlace(dst);   // 混淆：即使改回原名也无法直接运行（恢复用 quarantine_restore.exe）
    std::ofstream mf(std::filesystem::path(g_quarDir + L"\\manifest.txt"),
                     std::ios::app | std::ios::binary);
    if (mf.tellp() == 0) {
        std::string head = WToUtf8(std::wstring(STR(STR_QUAR_MANIFEST_HEAD)) + L"\r\n");
        mf.write(head.data(), head.size());
    }
    std::string line = WToUtf8(L"[" + NowStr() + L"] " + path + L" -> " + dst + L" ; " + vn + L"\r\n");
    mf.write(line.data(), line.size());
    return dst;
}

std::wstring ApplyThreat(const std::wstring& path, const char* virname) {
    std::wstring vn = virname ? Utf8ToW(virname) : L"Unknown";
    if (g_set.infectedAction == 1) {
        // 删除前先在隔离区留一份副本（防误报毁档；副本同样 XOR 混淆）
        QuarantineCopy(path, L"del", vn);
        if (DeleteFileW(path.c_str())) return STR(STR_ACT_DELETED);
        return STR(STR_ACT_DELETED_FAIL);
    }
    if (g_set.infectedAction == 2) {
        // 优先移动（同卷零拷贝）；跨卷失败则复制+混淆+删源
        EnsureDir(g_quarDir);
        std::wstring base = path.substr(path.find_last_of(L"\\/") + 1);
        wchar_t rnd[16]; swprintf(rnd, 16, L"%04X", (unsigned)(GetTickCount() & 0xFFFF));
        std::wstring dst = g_quarDir + L"\\" + GetTimeStamp() + L"_q_" + rnd + L"_" + base + L".quarantine";
        if (MoveFileExW(path.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            XorFileInPlace(dst);
            std::ofstream mf(std::filesystem::path(g_quarDir + L"\\manifest.txt"),
                             std::ios::app | std::ios::binary);
            if (mf.tellp() == 0) {
                std::string head = WToUtf8(std::wstring(STR(STR_QUAR_MANIFEST_HEAD)) + L"\r\n");
                mf.write(head.data(), head.size());
            }
            std::string line = WToUtf8(L"[" + NowStr() + L"] " + path + L" -> " + dst + L" ; " + vn + L"\r\n");
            mf.write(line.data(), line.size());
            return STR(STR_ACT_QUAR_OK);
        }
        // 跨卷/移动失败：复制留档后删除
        if (!QuarantineCopy(path, L"q", vn).empty() && DeleteFileW(path.c_str()))
            return STR(STR_ACT_QUAR_OK);
        return STR(STR_ACT_QUAR_FAIL);
    }
    return STR(STR_ACT_REPORTED);
}

// ---------------- 报告 ----------------
void WriteReport(ScanMode mode, unsigned long long files, unsigned long long threats,
                 unsigned long long errors, long long ms, const std::vector<std::wstring>& threatLines) {
    EnsureDir(g_reportDir);
    std::wstring path = g_reportDir + L"\\scan_" + GetTimeStamp() + L".txt";
    std::ofstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) return;
    f.write("\xEF\xBB\xBF", 3); // UTF-8 BOM
    auto wl = [&](const std::wstring& s) { std::string u = WToUtf8(s + L"\r\n"); f.write(u.data(), u.size()); };
    const wchar_t* modes[3] = {L"Quick", L"Full", L"Custom"};
    DbInfo db = GetDbInfo();
    wl(std::wstring(L"==== ") + STR(STR_REPORT_TITLE) + L" ====");
    wl(L"");
    wl(std::wstring(STR(STR_REPORT_DATETIME)) + L": " + NowStr());
    wl(std::wstring(STR(STR_REPORT_MODE)) + L": " + modes[mode]);
    wl(std::wstring(STR(STR_REPORT_ENGINE)) + L": ClamAV " + Utf8ToW(cl_retver()));
    if (db.ok) wl(std::wstring(STR(STR_REPORT_DB)) + L": " + db.dateStr + L" (" + GroupDigits(db.sigs) + L" " + STR(STR_STATUS_SIGS) + L")");
    wl(L"");
    if (threatLines.empty()) {
        wl(STR(STR_LOG_FOUND_CLEAN_NOTE));
    } else {
        for (auto& t : threatLines) wl(t);
    }
    wl(L"");
    wl(std::wstring(STR(STR_REPORT_FILES)) + L": " + GroupDigits(files));
    wl(std::wstring(STR(STR_REPORT_THREATS)) + L": " + GroupDigits(threats));
    wl(std::wstring(STR(STR_REPORT_ERRORS)) + L": " + GroupDigits(errors));
    wchar_t tb[40]; swprintf(tb, 40, L"%lld.%02lld s", ms / 1000, (ms % 1000) / 10);
    wl(std::wstring(STR(STR_REPORT_TIME)) + L": " + tb);
}

std::wstring LatestReportPath() {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::wstring best; FILETIME bt{};
    for (auto& e : fs::directory_iterator(g_reportDir, ec)) {
        if (e.path().extension() != L".txt") continue;
        HANDLE hf = CreateFileW(e.path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) continue;
        FILETIME ft{}; GetFileTime(hf, nullptr, nullptr, &ft); CloseHandle(hf);
        if (CompareFileTime(&ft, &bt) > 0) { bt = ft; best = e.path().wstring(); }
    }
    return best;
}

// ---------------- 扫描线程 ----------------
static bool LocalQueryDosTarget(const std::wstring& drive, std::wstring* out) {
    wchar_t buf[1024];
    DWORD n = QueryDosDeviceW(drive.c_str(), buf, 1024);
    if (!n) return false;
    if (out) *out = buf;
    return true;
}

// 把可能是映射盘符的路径解析为真实路径（K:\x → D:\...\x；真实盘符原样）
static std::wstring ResolveToRealPath(const std::wstring& dir) {
    if (dir.size() < 2 || dir[1] != L':') return dir;
    std::wstring tgt;
    if (!LocalQueryDosTarget(dir.substr(0, 2), &tgt)) return dir;
    if (tgt.rfind(L"\\??\\", 0) != 0) return dir;   // 真实卷
    std::wstring real = tgt.substr(4);
    while (!real.empty() && real.back() == L'\\') real.pop_back();
    return real + dir.substr(2);
}

static void ScanWorker(ScanMode mode, std::wstring customRoot, HWND mainWnd) {
    g_scan.files = 0; g_scan.threats = 0; g_scan.errors = 0;
    g_scan.t0 = std::chrono::steady_clock::now();
    g_scan.engineProgress = -1;

    std::vector<std::wstring> roots;
    if (mode == MODE_QUICK) {
        roots = QuickScanRoots();
        PushLog(L"—— " + std::wstring(STR(STR_LOG_START_QUICK)) + L" ——");
        for (auto& r : roots) PushLog(L"  ∟ " + r);
    } else if (mode == MODE_FULL) {
        DWORD drives = GetLogicalDrives();
        wchar_t root[] = L"A:\\";
        for (int i = 0; i < 26; i++) {
            if (!(drives & (1 << i))) continue;
            root[0] = (wchar_t)(L'A' + i);
            // 跳过 DOS 设备映射盘符（subst/本程序的盘符映射）：
            // 它们不是真实卷，内容经真实卷可达，扫了就是重复劳动
            std::wstring tgt;
            if (LocalQueryDosTarget(root, &tgt) && tgt.rfind(L"\\??\\", 0) == 0) continue;
            if (GetDriveTypeW(root) == DRIVE_FIXED) roots.push_back(root);
        }
        PushLog(L"—— " + std::wstring(STR(STR_LOG_START_FULL)) + L" ——");
    } else {
        roots.push_back(customRoot);
        PushLog(L"—— " + std::wstring(STR(STR_LOG_START_CUSTOM)) + L": " + customRoot + L" ——");
    }

    g_scan.phase = 1; // 加载引擎
    if (!EngineEnsure(mainWnd)) {
        DbInfo db = GetDbInfo();
        if (!db.ok) PushLog(L"[!] " + std::wstring(STR(STR_STATUS_NO_DB)) + L" - " + STR(STR_STATUS_UPDATE));
        else        PushLog(L"[!] ClamAV engine init failed");
        g_scan.phase = 0; g_scan.running = false;
        return;
    }
    PushLog(std::wstring(L"[i] ") + STR(STR_LOG_ENGINE) + L" ClamAV " + Utf8ToW(cl_retver()) +
            L" · " + GroupDigits(g_sigCount) + L" " + STR(STR_LOG_SIGS));
    if (g_scan.cancel) { g_scan.phase = 0; g_scan.running = false; PushLog(STR(STR_LOG_CANCELLED)); return; }

    g_scan.phase = 2;
    std::vector<std::wstring> threatLines;

    // 跳过自身相关目录（毒库/隔离区/报告/程序目录）。
    // 每个目录登记"逻辑路径+真实路径"两种形式（从映射盘符运行时两者不同）
    std::vector<std::wstring> skipDirs;
    {
        auto norm = [](std::wstring s) { if (!s.empty() && s.back() == L'\\') s.pop_back(); return s; };
        for (const std::wstring* d : { &g_exeDir, &g_quarDir, &g_reportDir }) {
            if (d->empty()) continue;
            skipDirs.push_back(ToLowerW(norm(*d)));
            skipDirs.push_back(ToLowerW(norm(ResolveToRealPath(*d))));
        }
        std::wstring edb = EffectiveDbDir();
        if (!edb.empty()) {
            skipDirs.push_back(ToLowerW(norm(edb)));
            skipDirs.push_back(ToLowerW(norm(ResolveToRealPath(edb))));
        }
    }
    auto isSkipDir = [&skipDirs](const std::wstring& p) -> bool {
        std::wstring lp = ToLowerW(StripLongPath(p));
        if (!lp.empty() && lp.back() == L'\\') lp.pop_back();
        for (auto& s : skipDirs) if (lp == s) return true;
        return false;
    };

    unsigned long long maxFileBytes = g_set.skipBigFiles ? ((unsigned long long)g_set.maxFileMB << 20) : 0;

    struct cl_scan_options so{};
    so.parse = CL_SCAN_PARSE_ARCHIVE | CL_SCAN_PARSE_ELF | CL_SCAN_PARSE_PDF | CL_SCAN_PARSE_SWF |
               CL_SCAN_PARSE_HWP3 | CL_SCAN_PARSE_XMLDOCS | CL_SCAN_PARSE_MAIL | CL_SCAN_PARSE_OLE2 |
               CL_SCAN_PARSE_HTML | CL_SCAN_PARSE_PE | CL_SCAN_PARSE_ONENOTE | CL_SCAN_PARSE_IMAGE;

    namespace fs = std::filesystem;
    for (auto& root : roots) {
        if (g_scan.cancel) break;
        std::vector<std::wstring> stack{root};
        while (!stack.empty()) {
            if (g_scan.cancel) break;
            std::wstring dir = stack.back(); stack.pop_back();
            std::error_code ec;
            fs::path dirp(ToLongPath(dir));
            for (fs::directory_iterator it(dirp, fs::directory_options::skip_permission_denied, ec), end;
                 it != end; it.increment(ec)) {
                if (g_scan.cancel) break;
                if (ec) break;
                const fs::path& p = it->path();
                DWORD at = GetFileAttributesW(ToLongPath(p.wstring()).c_str());
                if (at == INVALID_FILE_ATTRIBUTES) continue;
                if (at & FILE_ATTRIBUTE_DIRECTORY) {
                    if (at & FILE_ATTRIBUTE_REPARSE_POINT) continue; // 跳过联接/符号链接防循环
                    if (isSkipDir(p.wstring())) continue;
                    stack.push_back(StripLongPath(p.wstring()));
                } else {
                    if (ExtInList(p.wstring(), g_set.excludeExt)) continue;
                    if (maxFileBytes) {
                        std::error_code ec2;
                        uintmax_t sz = it->file_size(ec2);   // 无需开句柄（内部 GetFileAttributesEx）
                        if (ec2) { g_scan.errors++; continue; }
                        if (sz > (uintmax_t)maxFileBytes) continue;
                    }
                    const char* virname = nullptr;
                    unsigned long scanned = 0;
                    int rc = ScanFileUnicode(StripLongPath(p.wstring()), &virname, &scanned, g_engine, &so);
                    g_scan.files++;
                    if (rc == CL_VIRUS) {
                        g_scan.threats++;
                        std::wstring act = ApplyThreat(StripLongPath(p.wstring()), virname);
                        std::wstring line = L"[!!] " + std::wstring(STR(STR_LOG_FOUND)) + L": " +
                                            (virname ? Utf8ToW(virname) : L"?") + L"  " + StripLongPath(p.wstring()) + L"  " + act;
                        PushLog(line);
                        threatLines.push_back(L"  " + (virname ? Utf8ToW(virname) : L"?") + L" | " +
                                              StripLongPath(p.wstring()) + L" " + act);
                    } else if (rc != CL_CLEAN && rc != CL_SUCCESS && rc != CL_EMAXREC && rc != CL_EMAXSIZE &&
                               rc != CL_EMAXFILES && rc != CL_EFORMAT && rc != CL_EOPEN && rc != CL_EACCES) {
                        g_scan.errors++;
                    }
                }
            }
        }
    }

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_scan.t0).count();
    if (g_scan.cancel) PushLog(L"—— " + std::wstring(STR(STR_LOG_CANCELLED)) + L" ——");
    else               PushLog(L"—— " + std::wstring(STR(STR_LOG_DONE)) + L" ——");
    PushLog(std::wstring(STR(STR_LOG_SUMMARY)) + L": " + STR(STR_REPORT_FILES) + L" " +
            GroupDigits(g_scan.files.load()) + L" · " + STR(STR_REPORT_THREATS) + L" " +
            GroupDigits(g_scan.threats.load()) + L" · " + STR(STR_REPORT_ERRORS) + L" " +
            GroupDigits(g_scan.errors.load()) + L" · " + STR(STR_REPORT_TIME) + L" " +
            std::to_wstring((long long)(ms / 1000)) + L"s");
    WriteReport(mode, g_scan.files, g_scan.threats, g_scan.errors, ms, threatLines);

    g_scan.phase = 0;
    g_scan.running = false;
    PostMessageW(mainWnd, WM_APP + 3, 0, 0); // 通知主窗口扫描结束
}

void StartScan(ScanMode mode, const std::wstring& customRoot, HWND mainWnd) {
    if (g_scan.running) return;
    g_scan.cancel = false;
    g_scan.running = true;
    std::thread(ScanWorker, mode, customRoot, mainWnd).detach();
}

bool ScanBusy() { return g_scan.running; }
