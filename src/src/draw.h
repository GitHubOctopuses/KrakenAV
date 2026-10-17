// KrakenAV - draw.h
// GDI+ 绘图助手：配色、圆角卡片、复选框/单选钮、矢量图标、Logo
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>

// ---------- 配色 ----------
#define COL_BG        Gdiplus::Color(0xFF, 0x1B, 0x24, 0x31)
#define COL_SIDEBAR   Gdiplus::Color(0xFF, 0x14, 0x1B, 0x26)
#define COL_BOTTOM    Gdiplus::Color(0xFF, 0x10, 0x16, 0x1F)
#define COL_CARD      Gdiplus::Color(0xFF, 0x23, 0x2F, 0x41)
#define COL_CARD_BRD  Gdiplus::Color(0xFF, 0x2E, 0x3E, 0x56)
#define COL_ACCENT    Gdiplus::Color(0xFF, 0x3D, 0x9B, 0xE9)
#define COL_ACCENT_LT Gdiplus::Color(0xFF, 0x5B, 0xB8, 0xF5)
#define COL_TEXT      Gdiplus::Color(0xFF, 0xE8, 0xED, 0xF4)
#define COL_TEXT_DIM  Gdiplus::Color(0xFF, 0x9A, 0xA7, 0xB8)
#define COL_TEXT_WARN Gdiplus::Color(0xFF, 0xE8, 0xA3, 0x3D)
#define COL_RED       Gdiplus::Color(0xFF, 0xE0, 0x5B, 0x5B)
#define COL_GREEN     Gdiplus::Color(0xFF, 0x5B, 0xC9, 0x8A)

extern Gdiplus::Font* F_TITLE;   // 18 bold
extern Gdiplus::Font* F_NAV;     // 15
extern Gdiplus::Font* F_CARD;    // 16 bold
extern Gdiplus::Font* F_SUB;     // 12
extern Gdiplus::Font* F_STATS;   // 13
extern Gdiplus::Font* F_ABOUT;   // 14
extern Gdiplus::Font* F_SET;     // 13.5->14

void DrawInit();
void DrawShutdown();

// 基础
void RoundRect(Gdiplus::Graphics* g, const Gdiplus::RectF& rc, float radius,
               Gdiplus::Color fill, Gdiplus::Color border, float bw = 1.0f);
void Text(Gdiplus::Graphics* g, const wchar_t* s, const Gdiplus::RectF& rc,
          const Gdiplus::Font* f, Gdiplus::Color c,
          Gdiplus::StringAlignment align = Gdiplus::StringAlignmentNear,
          bool underline = false);

// 控件
void DrawCheckbox(Gdiplus::Graphics* g, const Gdiplus::RectF& box, bool checked,
                  const wchar_t* label, const Gdiplus::Font* f);
void DrawRadio(Gdiplus::Graphics* g, const Gdiplus::RectF& box, bool checked,
               const wchar_t* label, const Gdiplus::Font* f);
void DrawButton(Gdiplus::Graphics* g, const Gdiplus::RectF& rc, const wchar_t* label,
                const Gdiplus::Font* f, bool hover, bool accent = true);

// 图标（居中绘制于 rc）
enum IconKind { IC_SHIELD, IC_BOLT, IC_DISK, IC_FOLDER, IC_GEAR, IC_DOC, IC_REFRESH, IC_BOX, IC_KRAKEN };
void DrawIcon(Gdiplus::Graphics* g, IconKind kind, const Gdiplus::RectF& rc, Gdiplus::Color c);
