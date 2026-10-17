// KrakenAV - settingsui.cpp
// 设置窗口：自绘复选框/单选钮/确定按钮 + 原生EDIT输入框
#include "settingsui.h"
#include "draw.h"
#include "engine.h"
#include <commdlg.h>
#include <shlobj.h>

using namespace Gdiplus;

static Settings g_work;             // 编辑中的设置副本
static HWND g_setWnd = nullptr;
static HWND g_ed[8] = {};           // 0-3 数字 4-6 路径 7 排除类型
static int  g_hoverId = -1;         // 悬停控件
static HWND g_parent = nullptr;
static bool g_applied = false;

// 命中区域 id
enum {
    H_LANG_ZH = 1, H_LANG_EN,
    H_CB_BIGFILE, H_CB_BIGARCH, H_CB_MANYARCH, H_CB_DEEPARCH,
    H_BRW_ENGINE, H_BRW_DB, H_BRW_QUAR,
    H_RDO_REPORT, H_RDO_DELETE, H_RDO_QUAR,
    H_OK
};

// 几何（逻辑像素，绘制/命中共用）
static RectF L(int x, int y, int w, int h) {
    return RectF((REAL)SX(x), (REAL)SX(y), (REAL)SX(w), (REAL)SX(h));
}

static RectF CbRect(int id) {
    switch (id) {
    case H_LANG_ZH:     return L(110, 20, 110, 26);
    case H_LANG_EN:     return L(240, 20, 130, 26);
    case H_CB_BIGFILE:  return L(28,  112, 120, 24);
    case H_CB_BIGARCH:  return L(28,  148, 120, 24);
    case H_CB_MANYARCH: return L(28,  184, 150, 24);
    case H_CB_DEEPARCH: return L(28,  220, 150, 24);
    case H_RDO_REPORT:  return L(408, 322, 110, 24);
    case H_RDO_DELETE:  return L(408, 352, 80, 24);
    case H_RDO_QUAR:    return L(408, 382, 190, 24);
    case H_OK:          return L(596, 452, 132, 40);
    case H_BRW_ENGINE:  return L(676, 128, 76, 26);
    case H_BRW_DB:      return L(676, 186, 76, 26);
    case H_BRW_QUAR:    return L(676, 244, 76, 26);
    }
    return {};
}

static int HitTest(int px, int py) {
    static const int ids[] = { H_LANG_ZH, H_LANG_EN, H_CB_BIGFILE, H_CB_BIGARCH, H_CB_MANYARCH,
                               H_CB_DEEPARCH, H_RDO_REPORT, H_RDO_DELETE, H_RDO_QUAR, H_OK,
                               H_BRW_ENGINE, H_BRW_DB, H_BRW_QUAR };
    for (int id : ids) {
        RectF r = CbRect(id);
        if (px >= r.X && px < r.GetRight() && py >= r.Y && py < r.GetBottom()) return id;
    }
    return -1;
}

static void SyncEditsFromWork() {
    auto setn = [](HWND e, int v) { wchar_t b[16]; swprintf(b, 16, L"%d", v); SetWindowTextW(e, b); };
    setn(g_ed[0], g_work.maxFileMB);
    setn(g_ed[1], g_work.maxArchMB);
    setn(g_ed[2], g_work.maxArchFiles);
    setn(g_ed[3], g_work.maxArchDepth);
    SetWindowTextW(g_ed[4], g_work.engineDir.c_str());
    SetWindowTextW(g_ed[5], g_work.dbDir.c_str());
    SetWindowTextW(g_ed[6], g_work.quarDir.c_str());
    SetWindowTextW(g_ed[7], g_work.excludeExt.c_str());
}
static void SyncWorkFromEdits() {
    auto getn = [](HWND e, int def) {
        wchar_t b[32]; GetWindowTextW(e, b, 32);
        int v = _wtoi(b);
        if (v < 1) v = def; if (v > 100000) v = 100000;
        return v;
    };
    g_work.maxFileMB = getn(g_ed[0], 30);
    g_work.maxArchMB = getn(g_ed[1], 30);
    g_work.maxArchFiles = getn(g_ed[2], 30);
    g_work.maxArchDepth = getn(g_ed[3], 30);
    wchar_t b[2048];
    GetWindowTextW(g_ed[4], b, 2048); g_work.engineDir = b;
    GetWindowTextW(g_ed[5], b, 2048); g_work.dbDir = b;
    GetWindowTextW(g_ed[6], b, 2048); g_work.quarDir = b;
    GetWindowTextW(g_ed[7], b, 2048); g_work.excludeExt = b;
    SyncEditsFromWork();
}

