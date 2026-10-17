// KrakenAV - draw.cpp
#include "draw.h"
#include "common.h"

using namespace Gdiplus;

Gdiplus::Font* F_TITLE = nullptr;
Gdiplus::Font* F_NAV = nullptr;
Gdiplus::Font* F_CARD = nullptr;
Gdiplus::Font* F_SUB = nullptr;
Gdiplus::Font* F_STATS = nullptr;
Gdiplus::Font* F_ABOUT = nullptr;
Gdiplus::Font* F_SET = nullptr;

static const wchar_t* FONT_NAME = L"Microsoft YaHei";

void DrawInit() {
    float k = DPI() / 96.0f;
    F_TITLE = new Font(FONT_NAME, 17 * k, FontStyleBold, UnitPixel);
    F_NAV   = new Font(FONT_NAME, 14.5f * k, FontStyleRegular, UnitPixel);
    F_CARD  = new Font(FONT_NAME, 15.5f * k, FontStyleBold, UnitPixel);
    F_SUB   = new Font(FONT_NAME, 11.5f * k, FontStyleRegular, UnitPixel);
    F_STATS = new Font(FONT_NAME, 13 * k, FontStyleRegular, UnitPixel);
    F_ABOUT = new Font(FONT_NAME, 13.5f * k, FontStyleRegular, UnitPixel);
    F_SET   = new Font(FONT_NAME, 13.5f * k, FontStyleRegular, UnitPixel);
}
void DrawShutdown() {
    delete F_TITLE; F_TITLE = nullptr;
    delete F_NAV; F_NAV = nullptr;
    delete F_CARD; F_CARD = nullptr;
    delete F_SUB; F_SUB = nullptr;
    delete F_STATS; F_STATS = nullptr;
    delete F_ABOUT; F_ABOUT = nullptr;
    delete F_SET; F_SET = nullptr;
}

void RoundRect(Graphics* g, const RectF& rc, float radius, Color fill, Color border, float bw) {
    GraphicsPath path;
    float d = radius * 2;
    path.AddArc(rc.X, rc.Y, d, d, 180, 90);
    path.AddArc(rc.GetRight() - d, rc.Y, d, d, 270, 90);
    path.AddArc(rc.GetRight() - d, rc.GetBottom() - d, d, d, 0, 90);
    path.AddArc(rc.X, rc.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    SolidBrush b(fill);
    g->FillPath(&b, &path);
    if (border.GetValue() != 0) {
        Pen p(border, bw);
        g->DrawPath(&p, &path);
    }
}

void Text(Graphics* g, const wchar_t* s, const RectF& rc, const Font* f, Color c,
          StringAlignment align, bool underline) {
    if (!s || !*s) return;
    StringFormat sf;
    sf.SetAlignment(align);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsLineLimit);
    if (underline) {
        FontFamily fam;
        f->GetFamily(&fam);
        int style = f->GetStyle() | FontStyleUnderline;
        Font uf(&fam, f->GetSize(), style, f->GetUnit());
        SolidBrush b(c);
        g->DrawString(s, -1, &uf, rc, &sf, &b);
    } else {
        SolidBrush b(c);
        g->DrawString(s, -1, f, rc, &sf, &b);
    }
}

