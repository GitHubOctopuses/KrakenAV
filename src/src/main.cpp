// KrakenAV - main.cpp
// 主窗口：无边框深色界面，标题栏/侧边栏/扫描页/工具页/关于页/底部状态栏
#include "common.h"
#include "draw.h"
#include "engine.h"
#include "updater.h"
#include "settingsui.h"
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>

using namespace Gdiplus;

// ---------------- 中文路径自动处理 ----------------
// ClamAV 官方引擎按"程序所在目录"加载证书/数据库，不支持非 ASCII 路径。
// 方案：检测到程序目录含非 ASCII 字符时，用 DefineDosDevice 把一个空闲盘符
// 映射到程序目录（用户态 API、无需管理员），并经盘符路径透明重启自身。
//
// 单实例 + 防累积设计：
//   - 临时父实例（中文路径）：复用已有映射或新建，只负责拉起子实例，不做清理
//   - 子实例（ASCII 盘符路径）：先抢单实例互斥体；抢到才清理历史残留映射
//     （跳过自己正在用的盘符）；退出时删除自己的映射并释放互斥体
//   - 第二个子实例抢不到互斥体 → 激活已有窗口后退出，不产生新映射
static void RemoveOwnDosDevice();
static bool QueryDosTarget(const std::wstring& drive, std::wstring* out) {
    wchar_t buf[1024];
    DWORD n = QueryDosDeviceW(drive.c_str(), buf, 1024);
    if (!n) return false;
    if (out) *out = buf;
    return true;
}

static bool IsOurMapping(const std::wstring& target, const std::wstring& dir) {
    // 映射目标形如 \??\<dir>；真实分区是 \Device\HarddiskVolume...
    if (target.rfind(L"\\??\\", 0) != 0) return false;
    std::wstring d = dir;
    while (!d.empty() && d.back() == L'\\') d.pop_back();
    if (target.size() != 4 + d.size()) return false;
    return target.compare(4, d.size(), d) == 0;
}

static void CleanupStaleMappings(const std::wstring& dir, const std::wstring& skipDrive) {
    DWORD mask = GetLogicalDrives();
    for (int i = 25; i >= 0; i--) {
        if (!(mask & (1u << i))) continue;
        std::wstring d = std::wstring(1, wchar_t(L'A' + i)) + L":";
        if (d == skipDrive) continue;        // 自己正在用的映射不动
        std::wstring tgt;
        if (!QueryDosTarget(d, &tgt)) continue;
        if (IsOurMapping(tgt, dir)) {
            DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE,
                             d.c_str(), tgt.c_str());
        }
    }
}

// 盘符路径实例退出时删除自己使用的映射（程序目录含中文的会话级映射）
static void RemoveOwnDosDevice() {
    wchar_t exep[MAX_PATH];
    GetModuleFileNameW(nullptr, exep, MAX_PATH);
    if (PathHasNonAscii(exep)) return;       // 仅盘符路径实例负责清理
    wchar_t* sl = wcsrchr(exep, L'\\');
    if (!sl) return;
    std::wstring dir(exep, (size_t)(sl - exep));
    if (dir.size() < 2 || dir[1] != L':') return;
    std::wstring drv = dir.substr(0, 2);     // 如 "K:"
    std::wstring tgt;
    // 判定标准：自己的盘符映射目标以 \??\ 开头（真实分区是 \Device\...）
    // （不比较具体路径——QueryDosDeviceW 对含中文的目标可能返回 ?? 占位）
    if (QueryDosTarget(drv, &tgt) && tgt.rfind(L"\\??\\", 0) == 0) {
        DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE,
                         drv.c_str(), tgt.c_str());
    }
}

// 取得指向 dir 的映射盘符：优先复用已有映射（上次异常退出的残留），
// 否则占用 K:-Z: 中第一个空闲盘符新建。失败返回空串。
static std::wstring AcquireMappingDrive(const std::wstring& dir) {
    DWORD mask = GetLogicalDrives();
    // 复用
    for (wchar_t c = L'K'; c <= L'Z'; c++) {
        if (!(mask & (1u << (c - L'A')))) continue;
        std::wstring d = std::wstring(1, c) + L":";
        std::wstring tgt;
        if (QueryDosTarget(d, &tgt) && IsOurMapping(tgt, dir)) return d;
    }
    // 新建
    for (wchar_t c = L'K'; c <= L'Z'; c++) {
        if (mask & (1u << (c - L'A'))) continue;
        std::wstring d = std::wstring(1, c) + L":";
        if (!DefineDosDeviceW(0, d.c_str(), (L"\\??\\" + dir).c_str())) continue;
        return d;
    }
    return L"";
}