static void PaintSettings(HWND h, HDC dc) {
    RECT rc; GetClientRect(h, &rc);
    Bitmap bmp(rc.right, rc.bottom);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    SolidBrush bg(COL_BG);
    g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);

    Pen line(Color(0xFF, 0x2E, 0x3E, 0x56), 1);
    g.DrawLine(&line, SX(24), SX(64), SX(380), SX(64));
    g.DrawLine(&line, SX(404), SX(64), SX(736), SX(64));
    Text(&g, STR(STR_SET_LANG), L(24, 14, 80, 26), F_SET, COL_TEXT);

    // 语言复选框
    DrawCheckbox(&g, CbRect(H_LANG_ZH), g_work.lang == LANG_ZH, STR(STR_SET_LANG_ZH), F_SET);
    DrawCheckbox(&g, CbRect(H_LANG_EN), g_work.lang == LANG_EN, STR(STR_SET_LANG_EN), F_SET);

    // ---- 左列：扫描设置 ----
    Text(&g, STR(STR_SET_SCAN), L(24, 80, 200, 26), F_CARD, COL_TEXT);

    auto row = [&](int cbId, bool checked, const wchar_t* head, int edIdx, const wchar_t* tail) {
        DrawCheckbox(&g, CbRect(cbId), checked, head, F_SET);
        // 尾注文字从数字框之后开始
        RECT erc; GetWindowRect(g_ed[edIdx], &erc); MapWindowPoints(nullptr, h, (LPPOINT)&erc, 2);
        RectF tr((float)(erc.right + 6), CbRect(cbId).Y - 2, 400, 28);
        Text(&g, tail, tr, F_SET, COL_TEXT_DIM);
    };
    row(H_CB_BIGFILE,  g_work.skipBigFiles,  STR(STR_SET_SKIP_BIG_FILE), 0, STR(STR_SET_MB));
    row(H_CB_BIGARCH,  g_work.skipBigArch,   STR(STR_SET_SKIP_BIG_FILE), 1, STR(STR_SET_SKIP_BIG_ARCH));
    row(H_CB_MANYARCH, g_work.skipManyArch,  STR(STR_SET_SKIP_MANY_ARCH), 2, STR(STR_SET_FILES_UNIT));
    row(H_CB_DEEPARCH, g_work.skipDeepArch,  STR(STR_SET_SKIP_DEEP_ARCH), 3, STR(STR_SET_LAYERS));

    Text(&g, STR(STR_SET_EXCLUDE), L(28, 262, 360, 24), F_SET, COL_TEXT);
    RectF exclBox = L(28, 290, 340, 30);
    RoundRect(&g, exclBox, 5, Color(0xFF, 0x10, 0x16, 0x1F), COL_CARD_BRD);

    // ---- 右列：路径设置 ----
    Text(&g, STR(STR_SET_PATHS), L(404, 80, 200, 26), F_CARD, COL_TEXT);
    Text(&g, STR(STR_SET_ENGINE_DIR), L(408, 106, 200, 22), F_SUB, COL_TEXT_DIM);
    Text(&g, STR(STR_SET_DB_DIR),     L(408, 164, 200, 22), F_SUB, COL_TEXT_DIM);
    Text(&g, STR(STR_SET_QUAR_DIR),   L(408, 222, 200, 22), F_SUB, COL_TEXT_DIM);
    DrawButton(&g, CbRect(H_BRW_ENGINE), STR(STR_BTN_BROWSE), F_SUB, g_hoverId == H_BRW_ENGINE, false);
    DrawButton(&g, CbRect(H_BRW_DB), STR(STR_BTN_BROWSE), F_SUB, g_hoverId == H_BRW_DB, false);
    DrawButton(&g, CbRect(H_BRW_QUAR), STR(STR_BTN_BROWSE), F_SUB, g_hoverId == H_BRW_QUAR, false);

    // ---- 右列：受感染文件 ----
    Text(&g, STR(STR_SET_ACTION), L(404, 290, 200, 26), F_CARD, COL_TEXT);
    DrawRadio(&g, CbRect(H_RDO_REPORT), g_work.infectedAction == 0, STR(STR_SET_ACT_REPORT), F_SET);
    DrawRadio(&g, CbRect(H_RDO_DELETE), g_work.infectedAction == 1, STR(STR_SET_ACT_DELETE), F_SET);
    DrawRadio(&g, CbRect(H_RDO_QUAR),   g_work.infectedAction == 2, STR(STR_SET_ACT_QUAR), F_SET);

    // ---- 确定按钮 ----
    DrawButton(&g, CbRect(H_OK), STR(STR_BTN_OK), F_CARD, g_hoverId == H_OK, true);

    Graphics dg(dc);
    dg.DrawImage(&bmp, 0, 0);
}

