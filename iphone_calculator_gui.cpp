/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║         iPhone Calculator  —  Win32 / GDI+ GUI              ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * Compile (MSVC, vcvars64 already set):
 *   cl /EHsc /std:c++17 /Fe:iphone_calculator_gui.exe
 *      iphone_calculator_gui.cpp gdiplus.lib user32.lib gdi32.lib
 *
 * Mouse  : click any button
 * Keyboard shortcuts:
 *   0-9 / numpad  digit input            .   decimal point
 *   + - * /       operators              = / Enter  evaluate
 *   C             clear / all-clear      P   toggle sign (+/-)
 *   %             percent                ESC quit
 */

#define _WIN32_WINNT 0x0601
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

using namespace Gdiplus;
using namespace std;

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────
static const int WIN_W    = 340;    // client width
static const int WIN_H    = 590;    // client height
static const int DISP_H   = 176;    // display area height
static const int BTN_W    = 78;     // button width
static const int BTN_H    = 78;     // button height
static const int GAP      = 6;      // gap between buttons
static const int MARGIN   = 5;      // side margin
static const int COL_STEP = BTN_W + GAP;   // 84
static const int ROW_STEP = BTN_H + GAP;   // 84

// ─────────────────────────────────────────────────────────────────────────────
// Calculator state
// ─────────────────────────────────────────────────────────────────────────────
struct CalcState {
    double  current  = 0.0;
    double  stored   = 0.0;
    char    pending  = 0;
    wstring display  = L"0";
    bool    newEntry = true;
    bool    evaled   = false;
    bool    hasPoint = false;
    bool    error    = false;
    char    activeOp = 0;
};
static CalcState S;

// ─────────────────────────────────────────────────────────────────────────────
// Button model
// ─────────────────────────────────────────────────────────────────────────────
enum BtnColor  { BC_GRAY, BC_DARK, BC_ORANGE };
enum BtnAction {
    BA_0=0, BA_1, BA_2, BA_3, BA_4,
    BA_5,   BA_6, BA_7, BA_8, BA_9,
    BA_POINT, BA_CLEAR, BA_SIGN, BA_PCT,
    BA_DIV, BA_MUL, BA_SUB, BA_ADD, BA_EQ
};

struct Button {
    wstring   label;
    RECT      rect;
    BtnColor  clr;
    BtnAction act;
};
static vector<Button> gBtns;

static RECT btnRect(int col, int row, int colSpan = 1)
{
    int x = MARGIN   + col * COL_STEP;
    int y = DISP_H   + row * ROW_STEP;
    int w = BTN_W    + (colSpan - 1) * COL_STEP;
    return { x, y, x + w, y + BTN_H };
}