static bool RelaunchViaDosDevice() {
    wchar_t exep[MAX_PATH];
    GetModuleFileNameW(nullptr, exep, MAX_PATH);
    std::wstring exePath = exep;
    if (!PathHasNonAscii(exePath)) return false;
    wchar_t* sl = wcsrchr(exep, L'\\');
    if (!sl) return false;
    std::wstring dir(exep, (size_t)(sl - exep));          // 程序目录（可含中文）
    std::wstring fname = exePath.substr(exePath.find_last_of(L'\\') + 1);

    std::wstring drv = AcquireMappingDrive(dir);
    if (drv.empty()) return false;
    std::wstring newExe = drv + L"\\" + fname;
    if (GetFileAttributesW(newExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // 新建的映射无效则撤销（复用的不撤——可能另有实例在用）
        return false;
    }
    std::wstring cmd = L"\"" + newExe + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(newExe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                       0, nullptr, drv.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    return false;
}


// 布局常量（逻辑像素）
static const int WIN_W = 980, WIN_H = 640;
static const int TITLE_H = 48, SIDE_W = 190, BOTTOM_H = 36;

static HWND g_hMain = nullptr;
static HWND g_hLog = nullptr;          // 扫描页日志 EDIT
static HFONT g_logFont = nullptr;
static int g_page = 0;                 // 0 扫描 1 工具 2 关于
static int g_hover = -1;
static DbInfo g_dbCache;
static bool g_needDbRefresh = true;

// 程序目录是否含非 ASCII 字符（ClamAV certs/毒库更新都不支持中文路径）
static bool ExePathHasNonAscii() { return PathHasNonAscii(g_exeDir); }

// 命中区域
enum {
    H_NONE = 0,
    H_TITLE_GEAR, H_TITLE_MIN, H_TITLE_CLOSE,
    H_NAV0, H_NAV1, H_NAV2,
    H_QUICK, H_FULL, H_CUSTOM, H_STOP,
    H_T_SET, H_T_REP, H_T_UPD, H_T_QUAR,
    H_ABOUT_URL, H_UPDATE_LINK,
};

static RectF Rf(int x, int y, int w, int h) {
    return RectF((REAL)SX(x), (REAL)SX(y), (REAL)SX(w), (REAL)SX(h));
}
static bool InRf(const RectF& r, int px, int py) {
    return px >= r.X && px < r.GetRight() && py >= r.Y && py < r.GetBottom();
}

// ---- 各区域矩形 ----
static RectF TitleBtnRect(int id) {
    int W = SX(WIN_W);
    if (id == H_TITLE_CLOSE) return RectF((REAL)(W - SX(40)), 0, (REAL)SX(40), (REAL)SX(TITLE_H));
    if (id == H_TITLE_MIN)   return RectF((REAL)(W - SX(80)), 0, (REAL)SX(40), (REAL)SX(TITLE_H));
    if (id == H_TITLE_GEAR)  return RectF((REAL)(W - SX(122)), 0, (REAL)SX(42), (REAL)SX(TITLE_H));
    return RectF();
}
static RectF NavRect(int i) {
    return Rf(0, 76 + i * 72, SIDE_W, 64);
}
static RectF ScanCardRect(int i) {
    return Rf(SIDE_W + 24 + i * 246, 56, 230, 140);
}
static RectF StatsRect() { return Rf(SIDE_W + 24, 212, WIN_W - SIDE_W - 48, 28); }
static RectF StopRect()  { return Rf(WIN_W - 110, 210, 86, 32); }
static RectF LogRect()   { return Rf(SIDE_W + 24, 244, WIN_W - SIDE_W - 48, WIN_H - BOTTOM_H - 244 - 14); }
static RectF ToolCardRect(int r, int c) {
    return Rf(SIDE_W + 24 + c * 356, 56 + r * 146, 340, 130);
}
static RectF UpdateLinkRect() {
    return Rf(WIN_W - 210, WIN_H - BOTTOM_H, 190, BOTTOM_H);
}

static int HitTest(int px, int py) {
    if (py < SX(TITLE_H)) {
        for (int id : {H_TITLE_CLOSE, H_TITLE_MIN, H_TITLE_GEAR})
            if (InRf(TitleBtnRect(id), px, py)) return id;
        return H_NONE;
    }
    if (InRf(UpdateLinkRect(), px, py)) return H_UPDATE_LINK;
    if (px < SX(SIDE_W) && py < SX(WIN_H - BOTTOM_H)) {
        for (int i = 0; i < 3; i++) if (InRf(NavRect(i), px, py)) return H_NAV0 + i;
        return H_NONE;
    }
    if (g_page == 0) {
        for (int i = 0; i < 3; i++) if (InRf(ScanCardRect(i), px, py)) return H_QUICK + i;
        if (InRf(StopRect(), px, py)) return H_STOP;
    } else if (g_page == 1) {
        for (int r = 0; r < 2; r++) for (int c = 0; c < 2; c++)
            if (InRf(ToolCardRect(r, c), px, py)) return H_T_SET + r * 2 + c;
    } else if (g_page == 2) {
        RectF url = Rf(SIDE_W + (WIN_W - SIDE_W) / 2 - 250, 300, 500, 30);
        if (InRf(url, px, py)) return H_ABOUT_URL;
    }
    return H_NONE;
}

// 命中区域 → 客户区 RECT（悬停局部重绘用；无矩形返回 false）
static bool RectForId(int id, RECT* rc) {
    RectF r;
    if (id == H_TITLE_CLOSE || id == H_TITLE_MIN || id == H_TITLE_GEAR) r = TitleBtnRect(id);
    else if (id >= H_NAV0 && id <= H_NAV0 + 2)          r = NavRect(id - H_NAV0);
    else if (id >= H_QUICK && id <= H_QUICK + 2)        r = ScanCardRect(id - H_QUICK);
    else if (id == H_STOP)                              r = StopRect();
    else if (id >= H_T_SET && id <= H_T_SET + 3)        r = ToolCardRect((id - H_T_SET) / 2, (id - H_T_SET) % 2);
    else if (id == H_ABOUT_URL)                         r = Rf(SIDE_W + (WIN_W - SIDE_W) / 2 - 250, 300, 500, 30);
    else if (id == H_UPDATE_LINK)                       r = UpdateLinkRect();
    else return false;
    rc->left = (LONG)r.X; rc->top = (LONG)r.Y;
    rc->right = (LONG)r.GetRight() + 1; rc->bottom = (LONG)r.GetBottom() + 1;
    return true;
}

// ---------------- 绘制 ----------------
static void PaintMain(HWND h) {
    RECT rc; GetClientRect(h, &rc);
    int W = rc.right, H = rc.bottom;
    Bitmap bmp(W, H);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // 背景
    SolidBrush bg(COL_BG);
    g.FillRectangle(&bg, 0, 0, W, H);

    // ---- 标题栏 ----
    SolidBrush tbg(COL_SIDEBAR);
    g.FillRectangle(&tbg, 0, 0, W, SX(TITLE_H));
    Pen tline(COL_CARD_BRD, 1);
    g.DrawLine(&tline, 0, SX(TITLE_H) - 1, W, SX(TITLE_H) - 1);
    DrawIcon(&g, IC_KRAKEN, Rf(14, 8, 32, 32), COL_ACCENT_LT);
    Text(&g, L"KrakenAV", Rf(52, 0, 200, TITLE_H), F_TITLE, COL_TEXT);
    {
        for (int id : {H_TITLE_CLOSE, H_TITLE_MIN, H_TITLE_GEAR}) {
            RectF r = TitleBtnRect(id);
            if (g_hover == id) {
                SolidBrush hb(id == H_TITLE_CLOSE ? COL_RED : Color(0xFF, 0x2A, 0x36, 0x48));
                g.FillRectangle(&hb, r);
            }
            Color ic(0xFF, 0xC8, 0xD2, 0xE0);
            if (id == H_TITLE_CLOSE) {
                Pen p(ic, 1.8f); p.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
                g.DrawLine(&p, PointF(r.X + r.Width * 0.36f, r.Y + r.Height * 0.36f),
                                PointF(r.X + r.Width * 0.64f, r.Y + r.Height * 0.64f));
                g.DrawLine(&p, PointF(r.X + r.Width * 0.64f, r.Y + r.Height * 0.36f),
                                PointF(r.X + r.Width * 0.36f, r.Y + r.Height * 0.64f));
            } else if (id == H_TITLE_MIN) {
                Pen p(ic, 1.8f); p.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
                g.DrawLine(&p, PointF(r.X + r.Width * 0.34f, r.Y + r.Height * 0.5f),
                                PointF(r.X + r.Width * 0.66f, r.Y + r.Height * 0.5f));
            } else {
                DrawIcon(&g, IC_GEAR, RectF(r.X + r.Width * 0.24f, r.Y + r.Height * 0.24f,
                                            r.Width * 0.52f, r.Height * 0.52f), ic);
            }
        }
    }

    // ---- 侧边栏 ----
    SolidBrush sb(COL_SIDEBAR);
    g.FillRectangle(&sb, 0, SX(TITLE_H), SX(SIDE_W), H - SX(TITLE_H) - SX(BOTTOM_H));
    for (int i = 0; i < 3; i++) {
        bool active = (g_page == i);
        bool hov = (g_hover == H_NAV0 + i);
        RectF r = NavRect(i);
        if (active) {
            SolidBrush ab(Color(0xFF, 0x1D, 0x2A, 0x3D));
            g.FillRectangle(&ab, r);
            SolidBrush bar(COL_ACCENT);
            g.FillRectangle(&bar, r.X, r.Y, (REAL)SX(4), r.Height);
        } else if (hov) {
            SolidBrush hb(Color(0xFF, 0x19, 0x22, 0x31));
            g.FillRectangle(&hb, r);
        }
        DrawIcon(&g, i == 0 ? IC_SHIELD : (i == 1 ? IC_BOX : IC_DOC),
                 RectF(r.X + SX(24), r.Y + r.Height / 2 - SX(13), SX(26), SX(26)),
                 active ? COL_ACCENT_LT : COL_TEXT_DIM);
        Text(&g, STR((StrId)(STR_NAV_SCAN + i)),
             RectF(r.X + SX(62), r.Y, r.Width - SX(62), r.Height), F_NAV,
             active ? COL_TEXT : COL_TEXT_DIM);
    }

    // ---- 内容区 ----
    int cx0 = SX(SIDE_W);
    if (g_page == 0) {
        // 三张扫描卡片
        struct { StrId t, s; IconKind ic; } cards[3] = {
            { STR_SCAN_QUICK, STR_SCAN_QUICK_SUB, IC_BOLT },
            { STR_SCAN_FULL, STR_SCAN_FULL_SUB, IC_DISK },
            { STR_SCAN_CUSTOM, STR_SCAN_CUSTOM_SUB, IC_FOLDER },
        };
        bool busy = g_scan.running;
        for (int i = 0; i < 3; i++) {
            RectF r = ScanCardRect(i);
            bool hov = (g_hover == H_QUICK + i) && !busy;
            RoundRect(&g, r, 10,
                      busy ? Color(0xFF, 0x1D, 0x28, 0x38) : Color(0xFF, 0x23, 0x2F, 0x41),
                      hov ? COL_ACCENT : COL_CARD_BRD, hov ? 1.6f : 1.0f);
            DrawIcon(&g, cards[i].ic, RectF(r.X + r.Width / 2 - SX(22), r.Y + SX(20),
                                            SX(44), SX(44)),
                     hov ? COL_ACCENT_LT : COL_ACCENT);
            Text(&g, STR(cards[i].t), RectF(r.X, r.Y + SX(72), r.Width, SX(26)),
                 F_CARD, COL_TEXT, StringAlignmentCenter);
            Text(&g, STR(cards[i].s), RectF(r.X, r.Y + SX(100), r.Width, SX(22)),
                 F_SUB, COL_TEXT_DIM, StringAlignmentCenter);
        }
        // 统计行 + 停止按钮
        {
            std::wstring st;
            if (g_scan.phase == 1) {
                st = STR(STR_STAT_LOADING);
                int p = g_scan.engineProgress;
                if (p >= 0) st += L"  " + std::to_wstring(p) + L"%";
                st += L" ...";
            } else {
                long long sec = g_scan.running ?
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - g_scan.t0).count() : 0;
                wchar_t tb[16];
                swprintf(tb, 16, L"%02d:%02d:%02d", (int)(sec / 3600), (int)(sec / 60 % 60), (int)(sec % 60));
                st = std::wstring(STR(STR_STAT_SCANNED)) + L": " + GroupDigits(g_scan.files) +
                     L"    " + STR(STR_STAT_THREATS) + L": " + GroupDigits(g_scan.threats) +
                     L"    " + STR(STR_STAT_ELAPSED) + L": " + tb;
                if (!g_scan.running && g_scan.files > 0) st += L"  ·  " + std::wstring(STR(STR_LOG_DONE));
            }
            Text(&g, st.c_str(), StatsRect(), F_STATS,
                 g_scan.threats > 0 ? COL_TEXT_WARN : COL_TEXT_DIM);
            if (g_scan.running) {
                DrawButton(&g, StopRect(), STR(STR_BTN_STOP), F_STATS, g_hover == H_STOP, false);
                RoundRect(&g, StopRect(), 6, Color(0, 0, 0, 0), COL_RED, 1.2f);
            }
        }
        // 日志框描边
        RoundRect(&g, RectF(LogRect().X - 2, LogRect().Y - 2, LogRect().Width + 4, LogRect().Height + 4),
                  6, Color(0, 0, 0, 0), COL_CARD_BRD, 1.0f);
    } else if (g_page == 1) {
        struct { StrId t, s; IconKind ic; int hid; } tcards[4] = {
            { STR_TOOL_SETTINGS, STR_TOOL_SETTINGS_SUB, IC_GEAR,    H_T_SET },
            { STR_TOOL_REPORT,   STR_TOOL_REPORT_SUB,   IC_DOC,     H_T_REP },
            { STR_TOOL_UPDATE,   STR_TOOL_UPDATE_SUB,   IC_REFRESH, H_T_UPD },
            { STR_TOOL_QUAR,     STR_TOOL_QUAR_SUB,     IC_BOX,     H_T_QUAR },
        };
        for (int i = 0; i < 4; i++) {
            RectF r = ToolCardRect(i / 2, i % 2);
            bool hov = (g_hover == tcards[i].hid);
            RoundRect(&g, r, 10, Color(0xFF, 0x23, 0x2F, 0x41), hov ? COL_ACCENT : COL_CARD_BRD, hov ? 1.6f : 1.0f);
            DrawIcon(&g, tcards[i].ic, RectF(r.X + SX(24), r.Y + r.Height / 2 - SX(20), SX(40), SX(40)),
                     hov ? COL_ACCENT_LT : COL_ACCENT);
            Text(&g, STR(tcards[i].t), RectF(r.X + SX(80), r.Y + SX(34), r.Width - SX(90), SX(28)),
                 F_CARD, COL_TEXT);
            Text(&g, STR(tcards[i].s), RectF(r.X + SX(80), r.Y + SX(66), r.Width - SX(90), SX(24)),
                 F_SUB, COL_TEXT_DIM);
        }
    } else {
        // 关于页
        int ccx = cx0 + (W - cx0) / 2;
        RectF logo((REAL)(ccx - SX(52)), (REAL)SX(70), (REAL)SX(104), (REAL)SX(104));
        DrawIcon(&g, IC_KRAKEN, logo, COL_ACCENT);
        Text(&g, L"KrakenAV", RectF((REAL)cx0, (REAL)SX(190), (REAL)(W - cx0), (REAL)SX(40)),
             F_TITLE, COL_TEXT, StringAlignmentCenter);
        Text(&g, STR(STR_ABOUT_VER), RectF((REAL)cx0, (REAL)SX(236), (REAL)(W - cx0), (REAL)SX(26)),
             F_ABOUT, COL_TEXT_DIM, StringAlignmentCenter);
        Text(&g, STR(STR_ABOUT_ENGINE), RectF((REAL)cx0, (REAL)SX(266), (REAL)(W - cx0), (REAL)SX(26)),
             F_ABOUT, COL_TEXT_DIM, StringAlignmentCenter);
        Text(&g, STR(STR_ABOUT_URL), Rf(SIDE_W + (WIN_W - SIDE_W) / 2 - 250, 300, 500, 30),
             F_ABOUT, COL_ACCENT_LT, StringAlignmentCenter, true);
        Text(&g, STR(STR_ABOUT_GPL), RectF((REAL)cx0, (REAL)SX(470), (REAL)(W - cx0), (REAL)SX(26)),
             F_SUB, Color(0xFF, 0x6B, 0x7A, 0x8C), StringAlignmentCenter);
    }

    // ---- 底部状态栏 ----
    SolidBrush bb(COL_BOTTOM);
    g.FillRectangle(&bb, 0, H - SX(BOTTOM_H), W, SX(BOTTOM_H));
    Pen bl(COL_CARD_BRD, 1);
    g.DrawLine(&bl, 0, H - SX(BOTTOM_H), W, H - SX(BOTTOM_H));
    if (g_needDbRefresh) { g_dbCache = GetDbInfo(); g_needDbRefresh = false; }
    std::wstring dbtext = std::wstring(STR(STR_STATUS_DB)) + L": ";
    if (g_dbCache.ok)
        dbtext += g_dbCache.dateStr + L"  (" + GroupDigits(g_dbCache.sigs) + L" " + STR(STR_STATUS_SIGS) + L")";
    else
        dbtext += STR(STR_STATUS_NO_DB);
    Text(&g, dbtext.c_str(), Rf(SIDE_W + 24, WIN_H - BOTTOM_H, 500, BOTTOM_H), F_SUB,
         g_dbCache.ok ? COL_TEXT_DIM : COL_TEXT_WARN);
    Text(&g, STR(STR_STATUS_UPDATE), UpdateLinkRect(), F_SUB,
         g_hover == H_UPDATE_LINK ? Color(0xFF, 0x8A, 0xCC, 0xF8) : COL_ACCENT_LT,
         StringAlignmentFar);

    // 输出
    HDC dc = GetDC(h);
    {
        Graphics out(dc);
        out.DrawImage(&bmp, 0, 0);
    }
    ReleaseDC(h, dc);
}

// ---------------- 动作 ----------------
static void OpenReport() {
    EnsureDir(g_reportDir);
    std::wstring p = LatestReportPath();
    if (p.empty()) {
        MessageBoxW(g_hMain, STR(STR_MSG_NO_REPORT), L"KrakenAV", MB_OK | MB_ICONINFORMATION);
        SafeShellOpen(g_hMain, g_reportDir);
        return;
    }
    SafeShellOpen(g_hMain, p);
}
static void OpenQuarantine() {
    EnsureDir(g_quarDir);
    SafeShellOpen(g_hMain, g_quarDir);
}

static void DoStartScan(int mode) {
    if (g_scan.running) return;
    if (UpdateRunning()) {
        MessageBoxW(g_hMain, STR(STR_MSG_UPDATING), L"KrakenAV", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!GetDbInfo().ok) {
        if (MessageBoxW(g_hMain, STR(STR_MSG_NO_DB), STR(STR_MSG_NO_DB_TITLE),
                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
            ShowUpdateWindow(g_hMain);
        }
        return;
    }
    std::wstring root;
    if (mode == MODE_CUSTOM) {
        root = PickFolderDialog(g_hMain);
        if (root.empty()) return;
    }
    // 清空日志
    SetWindowTextW(g_hLog, L"");
    g_page = 0;
    ShowWindow(g_hLog, SW_SHOW);
    StartScan((ScanMode)mode, root, g_hMain);
    InvalidateRect(g_hMain, nullptr, FALSE);
}

// ---------------- 窗口过程 ----------------
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_hLog = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            SX(SIDE_W + 24), SX(244), SX(WIN_W - SIDE_W - 48), SX(WIN_H - BOTTOM_H - 244 - 14),
            h, (HMENU)1, nullptr, nullptr);
        g_logFont = CreateFontW(-SX(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
        SendMessageW(g_hLog, WM_SETFONT, (WPARAM)g_logFont, TRUE);
        SetWindowTextW(g_hLog, L"KrakenAV\r\n");
        // 中文路径启动自检：提示从 C:\KrakenAV 入口启动
        if (ExePathHasNonAscii()) {
            PushLog(STR(STR_LOG_PATH_HINT));
        }
        SetTimer(h, 1, 100, nullptr);
        break;
    }
    case WM_TIMER: {
        if (w == 1) {
            DrainLogToEdit(g_hLog);
            if (g_scan.running || g_scan.files != 0) {
                RECT statsRC{ SX(SIDE_W), SX(206), SX(WIN_W), SX(WIN_H - BOTTOM_H) };
                InvalidateRect(h, &statsRC, FALSE);
            }
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        if ((HWND)l == g_hLog) {
            HDC dc = (HDC)w;
            SetBkColor(dc, RGB(0x10, 0x16, 0x1F));
            SetTextColor(dc, RGB(0xC8, 0xD2, 0xE0));
            static HBRUSH br = CreateSolidBrush(RGB(0x10, 0x16, 0x1F));
            return (LRESULT)br;
        }
        break;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        ValidateRect(h, nullptr); // 双缓冲自绘
        PaintMain(h);
        return 0;
    }
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        if (r == HTCLIENT) {
            int px = GET_X_LPARAM(l), py = GET_Y_LPARAM(l);
            POINT pt{ px, py }; ScreenToClient(h, &pt);
            if (pt.y < SX(TITLE_H) && HitTest(pt.x, pt.y) == H_NONE) return HTCAPTION;
        }
        return r;
    }
    case WM_MOUSEMOVE: {
        int px = GET_X_LPARAM(l), py = GET_Y_LPARAM(l);
        int id = HitTest(px, py);
        if (id != g_hover) {
            int old = g_hover;
            g_hover = id;
            // 局部重绘：只失效新旧悬停区域，避免整窗位图重分配
            RECT rc;
            if (RectForId(old, &rc)) InvalidateRect(h, &rc, FALSE);
            if (RectForId(id, &rc))  InvalidateRect(h, &rc, FALSE);
        }
        bool hand = (id == H_UPDATE_LINK || id == H_ABOUT_URL ||
                     id >= H_NAV0 || id == H_TITLE_GEAR ||
                     (g_page == 0 && (id == H_QUICK || id == H_FULL || id == H_CUSTOM || id == H_STOP)) ||
                     (g_page == 1 && id >= H_T_SET && id <= H_T_QUAR));
        SetCursor(LoadCursor(nullptr, hand ? IDC_HAND : IDC_ARROW));
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        break;
    }
    case WM_MOUSELEAVE:
        g_hover = -1;
        InvalidateRect(h, nullptr, FALSE);
        break;
    case WM_LBUTTONDOWN: {
        int px = GET_X_LPARAM(l), py = GET_Y_LPARAM(l);
        int id = HitTest(px, py);
        switch (id) {
        case H_TITLE_CLOSE:
            SendMessageW(h, WM_CLOSE, 0, 0);
            return 0;
        case H_TITLE_MIN:
            ShowWindow(h, SW_MINIMIZE);
            return 0;
        case H_TITLE_GEAR:
        case H_T_SET:
            ShowSettingsWindow(h);
            g_needDbRefresh = true;
            InvalidateRect(h, nullptr, TRUE);
            return 0;
        case H_NAV0: case H_NAV1: case H_NAV2: {
            g_page = id - H_NAV0;
            ShowWindow(g_hLog, g_page == 0 ? SW_SHOW : SW_HIDE);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case H_QUICK: case H_FULL: case H_CUSTOM:
            DoStartScan(id == H_QUICK ? MODE_QUICK : (id == H_FULL ? MODE_FULL : MODE_CUSTOM));
            return 0;
        case H_STOP:
            if (g_scan.running) g_scan.cancel = true;
            return 0;
        case H_T_REP: OpenReport(); return 0;
        case H_T_UPD: ShowUpdateWindow(h); return 0;
        case H_T_QUAR: OpenQuarantine(); return 0;
        case H_ABOUT_URL:
            SafeShellOpen(h, L"https://github.com/GitHubOctopuses/KrakenAV");
            return 0;
        case H_UPDATE_LINK:
            ShowUpdateWindow(h);
            return 0;
        }
        break;
    }
    case WM_APP + 3:  // 扫描结束
        InvalidateRect(h, nullptr, FALSE);
        break;
    case WM_APP + 4:  // 病毒库已更新
        g_needDbRefresh = true;
        InvalidateRect(h, nullptr, FALSE);
        break;
    case WM_CLOSE: {
        if (g_scan.running) {
            g_scan.cancel = true;
            for (int i = 0; i < 50 && g_scan.running; i++) Sleep(100);
        }
        UpdateForceClose();
        DestroyWindow(h);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, 1);
        UpdateForceClose();
        EngineShutdown();
        RemoveOwnDosDevice();    // 清理本实例使用的盘符映射
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------- 入口 ----------------
static HANDLE g_hSingleInstance = nullptr;

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    // 程序目录含中文 → 自动经 ASCII 盘符映射重启（ClamAV 引擎不支持非 ASCII 路径）
    // 临时父实例不抢单实例锁、不做映射清理（都由子实例负责，避免父子竞争）
    if (RelaunchViaDosDevice()) return 0;

    // 单实例：抢不到说明已有实例 → 激活它的窗口后退出（也不动任何映射）
    g_hSingleInstance = CreateMutexW(nullptr, TRUE, L"Local\\KrakenAV_SingleInstance");
    if (g_hSingleInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(L"KrakenMainWnd", nullptr);
        if (prev) {
            ShowWindow(prev, SW_RESTORE);
            SetForegroundWindow(prev);
        }
        CloseHandle(g_hSingleInstance);
        return 0;
    }

    SetProcessDPIAware();
    {
        HDC s = GetDC(nullptr);
        int dpi = GetDeviceCaps(s, LOGPIXELSX);
        ReleaseDC(nullptr, s);
        SetGlobalDpi(dpi);
    }
    // 路径
    wchar_t exep[MAX_PATH];
    GetModuleFileNameW(nullptr, exep, MAX_PATH);
    wchar_t* sl = wcsrchr(exep, L'\\');
    if (sl) *sl = 0;
    g_exeDir = exep;
    g_engineDir = g_exeDir;   // 引擎 DLL 与 exe 同目录（扁平布局）
    g_dbDir = g_exeDir + L"\\database";
    g_quarDir = g_exeDir + L"\\quarantine";
    g_reportDir = g_exeDir + L"\\reports";
    g_confDir = g_exeDir + L"\\conf";
    // 唯一实例就位：清理历史残留映射（上次崩溃遗留），跳过自己正在用的盘符
    if (g_exeDir.size() >= 2 && g_exeDir[1] == L':')
        CleanupStaleMappings(g_exeDir, g_exeDir.substr(0, 2));
    LoadSettings();
    g_lang = g_set.lang;
    g_engineDir = g_set.engineDir;
    g_dbDir = g_set.dbDir;
    g_quarDir = g_set.quarDir;
    EnsureDir(g_dbDir); EnsureDir(g_quarDir); EnsureDir(g_reportDir); EnsureDir(g_confDir);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gi;
    ULONG_PTR gdp;
    GdiplusStartup(&gdp, &gi, nullptr);
    DrawInit();
    EngineInitLibrary();

    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));   // exe 资源里的应用图标
    wc.lpszClassName = L"KrakenMainWnd";
    RegisterClassW(&wc);

    int W = SX(WIN_W), H = SX(WIN_H);
    int sx = (GetSystemMetrics(SM_CXSCREEN) - W) / 2, sy = (GetSystemMetrics(SM_CYSCREEN) - H) / 2;
    g_hMain = CreateWindowExW(0, L"KrakenMainWnd", L"KrakenAV",
        WS_POPUP | WS_MINIMIZEBOX,
        sx, sy, W, H, nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hMain, nShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DrawShutdown();
    GdiplusShutdown(gdp);
    CoUninitialize();
    // 释放单实例锁（RemoveOwnDosDevice 已在 WM_DESTROY 清掉自己的映射）
    if (g_hSingleInstance) {
        ReleaseMutex(g_hSingleInstance);
        CloseHandle(g_hSingleInstance);
    }
    return 0;
}