void DrawCheckbox(Graphics* g, const RectF& box, bool checked, const wchar_t* label, const Font* f) {
    float size = f->GetSize() * 0.95f;
    RectF cb(box.X, box.Y + (box.Height - size) / 2, size, size);
    GraphicsPath path;
    float r = 3;
    path.AddArc(cb.X, cb.Y, r * 2, r * 2, 180, 90);
    path.AddArc(cb.GetRight() - r * 2, cb.Y, r * 2, r * 2, 270, 90);
    path.AddArc(cb.GetRight() - r * 2, cb.GetBottom() - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(cb.X, cb.GetBottom() - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
    SolidBrush fill(checked ? COL_ACCENT : Color(0xFF, 0x2A, 0x36, 0x48));
    g->FillPath(&fill, &path);
    Pen p(COL_CARD_BRD, 1.2f);
    g->DrawPath(&p, &path);
    if (checked) { // 对勾
        Pen cp(Color(0xFF, 0xFF, 0xFF, 0xFF), 2.0f);
        cp.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        PointF c1(cb.X + size * 0.24f, cb.Y + size * 0.52f);
        PointF c2(cb.X + size * 0.44f, cb.Y + size * 0.72f);
        PointF c3(cb.X + size * 0.78f, cb.Y + size * 0.30f);
        g->DrawLine(&cp, c1, c2);
        g->DrawLine(&cp, c2, c3);
    }
    if (label && *label) {
        RectF tr(box.X + size + 7, box.Y - 2, 600, box.Height + 4);
        SolidBrush b(COL_TEXT);
        StringFormat sf; sf.SetAlignment(StringAlignmentNear); sf.SetLineAlignment(StringAlignmentCenter);
        g->DrawString(label, -1, f, tr, &sf, &b);
    }
}

void DrawRadio(Graphics* g, const RectF& box, bool checked, const wchar_t* label, const Font* f) {
    float size = f->GetSize() * 0.95f;
    float cy = box.Y + box.Height / 2, cx = box.X + size / 2;
    SolidBrush fill(checked ? COL_ACCENT : Color(0xFF, 0x2A, 0x36, 0x48));
    g->FillEllipse(&fill, cx - size / 2, cy - size / 2, size, size);
    Pen p(COL_CARD_BRD, 1.2f);
    g->DrawEllipse(&p, cx - size / 2, cy - size / 2, size, size);
    if (checked) {
        SolidBrush dot(Color(0xFFFFFFFF));
        g->FillEllipse(&dot, cx - size * 0.18f, cy - size * 0.18f, size * 0.36f, size * 0.36f);
    }
    if (label && *label) {
        RectF tr(box.X + size + 7, box.Y - 2, 600, box.Height + 4);
        SolidBrush b(COL_TEXT);
        StringFormat sf; sf.SetAlignment(StringAlignmentNear); sf.SetLineAlignment(StringAlignmentCenter);
        g->DrawString(label, -1, f, tr, &sf, &b);
    }
}

void DrawButton(Graphics* g, const RectF& rc, const wchar_t* label, const Font* f, bool hover, bool accent) {
    Color fill = hover ? (accent ? Color(0xFF, 0x4F, 0xAA, 0xEE) : Color(0xFF, 0x33, 0x42, 0x58))
                       : (accent ? COL_ACCENT : Color(0xFF, 0x2A, 0x36, 0x48));
    RoundRect(g, rc, 6, fill, Color(0, 0, 0, 0));
    Text(g, label, rc, f, Color(0xFFFFFFFF), StringAlignmentCenter);
}

// ---------------- 矢量图标 ----------------
static void PathShield(GraphicsPath* p, const RectF& rc) {
    float cx = rc.X + rc.Width / 2;
    PointF pts[] = {
        { cx, rc.Y },
        { rc.X + rc.Width, rc.Y + rc.Height * 0.18f },
        { rc.X + rc.Width * 0.96f, rc.Y + rc.Height * 0.55f },
        { cx, rc.Y + rc.Height },
        { rc.X + rc.Width * 0.04f, rc.Y + rc.Height * 0.55f },
        { rc.X, rc.Y + rc.Height * 0.18f },
    };
    p->AddLines(pts, 6);
    p->CloseFigure();
}

void DrawIcon(Graphics* g, IconKind kind, const RectF& rc, Color c) {
    Pen pen(c, rc.Width / 9.0f);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    SolidBrush br(c);
    float x = rc.X, y = rc.Y, w = rc.Width, h = rc.Height;
    switch (kind) {
    case IC_SHIELD: {
        GraphicsPath p; PathShield(&p, rc);
        g->FillPath(&br, &p);
        break;
    }
    case IC_BOLT: { // 闪电（快速）
        PointF pts[] = { {x + w * 0.58f, y}, {x + w * 0.18f, y + h * 0.52f}, {x + w * 0.46f, y + h * 0.52f},
                         {x + w * 0.40f, y + h}, {x + w * 0.82f, y + h * 0.44f}, {x + w * 0.54f, y + h * 0.44f} };
        GraphicsPath p;
        p.AddLines(pts, 6);
        p.CloseFigure();
        g->FillPath(&br, &p);
        break;
    }
    case IC_DISK: { // 硬盘
        GraphicsPath p;
        p.AddEllipse(x + w * 0.08f, y + h * 0.08f, w * 0.84f, h * 0.84f);
        g->DrawPath(&pen, &p);
        GraphicsPath p2; p2.AddEllipse(x + w * 0.36f, y + h * 0.36f, w * 0.28f, h * 0.28f);
        g->FillPath(&br, &p2);
        break;
    }
    case IC_FOLDER: {
        GraphicsPath p;
        p.AddLine(x + w * 0.06f, y + h * 0.28f, x + w * 0.38f, y + h * 0.28f);
        p.AddLine(x + w * 0.46f, y + h * 0.16f, x + w * 0.66f, y + h * 0.16f);
        p.AddLine(x + w * 0.74f, y + h * 0.28f, x + w * 0.94f, y + h * 0.28f);
        p.AddLine(x + w * 0.94f, y + h * 0.28f, x + w * 0.94f, y + h * 0.84f);
        p.AddLine(x + w * 0.94f, y + h * 0.84f, x + w * 0.06f, y + h * 0.84f);
        p.AddLine(x + w * 0.06f, y + h * 0.84f, x + w * 0.06f, y + h * 0.28f);
        p.CloseFigure();
        g->DrawPath(&pen, &p);
        break;
    }
    case IC_GEAR: {
        float cx = x + w / 2, cy = y + h / 2, R = w * 0.36f, r2 = w * 0.22f;
        GraphicsPath ring;
        ring.AddEllipse(cx - R, cy - R, R * 2, R * 2);
        ring.AddEllipse(cx - r2, cy - r2, r2 * 2, r2 * 2);
        g->FillPath(&br, &ring);
        Pen tp(c, w * 0.075f);
        for (int i = 0; i < 8; i++) {
            double a = i * 3.14159265 / 4.0;
            PointF s((REAL)(cx + cos(a) * R * 0.92), (REAL)(cy + sin(a) * R * 0.92));
            PointF e((REAL)(cx + cos(a) * R * 1.25), (REAL)(cy + sin(a) * R * 1.25));
            g->DrawLine(&tp, s, e);
        }
        break;
    }
    case IC_DOC: {
        GraphicsPath p;
        p.AddLine(x + w * 0.22f, y, x + w * 0.66f, y);
        p.AddLine(x + w * 0.66f, y, x + w * 0.86f, y + h * 0.22f);
        p.AddLine(x + w * 0.86f, y + h * 0.22f, x + w * 0.86f, y + h);
        p.AddLine(x + w * 0.86f, y + h, x + w * 0.22f, y + h);
        p.AddLine(x + w * 0.22f, y + h, x + w * 0.22f, y);
        p.CloseFigure();
        g->DrawPath(&pen, &p);
        for (int i = 1; i <= 3; i++) {
            g->DrawLine(&pen, PointF(x + w * 0.34f, y + h * 0.22f * i + h * 0.14f),
                              PointF(x + w * 0.74f, y + h * 0.22f * i + h * 0.14f));
        }
        break;
    }
    case IC_REFRESH: {
        g->DrawArc(&pen, x + w * 0.10f, y + h * 0.10f, w * 0.8f, h * 0.8f, -30, 260);
        PointF tri[] = { {x + w * 0.16f, y + h * 0.02f}, {x + w * 0.38f, y + h * 0.14f}, {x + w * 0.12f, y + h * 0.26f} };
        GraphicsPath p; p.AddLines(tri, 3); p.CloseFigure();
        g->FillPath(&br, &p);
        break;
    }
    case IC_BOX: {
        GraphicsPath p;
        p.AddLine(x + w * 0.5f, y + h * 0.06f, x + w * 0.92f, y + h * 0.28f);
        p.AddLine(x + w * 0.92f, y + h * 0.28f, x + w * 0.92f, y + h * 0.72f);
        p.AddLine(x + w * 0.92f, y + h * 0.72f, x + w * 0.5f, y + h * 0.94f);
        p.AddLine(x + w * 0.5f, y + h * 0.94f, x + w * 0.08f, y + h * 0.72f);
        p.AddLine(x + w * 0.08f, y + h * 0.72f, x + w * 0.08f, y + h * 0.28f);
        p.CloseFigure();
        g->DrawPath(&pen, &p);
        g->DrawLine(&pen, PointF(x + w * 0.08f, y + h * 0.28f), PointF(x + w * 0.5f, y + h * 0.5f));
        g->DrawLine(&pen, PointF(x + w * 0.92f, y + h * 0.28f), PointF(x + w * 0.5f, y + h * 0.5f));
        g->DrawLine(&pen, PointF(x + w * 0.5f, y + h * 0.5f), PointF(x + w * 0.5f, y + h * 0.94f));
        break;
    }
    case IC_KRAKEN: { // 盾 + 眼 + 触手
        GraphicsPath shield; PathShield(&shield, rc);
        Pen sp(c, w / 7.0f);
        g->DrawPath(&sp, &shield);
        float ecx = x + w * 0.5f, ecy = y + h * 0.42f, er = w * 0.19f;
        GraphicsPath eye; eye.AddEllipse(ecx - er, ecy - er, er * 2, er * 2);
        g->FillPath(&br, &eye);
        SolidBrush pup(Color(0xFF, 0x14, 0x1B, 0x26));
        float pr = er * 0.45f;
        GraphicsPath pupil; pupil.AddEllipse(ecx - pr, ecy - pr * 1.3f, pr * 2, pr * 2.6f);
        g->FillPath(&pup, &pupil);
        Pen tp2(c, w / 10.0f);
        tp2.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        GraphicsPath t1;
        t1.AddBezier(Gdiplus::PointF(x + w * 0.26f, y + h * 0.78f),
                     Gdiplus::PointF(x + w * 0.20f, y + h * 0.95f),
                     Gdiplus::PointF(x + w * 0.36f, y + h * 1.00f),
                     Gdiplus::PointF(x + w * 0.30f, y + h * 1.10f));
        g->DrawPath(&tp2, &t1);
        GraphicsPath t2;
        t2.AddBezier(Gdiplus::PointF(x + w * 0.74f, y + h * 0.78f),
                     Gdiplus::PointF(x + w * 0.80f, y + h * 0.95f),
                     Gdiplus::PointF(x + w * 0.64f, y + h * 1.00f),
                     Gdiplus::PointF(x + w * 0.70f, y + h * 1.10f));
        g->DrawPath(&tp2, &t2);
        break;
    }
    }
}