static void buildButtons()
{
    gBtns = {
        // ── row 0: AC  +/-  %  ÷ ──────────────────────────────────────────
        { L"AC",  btnRect(0,0),   BC_GRAY,   BA_CLEAR },
        { L"+/-", btnRect(1,0),   BC_GRAY,   BA_SIGN  },
        { L"%",   btnRect(2,0),   BC_GRAY,   BA_PCT   },
        { L"\u00F7", btnRect(3,0),BC_ORANGE, BA_DIV   },   // ÷
        // ── row 1: 7  8  9  × ─────────────────────────────────────────────
        { L"7",   btnRect(0,1),   BC_DARK,   BA_7     },
        { L"8",   btnRect(1,1),   BC_DARK,   BA_8     },
        { L"9",   btnRect(2,1),   BC_DARK,   BA_9     },
        { L"\u00D7", btnRect(3,1),BC_ORANGE, BA_MUL   },   // ×
        // ── row 2: 4  5  6  − ─────────────────────────────────────────────
        { L"4",   btnRect(0,2),   BC_DARK,   BA_4     },
        { L"5",   btnRect(1,2),   BC_DARK,   BA_5     },
        { L"6",   btnRect(2,2),   BC_DARK,   BA_6     },
        { L"\u2212", btnRect(3,2),BC_ORANGE, BA_SUB   },   // −
        // ── row 3: 1  2  3  + ─────────────────────────────────────────────
        { L"1",   btnRect(0,3),   BC_DARK,   BA_1     },
        { L"2",   btnRect(1,3),   BC_DARK,   BA_2     },
        { L"3",   btnRect(2,3),   BC_DARK,   BA_3     },
        { L"+",   btnRect(3,3),   BC_ORANGE, BA_ADD   },
        // ── row 4: 0 (wide)  .  = ─────────────────────────────────────────
        { L"0",   btnRect(0,4,2), BC_DARK,   BA_0     },   // double-wide
        { L".",   btnRect(2,4),   BC_DARK,   BA_POINT },
        { L"=",   btnRect(3,4),   BC_ORANGE, BA_EQ    },
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Numeric helpers
// ─────────────────────────────────────────────────────────────────────────────
static wstring fmtNum(double v)
{
    if (!isfinite(v))           return L"Error";
    if (v == 0.0)               return L"0";
    if (v == trunc(v) && fabs(v) < 1e12) {
        wostringstream o; o << fixed << setprecision(0) << v; return o.str();
    }
    wostringstream o; o << setprecision(10) << v;
    wstring s = o.str();
    if (s.find(L'.') != wstring::npos) {
        s.erase(s.find_last_not_of(L'0') + 1);
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    if ((int)s.size() > 12) {
        wostringstream o2; o2 << scientific << setprecision(4) << v; return o2.str();
    }
    return s;
}

static double applyOp(double a, double b, char op)
{
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/':
            if (b == 0.0) { S.error = true; return 0.0; }
            return a / b;
    }
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// Calculator actions
// ─────────────────────────────────────────────────────────────────────────────
static void onDigit(int d)
{
    if (S.error) return;
    wstring ds(1, (wchar_t)(L'0' + d));
    if (S.newEntry || S.evaled) {
        S.display = ds; S.hasPoint = false;
        S.newEntry = false; S.evaled = false;
    } else {
        if ((int)S.display.size() >= 12) return;
        S.display = (S.display == L"0") ? ds : S.display + ds;
    }
    try { S.current = stod(S.display); } catch (...) {}
}

static void onPoint()
{
    if (S.error) return;
    if (S.newEntry || S.evaled) {
        S.display = L"0."; S.hasPoint = true;
        S.newEntry = false; S.evaled = false;
    } else if (!S.hasPoint) {
        S.display += L'.'; S.hasPoint = true;
    }
}

static void onOperator(char op)
{
    if (S.error) return;
    if (S.pending && !S.newEntry) {
        double r = applyOp(S.stored, S.current, S.pending);
        if (!S.error) { S.current = r; S.display = fmtNum(r); }
    }
    S.stored   = S.current;
    S.pending  = op;
    S.activeOp = op;
    S.newEntry = true;
    S.evaled   = false;
}

static void onEquals()
{
    if (S.error || !S.pending) return;
    double r  = applyOp(S.stored, S.current, S.pending);
    S.current = S.error ? 0.0 : r;
    S.display = S.error ? L"Error" : fmtNum(r);
    S.stored  = S.current;
    S.pending = 0; S.activeOp = 0;
    S.newEntry = true; S.evaled = true;
}

static void onClear()
{
    if (S.error ||
        (S.display == L"0" && S.newEntry && !S.pending))
        S = CalcState{};
    else {
        S.display = L"0"; S.current = 0.0;
        S.newEntry = true; S.hasPoint = false;
    }
}

static void onSign()
{
    if (S.error || S.display == L"0") return;
    S.current = -S.current;
    S.display = fmtNum(S.current);
}

static void onPct()
{
    if (S.error) return;
    S.current = (S.pending == '+' || S.pending == '-')
                ? S.stored * (S.current / 100.0)
                : S.current / 100.0;
    S.display = fmtNum(S.current);
    S.newEntry = true;
}

static void dispatch(BtnAction a)
{
    switch (a) {
        case BA_0: onDigit(0); break; case BA_1: onDigit(1); break;
        case BA_2: onDigit(2); break; case BA_3: onDigit(3); break;
        case BA_4: onDigit(4); break; case BA_5: onDigit(5); break;
        case BA_6: onDigit(6); break; case BA_7: onDigit(7); break;
        case BA_8: onDigit(8); break; case BA_9: onDigit(9); break;
        case BA_POINT: onPoint();       break;
        case BA_CLEAR: onClear();       break;
        case BA_SIGN:  onSign();        break;
        case BA_PCT:   onPct();         break;
        case BA_DIV:   onOperator('/'); break;
        case BA_MUL:   onOperator('*'); break;
        case BA_SUB:   onOperator('-'); break;
        case BA_ADD:   onOperator('+'); break;
        case BA_EQ:    onEquals();      break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GDI+ drawing helpers
// ─────────────────────────────────────────────────────────────────────────────

// Fill a rectangle with rounded corners
static void DrawRoundRect(Graphics& g, const Brush& br, const RECT& rc, float r)
{
    float x = (float)rc.left,  y = (float)rc.top;
    float w = (float)(rc.right - rc.left);
    float h = (float)(rc.bottom - rc.top);
    float d = r * 2.0f;
    GraphicsPath path;
    path.AddArc(x,       y,       d, d, 180, 90);
    path.AddArc(x+w-d,   y,       d, d, 270, 90);
    path.AddArc(x+w-d,   y+h-d,   d, d,   0, 90);
    path.AddArc(x,       y+h-d,   d, d,  90, 90);
    path.CloseFigure();
    g.FillPath(&br, &path);
}

// iPhone colors
static Color colorFor(BtnColor c, bool pressed, bool highlight)
{
    if (highlight)  return Color(255, 255, 255, 255);     // white bg (active op)
    switch (c) {
        case BC_GRAY:
            return pressed ? Color(255, 200, 200, 200) : Color(255, 165, 165, 165);
        case BC_ORANGE:
            return pressed ? Color(255, 200, 117,   0) : Color(255, 255, 149,   0);
        default: /* BC_DARK */
            return pressed ? Color(255,  90,  90,  90) : Color(255,  51,  51,  51);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Full-frame render (double-buffered)
// ─────────────────────────────────────────────────────────────────────────────
static int gPressedBtn = -1;

static void Render(HWND hwnd, HDC hdc)
{
    RECT cr; GetClientRect(hwnd, &cr);
    int cw = cr.right, ch = cr.bottom;

    // Off-screen bitmap
    Bitmap   bmp(cw, ch, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

    // ── background ───────────────────────────────────────────────────────────
    SolidBrush bgBr(Color(255, 0, 0, 0));
    g.FillRectangle(&bgBr, 0, 0, cw, ch);

    // ── display number ───────────────────────────────────────────────────────
    wstring disp = S.display;
    if ((int)disp.size() > 12) {
        wostringstream o;
        o << scientific << setprecision(4) << S.current;
        disp = o.str();
    }

    float fSize = (disp.size() <= 5) ? 84.0f
                : (disp.size() <= 7) ? 68.0f
                : (disp.size() <= 9) ? 54.0f
                :                      38.0f;

    FontFamily ff(L"Segoe UI");
    Font dispFont(&ff, fSize, FontStyleRegular, UnitPixel);
    SolidBrush whtBr(Color(255, 255, 255, 255));

    StringFormat sf;
    sf.SetAlignment(StringAlignmentFar);
    sf.SetLineAlignment(StringAlignmentFar);
    // right-margin 18px, bottom-margin 10px
    RectF dispRc(10.0f, 0.0f, (float)(cw - 28), (float)(DISP_H - 10));
    g.DrawString(disp.c_str(), -1, &dispFont, dispRc, &sf, &whtBr);

    // ── update AC label ───────────────────────────────────────────────────────
    for (auto& b : gBtns)
        if (b.act == BA_CLEAR)
            b.label = (S.display == L"0" && S.newEntry && !S.pending) ? L"AC" : L"C";

    // ── buttons ───────────────────────────────────────────────────────────────
    FontFamily btnFF(L"Segoe UI");

    for (int i = 0; i < (int)gBtns.size(); i++) {
        const Button& b = gBtns[i];
        bool pressed = (i == gPressedBtn);

        // Highlight active operator (white bg / orange text)
        char opCh = (b.act == BA_DIV ? '/' :
                     b.act == BA_MUL ? '*' :
                     b.act == BA_SUB ? '-' :
                     b.act == BA_ADD ? '+' : 0);
        bool hlOp = (opCh && S.activeOp == opCh && S.newEntry && !S.evaled);

        // Button background
        SolidBrush bbr(colorFor(b.clr, pressed, hlOp));
        DrawRoundRect(g, bbr, b.rect, 39.0f);   // radius=39 → pill / circle

        // Text color
        Color txtClr;
        if (hlOp)             txtClr = Color(255, 255, 149,  0);   // orange
        else if (b.clr == BC_GRAY) txtClr = Color(255,   0,   0,  0);   // black
        else                  txtClr = Color(255, 255, 255,255);   // white

        // Font size (smaller for "+/-")
        float fs = (b.label == L"+/-") ? 24.0f : 34.0f;
        Font btnFont(&btnFF, fs, FontStyleRegular, UnitPixel);
        SolidBrush tbr(txtClr);

        // For the double-wide "0" button, centre label in the LEFT half (iPhone style)
        RectF rf;
        if (b.act == BA_0) {
            rf = RectF((float)b.rect.left, (float)b.rect.top,
                       (float)BTN_W,       (float)BTN_H);
        } else {
            rf = RectF((float)b.rect.left, (float)b.rect.top,
                       (float)(b.rect.right  - b.rect.left),
                       (float)(b.rect.bottom - b.rect.top));
        }

        StringFormat csf;
        csf.SetAlignment(StringAlignmentCenter);
        csf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(b.label.c_str(), -1, &btnFont, rf, &csf, &tbr);
    }

    // ── blit to screen ────────────────────────────────────────────────────────
    Graphics scr(hdc);
    scr.DrawImage(&bmp, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Window procedure
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_ERASEBKGND:
        return 1;   // prevent flicker — we paint everything in WM_PAINT

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Render(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
        for (int i = 0; i < (int)gBtns.size(); i++) {
            const RECT& r = gBtns[i].rect;
            if (mx >= r.left && mx < r.right &&
                my >= r.top  && my < r.bottom) {
                gPressedBtn = i;
                SetCapture(hwnd);
                dispatch(gBtns[i].act);
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
        gPressedBtn = -1;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    // WM_CHAR handles printable characters (including shifted variants)
    case WM_CHAR: {
        wchar_t c = (wchar_t)wp;
        if      (c >= L'0' && c <= L'9') onDigit(c - L'0');
        else switch (c) {
            case L'.': case L',':  onPoint();       break;
            case L'+':             onOperator('+'); break;
            case L'-':             onOperator('-'); break;
            case L'*':             onOperator('*'); break;
            case L'/':             onOperator('/'); break;
            case L'=': case L'\r': onEquals();      break;
            case L'c': case L'C':  onClear();       break;
            case L'p': case L'P':  onSign();        break;
            case L'%':             onPct();         break;
            case 8: /* Backspace */onClear();       break;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    // WM_KEYDOWN for non-character keys
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { PostQuitMessage(0); return 0; }
        if (wp >= VK_NUMPAD0 && wp <= VK_NUMPAD9)
            { onDigit((int)(wp - VK_NUMPAD0)); InvalidateRect(hwnd,nullptr,FALSE); }
        if (wp == VK_DECIMAL)
            { onPoint(); InvalidateRect(hwnd,nullptr,FALSE); }
        if (wp == VK_ADD)      { onOperator('+'); InvalidateRect(hwnd,nullptr,FALSE); }
        if (wp == VK_SUBTRACT) { onOperator('-'); InvalidateRect(hwnd,nullptr,FALSE); }
        if (wp == VK_MULTIPLY) { onOperator('*'); InvalidateRect(hwnd,nullptr,FALSE); }
        if (wp == VK_DIVIDE)   { onOperator('/'); InvalidateRect(hwnd,nullptr,FALSE); }
        if (wp == VK_RETURN)   { onEquals();      InvalidateRect(hwnd,nullptr,FALSE); }
        return DefWindowProc(hwnd, msg, wp, lp);

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow)
{
    SetProcessDPIAware();   // crisp rendering on high-DPI displays

    // Initialize GDI+
    GdiplusStartupInput gsi;
    ULONG_PTR           gdipToken;
    GdiplusStartup(&gdipToken, &gsi, nullptr);

    buildButtons();

    // Register window class
    WNDCLASSEX wc    = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"iPhoneCalcGUI";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassEx(&wc);

    // Calculate window size including title bar and borders
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT  wr    = { 0, 0, WIN_W, WIN_H };
    AdjustWindowRect(&wr, style, FALSE);

    HWND hwnd = CreateWindowEx(
        0,
        L"iPhoneCalcGUI",
        L"iPhone Calculator",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right  - wr.left,
        wr.bottom - wr.top,
        nullptr, nullptr, hInst, nullptr
    );

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdipToken);
    return (int)msg.wParam;
}
