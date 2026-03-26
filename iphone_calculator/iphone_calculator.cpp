/*
 * ╔══════════════════════════════════════════════╗
 * ║       iPhone Calculator — C++ Console        ║
 * ╚══════════════════════════════════════════════╝
 *
 * Controls:
 *   0-9         Digit input
 *   .           Decimal point
 *   Backspace   Delete last digit
 *   + - * /     Arithmetic operators
 *   Enter / =   Evaluate
 *   C           Clear entry  (AC when display is 0)
 *   P           Toggle positive / negative  (+/-)
 *   %           Percent
 *   ESC         Quit
 *
 * Requires Windows (uses ANSI true-color & conio.h).
 * Compile:  g++ -std=c++17 -o calc iphone_calculator.cpp
 */

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <windows.h>
#include <conio.h>

using namespace std;

// ─── Console / ANSI setup ────────────────────────────────────────────────────

static HANDLE hCon;

static void initConsole()
{
    hCon = GetStdHandle(STD_OUTPUT_HANDLE);

    // Enable virtual-terminal (ANSI) processing (Windows 10+)
    DWORD mode = 0;
    GetConsoleMode(hCon, &mode);
    SetConsoleMode(hCon, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    SetConsoleTitle(TEXT("iPhone Calculator"));

    // Hide the cursor
    CONSOLE_CURSOR_INFO ci = {1, FALSE};
    SetConsoleCursorInfo(hCon, &ci);

    // Resize console window to fit the widget snugly
    SMALL_RECT wr = {0, 0, 51, 22};
    SetConsoleWindowInfo(hCon, TRUE, &wr);
    COORD cb = {52, 23};
    SetConsoleScreenBufferSize(hCon, cb);
}

// ANSI escape helpers
static string fg(int r, int g, int b)
{
    return "\033[38;2;" + to_string(r) + ";" + to_string(g) + ";" + to_string(b) + "m";
}
static string bg(int r, int g, int b)
{
    return "\033[48;2;" + to_string(r) + ";" + to_string(g) + ";" + to_string(b) + "m";
}

static const char* RST  = "\033[0m";
static const char* CLS  = "\033[2J\033[H";   // clear screen + home cursor
static const char* HIDE = "\033[?25l";
static const char* SHOW = "\033[?25h";

// ── iPhone color palette ──────────────────────────────────────────────────────
static const string APP_BG   = bg( 28,  28,  30);   // #1C1C1E  app background
static const string BTN_GRAY = bg(165, 165, 165);   // #A5A5A5  top row  (AC, +/-, %)
static const string BTN_DARK = bg( 51,  51,  51);   // #333333  number buttons
static const string BTN_ORA  = bg(255, 149,   0);   // #FF9500  operator buttons
static const string BTN_WHT  = bg(255, 255, 255);   // #FFFFFF  highlighted operator
static const string BORDER   = fg( 70,  70,  72);   // border lines
static const string TXT_W    = fg(255, 255, 255);   // white text
static const string TXT_B    = fg(  0,   0,   0);   // black text
static const string TXT_ORA  = fg(255, 149,   0);   // orange text (highlighted op)

// ─── Calculator state ────────────────────────────────────────────────────────

struct CalcState
{
    double current  = 0.0;
    double stored   = 0.0;
    char   pending  = 0;      // pending operator
    string display  = "0";
    bool   newEntry = true;   // next digit starts a fresh number
    bool   evaled   = false;  // just pressed = or Enter
    bool   hasPoint = false;  // decimal point exists in display
    bool   error    = false;
    char   activeOp = 0;      // operator button to highlight
};

static CalcState S;

// ── Helpers ───────────────────────────────────────────────────────────────────

static string fmtNum(double v)
{
    if (!isfinite(v)) return "Error";
    if (v == 0.0)    return "0";
    // Integer form (no trailing .0)
    if (v == trunc(v) && fabs(v) < 1e13)
    {
        ostringstream o;
        o << fixed << setprecision(0) << v;
        return o.str();
    }
    // Floating-point, up to 10 significant digits, strip trailing zeros
    ostringstream o;
    o << setprecision(10) << v;
    string s = o.str();
    if (s.find('.') != string::npos)
    {
        s.erase(s.find_last_not_of('0') + 1);
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

static double applyOp(double a, double b, char op)
{
    switch (op)
    {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/':
            if (b == 0.0) { S.error = true; return 0.0; }
            return a / b;
    }
    return b;
}

// ─── Input handlers ──────────────────────────────────────────────────────────

static void onDigit(char d)
{
    if (S.error) return;
    if (S.newEntry || S.evaled)
    {
        S.display  = string(1, d);
        S.hasPoint = false;
        S.newEntry = false;
        S.evaled   = false;
    }
    else
    {
        if ((int)S.display.size() >= 12) return;
        S.display = (S.display == "0") ? string(1, d) : S.display + d;
    }
    S.current = stod(S.display);
}

static void onPoint()
{
    if (S.error) return;
    if (S.newEntry || S.evaled)
    {
        S.display  = "0.";
        S.hasPoint = true;
        S.newEntry = false;
        S.evaled   = false;
    }
    else if (!S.hasPoint)
    {
        S.display += '.';
        S.hasPoint = true;
    }
}

static void onBackspace()
{
    if (S.error || S.newEntry || S.evaled) return;
    if (S.display.size() <= 1)
    {
        S.display  = "0";
        S.hasPoint = false;
        S.newEntry = true;
    }
    else
    {
        if (S.display.back() == '.') S.hasPoint = false;
        S.display.pop_back();
    }
    S.current = stod(S.display);
}

static void onOperator(char op)
{
    if (S.error) return;
    // Chain: evaluate previous pending operation first
    if (S.pending && !S.newEntry)
    {
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
    S.display = S.error ? "Error" : fmtNum(r);
    S.stored  = S.current;
    S.pending = 0;
    S.activeOp = 0;
    S.newEntry = true;
    S.evaled   = true;
}

static void onClear()
{
    // AC when display already shows 0 and no pending op
    if (S.error || (S.display == "0" && S.newEntry && !S.pending))
        S = CalcState{};
    else
    {
        S.display  = "0";
        S.current  = 0.0;
        S.newEntry = true;
        S.hasPoint = false;
    }
}

static void onToggleSign()
{
    if (S.error || S.display == "0") return;
    S.current = -S.current;
    S.display = fmtNum(S.current);
}

static void onPercent()
{
    if (S.error) return;
    S.current = (S.pending == '+' || S.pending == '-')
                ? S.stored * (S.current / 100.0)
                : S.current / 100.0;
    S.display  = fmtNum(S.current);
    S.newEntry = true;
}

// ─── Drawing helpers ─────────────────────────────────────────────────────────

// Button column widths (inner characters)
static const int W1 = 10, W2 = 10, W3 = 10, W4 = 11;

// Total inner display width  = W1 + pipe + W2 + pipe + W3 + pipe + W4
static const int DISP_W = W1 + 1 + W2 + 1 + W3 + 1 + W4;  // 44

// Separator lines
static const string SEP4 =
    "+" + string(W1,'-') + "+" + string(W2,'-') + "+" + string(W3,'-') + "+" + string(W4,'-') + "+";

static const string SEP_LAST =
    "+" + string(W1+1+W2,'-') + "+" + string(W3,'-') + "+" + string(W4,'-') + "+";

static string centre(const string& s, int w)
{
    int total = (int)s.size();
    if (total >= w) return s.substr(0, w);
    int sp = (w - total) / 2;
    int ex = (w - total) % 2;
    return string(sp, ' ') + s + string(sp + ex, ' ');
}

struct Btn { string label, fgc, bgc; };

static string cell(const Btn& b, int w)
{
    return b.bgc + b.fgc + centre(b.label, w) + RST;
}

static void printRow4(const Btn& a, const Btn& b, const Btn& c, const Btn& d)
{
    cout << APP_BG << BORDER << "|"
         << cell(a, W1) << APP_BG << BORDER << "|"
         << cell(b, W2) << APP_BG << BORDER << "|"
         << cell(c, W3) << APP_BG << BORDER << "|"
         << cell(d, W4) << APP_BG << BORDER << "|\n";
}

static void printRowLast(const Btn& z, const Btn& dot, const Btn& eq)
{
    int w0 = W1 + 1 + W2;   // 0 is double-wide
    cout << APP_BG << BORDER << "|"
         << cell(z,   w0) << APP_BG << BORDER << "|"
         << cell(dot, W3) << APP_BG << BORDER << "|"
         << cell(eq,  W4) << APP_BG << BORDER << "|\n";
}

// ─── Main draw ───────────────────────────────────────────────────────────────

static void draw()
{
    string acLbl = (S.display == "0" && S.newEntry && !S.pending) ? "AC" : "C";

    // Operator button: white-bg + orange-text when actively waiting for 2nd operand
    auto opBtn = [&](char op, const string& lbl) -> Btn {
        bool hl = (S.activeOp == op && S.newEntry && !S.evaled);
        return { lbl, hl ? TXT_ORA : TXT_W, hl ? BTN_WHT : BTN_ORA };
    };

    // Prepare display string
    string disp = S.error ? "Error" : S.display;
    if ((int)disp.size() > 14)
    {
        ostringstream o;
        o << scientific << setprecision(5) << S.current;
        disp = o.str();
    }
    string dispRow = string(DISP_W - (int)disp.size(), ' ') + disp;

    cout << CLS;

    // ── Top border + title ────────────────────────────────────────────────────
    cout << APP_BG << BORDER << "+" << string(DISP_W, '-') << "+\n";
    cout << APP_BG << BORDER << "|"
         << APP_BG << TXT_ORA << centre("iPhone  Calculator", DISP_W)
         << APP_BG << BORDER << "|\n";
    cout << APP_BG << BORDER << "+" << string(DISP_W, '-') << "+\n";

    // ── Display ───────────────────────────────────────────────────────────────
    cout << APP_BG << BORDER << "|"
         << APP_BG << TXT_W  << dispRow
         << APP_BG << BORDER << "|\n";

    // ── Buttons ───────────────────────────────────────────────────────────────
    cout << APP_BG << BORDER << SEP4 << "\n";

    // Row 1 — AC/C  +/-  %  ÷
    printRow4(
        {acLbl, TXT_B, BTN_GRAY},
        {"+/-",  TXT_B, BTN_GRAY},
        {"%",    TXT_B, BTN_GRAY},
        opBtn('/', "/")
    );
    cout << APP_BG << BORDER << SEP4 << "\n";

    // Row 2 — 7  8  9  ×
    printRow4({"7",TXT_W,BTN_DARK},{"8",TXT_W,BTN_DARK},{"9",TXT_W,BTN_DARK}, opBtn('*',"x"));
    cout << APP_BG << BORDER << SEP4 << "\n";

    // Row 3 — 4  5  6  −
    printRow4({"4",TXT_W,BTN_DARK},{"5",TXT_W,BTN_DARK},{"6",TXT_W,BTN_DARK}, opBtn('-',"-"));
    cout << APP_BG << BORDER << SEP4 << "\n";

    // Row 4 — 1  2  3  +
    printRow4({"1",TXT_W,BTN_DARK},{"2",TXT_W,BTN_DARK},{"3",TXT_W,BTN_DARK}, opBtn('+',"+" ));
    cout << APP_BG << BORDER << SEP_LAST << "\n";

    // Row 5 — 0 (wide)  .  =
    Btn eqBtn = {"=", TXT_W, BTN_ORA};
    printRowLast({"0",TXT_W,BTN_DARK}, {".",TXT_W,BTN_DARK}, eqBtn);
    cout << APP_BG << BORDER << SEP_LAST << "\n";

    // ── Legend ────────────────────────────────────────────────────────────────
    cout << RST << "\n"
         << "  [0-9] digits  [Backspace] delete  [C/AC] clear\n"
         << "  [+ - * /] ops  [Enter] or [=] evaluate\n"
         << "  [P] negate  [%] percent  [ESC] quit\n";

    cout.flush();
}

// ─── Entry point ─────────────────────────────────────────────────────────────

int main()
{
    initConsole();
    cout << HIDE;
    draw();

    while (true)
    {
        int ch = _getch();

        if (ch == 27) break;                        // ESC → quit
        if (ch == 0 || ch == 0xE0) { _getch(); continue; }  // skip fn/arrow keys

        if      (ch >= '0' && ch <= '9')  onDigit(static_cast<char>(ch));
        else if (ch == '.')               onPoint();
        else if (ch == 8)                 onBackspace();     // Backspace
        else if (ch == '+')               onOperator('+');
        else if (ch == '-')               onOperator('-');
        else if (ch == '*')               onOperator('*');
        else if (ch == '/')               onOperator('/');
        else if (ch == '=' || ch == '\r') onEquals();
        else if (ch == 'C' || ch == 'c')  onClear();
        else if (ch == 'P' || ch == 'p')  onToggleSign();
        else if (ch == '%')               onPercent();

        draw();
    }

    // Restore terminal
    cout << RST << SHOW << CLS;
    return 0;
}