static LRESULT CALLBACK SetProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        auto mkedit = [&](int idx, int x, int y, int cw, DWORD style) {
            g_ed[idx] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | style,
                SX(x), SX(y), SX(cw), SX(26), h, (HMENU)(intptr_t)(100 + idx), nullptr, nullptr);
            SendMessageW(g_ed[idx], WM_SETFONT,
                (WPARAM)CreateFontW(-SX(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                    0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei"), TRUE);
        };
        mkedit(0, 172, 112, 56, ES_NUMBER);
        mkedit(1, 172, 148, 56, ES_NUMBER);
        mkedit(2, 202, 184, 56, ES_NUMBER);
        mkedit(3, 202, 220, 56, ES_NUMBER);
        mkedit(4, 408, 128, 260, 0);
        mkedit(5, 408, 186, 260, 0);
        mkedit(6, 408, 244, 260, 0);
        mkedit(7, 32, 294, 332, 0);
        SyncEditsFromWork();
        break;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetBkColor(dc, RGB(0x10, 0x16, 0x1F));
        SetTextColor(dc, RGB(0xE8, 0xED, 0xF4));
        static HBRUSH br = CreateSolidBrush(RGB(0x10, 0x16, 0x1F));
        return (LRESULT)br;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        PaintSettings(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int id = HitTest(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (id != g_hoverId) { g_hoverId = id; InvalidateRect(h, nullptr, FALSE); }
        SetCursor(id > 0 ? LoadCursor(nullptr, IDC_HAND) : LoadCursor(nullptr, IDC_ARROW));
        break;
    }
    case WM_LBUTTONDOWN: {
        int id = HitTest(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        switch (id) {
        case H_LANG_ZH: case H_LANG_EN:
            g_work.lang = (id == H_LANG_ZH) ? LANG_ZH : LANG_EN;
            break;
        case H_CB_BIGFILE:  g_work.skipBigFiles = !g_work.skipBigFiles; break;
        case H_CB_BIGARCH:  g_work.skipBigArch = !g_work.skipBigArch; break;
        case H_CB_MANYARCH: g_work.skipManyArch = !g_work.skipManyArch; break;
        case H_CB_DEEPARCH: g_work.skipDeepArch = !g_work.skipDeepArch; break;
        case H_RDO_REPORT: g_work.infectedAction = 0; break;
        case H_RDO_DELETE: g_work.infectedAction = 1; break;
        case H_RDO_QUAR:   g_work.infectedAction = 2; break;
        case H_BRW_ENGINE: case H_BRW_DB: case H_BRW_QUAR: {
            std::wstring f = PickFolderDialog(h);
            if (!f.empty()) {
                int idx = id == H_BRW_ENGINE ? 4 : (id == H_BRW_DB ? 5 : 6);
                SetWindowTextW(g_ed[idx], f.c_str());
            }
            break;
        }
        case H_OK: {
            SyncWorkFromEdits();
            g_set = g_work;
            g_lang = g_work.lang;
            SaveSettings();
            EnsureDir(g_dbDir); EnsureDir(g_quarDir); EnsureDir(g_reportDir);
            EngineMarkDirty();
            g_applied = true;
            DestroyWindow(h);
            return 0;
        }
        }
        if (id > 0) InvalidateRect(h, nullptr, FALSE);
        break;
    }
    case WM_DESTROY:
        g_setWnd = nullptr;
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

void ShowSettingsWindow(HWND parent) {
    g_parent = parent;
    g_work = g_set;          // 从当前设置开始编辑
    g_applied = false;
    g_hoverId = -1;
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = SetProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"KrakenSetWnd";
        RegisterClassW(&wc);
        reg = true;
    }
    g_setWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"KrakenSetWnd", L"KrakenAV Settings",
        WS_POPUP | WS_CAPTION,
        CW_USEDEFAULT, CW_USEDEFAULT, SX(768), SX(540),
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowTextW(g_setWnd, STR(STR_SET_TITLE));   // 标题跟随界面语言
    EnableWindow(parent, FALSE);
    ShowWindow(g_setWnd, SW_SHOW);
    // 模态循环（WM_DESTROY 时 g_setWnd 置空退出）
    MSG msg;
    BOOL r;
    while (g_setWnd != nullptr && (r = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (g_applied && g_parent) InvalidateRect(g_parent, nullptr, TRUE);
}
