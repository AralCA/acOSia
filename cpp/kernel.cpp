// ============================================================
//  acOSia v2 - freestanding C++ kernel (32-bit protected mode)
//  ------------------------------------------------------------
//  No standard library, no OS beneath. The kernel talks to the
//  hardware directly: VGA text memory at 0xB8000 for output and
//  the PS/2 controller (ports 0x60/0x64) for keyboard input.
// ============================================================

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned int       usize;

// ------------------------------------------------------------
//  Runtime bits the compiler may emit references to. In a hosted
//  program libc / the C++ runtime provide these; here we are it.
// ------------------------------------------------------------
extern "C" void* memset(void* dst, int c, usize n) {
    u8* p = (u8*)dst;
    while (n--) *p++ = (u8)c;
    return dst;
}
extern "C" void* memcpy(void* dst, const void* src, usize n) {
    u8* d = (u8*)dst; const u8* s = (const u8*)src;
    while (n--) *d++ = *s++;
    return dst;
}
// Referenced by an abstract class's vtable; never actually called here.
extern "C" void __cxa_pure_virtual() { for (;;) { } }

// ------------------------------------------------------------
//  x86 port I/O
// ------------------------------------------------------------
static inline void outb(u16 port, u8 val) {
    asm volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline u8 inb(u16 port) {
    u8 r;
    asm volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

// ------------------------------------------------------------
//  Small string helper
// ------------------------------------------------------------
static bool streq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}

// ============================================================
//  VGA text-mode console (80x25, memory-mapped at 0xB8000)
// ============================================================
class Vga {
    static const int W = 80;
    static const int H = 25;
    volatile u16* const buf;
    int row, col;
    u8  color;                              // high nibble bg, low nibble fg

    u16 cell(char c) const { return (u16)(u8)c | ((u16)color << 8); }

    void move_cursor() {
        u16 pos = (u16)(row * W + col);
        outb(0x3D4, 14); outb(0x3D5, (u8)(pos >> 8));
        outb(0x3D4, 15); outb(0x3D5, (u8)(pos & 0xFF));
    }
    void scroll() {
        for (int y = 1; y < H; y++)
            for (int x = 0; x < W; x++)
                buf[(y - 1) * W + x] = buf[y * W + x];
        for (int x = 0; x < W; x++)
            buf[(H - 1) * W + x] = cell(' ');
        row = H - 1;
    }
public:
    Vga() : buf((volatile u16*)0xB8000), row(0), col(0), color(0x0F) {}

    void set_color(u8 c) { color = c; }

    void clear() {
        for (int i = 0; i < W * H; i++) buf[i] = cell(' ');
        row = 0; col = 0;
        move_cursor();
    }
    void putc(char c) {
        if (c == '\n')      { col = 0; row++; }
        else if (c == '\r') { col = 0; }
        else if (c == '\b') { if (col > 0) { col--; buf[row * W + col] = cell(' '); } }
        else {
            buf[row * W + col] = cell(c);
            if (++col >= W) { col = 0; row++; }
        }
        if (row >= H) scroll();
        move_cursor();
    }
    void puts(const char* s) { while (*s) putc(*s++); }

    // print a signed integer in base 10
    void put_dec(int v) {
        char buf2[12]; int i = 0;
        bool neg = v < 0;
        unsigned int u = neg ? (unsigned int)(-v) : (unsigned int)v;
        if (u == 0) { putc('0'); return; }
        while (u) { buf2[i++] = (char)('0' + (u % 10)); u /= 10; }
        if (neg) putc('-');
        while (i--) putc(buf2[i]);
    }

    // direct cell write for the game (does not move the text cursor)
    void put_at(int x, int y, char c, u8 col) {
        buf[y * W + x] = (u16)(u8)c | ((u16)col << 8);
    }
    void hide_cursor() { outb(0x3D4, 0x0A); outb(0x3D5, 0x20); }
    void show_cursor() {
        outb(0x3D4, 0x0A); outb(0x3D5, 0x0E);
        outb(0x3D4, 0x0B); outb(0x3D5, 0x0F);
    }
};

// ============================================================
//  PS/2 keyboard driver (scancode set 1)
// ============================================================
static const char scancode_map[128] = {
/*0x00*/  0,   27,  '1', '2', '3', '4', '5', '6',
/*0x08*/ '7', '8', '9', '0', '-', '=','\b','\t',
/*0x10*/ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
/*0x18*/ 'o', 'p', '[', ']','\n',  0,  'a', 's',
/*0x20*/ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
/*0x28*/'\'', '`',  0, '\\','z', 'x', 'c', 'v',
/*0x30*/ 'b', 'n', 'm', ',', '.', '/',  0,  '*',
/*0x38*/  0,  ' ',  0,   0,   0,   0,   0,   0,
    // 0x40..0x7F: unused -> 0
};

class Keyboard {
public:
    // Block until a key with a printable/ASCII mapping is pressed.
    char getchar() {
        for (;;) {
            if (inb(0x64) & 1) {                 // output buffer full?
                u8 sc = inb(0x60);
                if (sc & 0x80) continue;         // key release -> ignore
                char c = scancode_map[sc & 0x7F];
                if (c) return c;
            }
        }
    }

    // non-blocking: return a key, or -1 if nothing is waiting
    int poll() {
        if (inb(0x64) & 1) {
            u8 sc = inb(0x60);
            if (sc & 0x80) return -1;            // key release
            char c = scancode_map[sc & 0x7F];
            if (c) return (unsigned char)c;
        }
        return -1;
    }
};

// ============================================================
//  Support types for the C++ feature demo
// ============================================================
template <typename T>
static constexpr T kmax(T a, T b) { return a > b ? a : b; }

static constexpr u32 fib(u32 n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

struct Vec2 {                                    // operator overloading
    int x, y;
    Vec2 operator+(const Vec2& o) const { return Vec2{ x + o.x, y + o.y }; }
};

struct Shape {                                   // polymorphism
    virtual int area() const = 0;
    virtual const char* name() const = 0;
};
struct Rect : Shape {
    int w, h;
    Rect(int a, int b) : w(a), h(b) {}
    int area() const override { return w * h; }
    const char* name() const override { return "Rect"; }
};
struct Square : Shape {
    int s;
    Square(int a) : s(a) {}
    int area() const override { return s * s; }
    const char* name() const override { return "Square"; }
};

struct ScopeGuard {                              // RAII: destructor fires on scope exit
    Vga& con;
    ~ScopeGuard() { con.puts("  [RAII]      destructor ran when scope exited\n"); }
};

// ============================================================
//  PIT (timer) polling for real-time pacing, since we have no
//  interrupts. Channel 0 runs near 1.193182 MHz.
// ============================================================
static u16 pit_read() {
    outb(0x43, 0x00);                        // latch channel 0
    u8 lo = inb(0x40);
    u8 hi = inb(0x40);
    return (u16)(lo | (hi << 8));
}
static void pit_delay(u32 ms) {
    u32 target = ms * 1193;                  // counts per millisecond
    u32 elapsed = 0;
    u16 prev = pit_read();
    while (elapsed < target) {
        u16 cur = pit_read();
        elapsed += (u16)(prev - cur);        // counter counts down; u16 math handles wrap
        prev = cur;
    }
}

// ============================================================
//  factory - a tiny Factorio-like game
//  ------------------------------------------------------------
//  A grid world with ore. Miners dig ore and push items onto
//  belts; belts carry them to a hub, which scores them. Game
//  state lives at a fixed scratch address so we need no heap.
// ============================================================
static const int MAPW = 80;
static const int MAPH = 22;                  // screen: row 0 HUD, rows 1..22 map, 23/24 help
static const int DX[4] = { 0, 1, 0, -1 };    // dir 0 up, 1 right, 2 down, 3 left
static const int DY[4] = { -1, 0, 1, 0 };

struct World {
    u8 base [MAPH][MAPW];                     // 0 empty, 1 ore
    u8 build[MAPH][MAPW];                     // 0 none, 1 belt, 2 miner, 3 hub
    u8 dir  [MAPH][MAPW];                     // facing / flow direction
    u8 item [MAPH][MAPW];                     // 0/1, an item riding a belt
    u8 nit  [MAPH][MAPW];                     // next-tick scratch
};

class Game {
    Vga& con;
    Keyboard& kbd;
    World* const w;                          // scratch RAM: no heap, no .bss
    u32 seed;
    int curx, cury;
    int tool;                                // 1 belt, 2 miner, 3 hub
    int tool_dir;                            // placement direction
    u32 score;
    u32 tick_count;
    bool paused;

    u32 rnd() { seed = seed * 1664525u + 1013904223u; return seed; }

    static char belt_char(int d) {
        return d == 0 ? '^' : d == 1 ? '>' : d == 2 ? 'v' : '<';
    }
    const char* tool_name() {
        return tool == 1 ? "belt " : tool == 2 ? "miner" : "hub  ";
    }
    void text(int x, int y, const char* s, u8 col) {
        while (*s) con.put_at(x++, y, *s++, col);
    }
    void num(int x, int y, u32 v, u8 col) {
        char b[12]; int i = 0;
        if (v == 0) { con.put_at(x, y, '0', col); return; }
        while (v) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
        while (i--) con.put_at(x++, y, b[i], col);
    }

    void init() {
        con.hide_cursor();
        con.clear();
        seed = ((u32)pit_read() << 3) ^ 0xACED1234u;

        for (int y = 0; y < MAPH; y++)
            for (int x = 0; x < MAPW; x++) {
                w->base[y][x]  = (rnd() % 100) < 7 ? 1 : 0;
                w->build[y][x] = 0;
                w->dir[y][x]   = 1;
                w->item[y][x]  = 0;
            }

        // a small starter factory so the world is alive on entry
        w->base[10][10] = 1; w->build[10][10] = 2; w->dir[10][10] = 1;   // miner on ore, facing right
        for (int x = 11; x <= 14; x++) { w->build[10][x] = 1; w->dir[10][x] = 1; }
        w->build[10][15] = 3;                                            // hub

        curx = 20; cury = 10;
        tool = 1; tool_dir = 1;
        score = 0; tick_count = 0; paused = false;

        for (int x = 0; x < MAPW; x++) { con.put_at(x, 23, ' ', 0x08); con.put_at(x, 24, ' ', 0x08); }
        text(0, 23, "Move WASD   Place SPACE   Delete X   Rotate R   Tool: 1 belt  2 miner  3 hub", 0x07);
        text(0, 24, "Pause P   Quit Q      Goal: miner on ore # -> belts -> hub H, raise the score", 0x07);
    }

    void draw_hud() {
        for (int x = 0; x < MAPW; x++) con.put_at(x, 0, ' ', 0x1F);
        text(1, 0, "acOSia factory", 0x1F);
        text(18, 0, "score", 0x1F); num(24, 0, score, 0x1E);
        text(32, 0, "tool", 0x1F);  text(37, 0, tool_name(), 0x1F);
        text(45, 0, "dir", 0x1F);   con.put_at(49, 0, belt_char(tool_dir), 0x1F);
        text(54, 0, "tick", 0x1F);  num(59, 0, tick_count, 0x1F);
        if (paused) text(72, 0, "PAUSED", 0x1C);
    }

    void draw_map() {
        for (int y = 0; y < MAPH; y++)
            for (int x = 0; x < MAPW; x++) {
                char ch; u8 col;
                u8 b = w->build[y][x];
                if (b == 1) {
                    if (w->item[y][x]) { ch = 'o'; col = 0x0E; }
                    else               { ch = belt_char(w->dir[y][x]); col = 0x0B; }
                } else if (b == 2) {
                    ch = 'M'; col = w->base[y][x] ? 0x0A : 0x0C;         // green on ore, else red
                } else if (b == 3) {
                    ch = 'H'; col = 0x0D;
                } else if (w->base[y][x]) {
                    ch = '#'; col = 0x06;
                } else {
                    ch = ' '; col = 0x00;
                }
                if (x == curx && y == cury) {                            // cursor: blue background
                    if (ch == ' ') ch = '+';
                    col = (u8)((col & 0x0F) | 0x10);
                    if ((col & 0x0F) == 0) col = (u8)((col & 0xF0) | 0x0F);
                }
                con.put_at(x, y + 1, ch, col);
            }
    }

    bool handle(int k) {
        switch (k) {
            case 'w': if (cury > 0)        cury--; break;
            case 's': if (cury < MAPH - 1) cury++; break;
            case 'a': if (curx > 0)        curx--; break;
            case 'd': if (curx < MAPW - 1) curx++; break;
            case '1': tool = 1; break;
            case '2': tool = 2; break;
            case '3': tool = 3; break;
            case 'r': tool_dir = (tool_dir + 1) & 3; break;
            case ' ':
                w->build[cury][curx] = (u8)tool;
                w->dir[cury][curx]   = (u8)tool_dir;
                w->item[cury][curx]  = 0;
                break;
            case 'x':
                w->build[cury][curx] = 0;
                w->item[cury][curx]  = 0;
                break;
            case 'p': paused = !paused; break;
            case 'q': return true;
        }
        return false;
    }

    void tick() {
        memset(w->nit, 0, sizeof(w->nit));
        // belts carry their item one tile forward
        for (int y = 0; y < MAPH; y++)
            for (int x = 0; x < MAPW; x++) {
                if (w->build[y][x] != 1 || !w->item[y][x]) continue;
                int d = w->dir[y][x];
                int tx = x + DX[d], ty = y + DY[d];
                bool moved = false;
                if (tx >= 0 && tx < MAPW && ty >= 0 && ty < MAPH) {
                    u8 tb = w->build[ty][tx];
                    if (tb == 3) { score++; moved = true; }              // hub collects it
                    else if (tb == 1 && !w->item[ty][tx] && !w->nit[ty][tx]) {
                        w->nit[ty][tx] = 1; moved = true;
                    }
                }
                if (!moved) w->nit[y][x] = 1;                            // blocked, stay put
            }
        // miners dig ore and drop items on the belt they face
        if ((tick_count & 3) == 0) {
            for (int y = 0; y < MAPH; y++)
                for (int x = 0; x < MAPW; x++) {
                    if (w->build[y][x] != 2 || !w->base[y][x]) continue;
                    int d = w->dir[y][x];
                    int tx = x + DX[d], ty = y + DY[d];
                    if (tx >= 0 && tx < MAPW && ty >= 0 && ty < MAPH &&
                        w->build[ty][tx] == 1 && !w->item[ty][tx] && !w->nit[ty][tx]) {
                        w->nit[ty][tx] = 1;
                    }
                }
        }
        memcpy(w->item, w->nit, sizeof(w->item));
        tick_count++;
    }

public:
    Game(Vga& c, Keyboard& k)
        : con(c), kbd(k), w((World*)0x00020000),
          seed(1), curx(0), cury(0), tool(1), tool_dir(1),
          score(0), tick_count(0), paused(false) {}

    void run() {
        init();
        draw_hud();
        draw_map();
        u32 frame = 0;
        for (;;) {
            int k = kbd.poll();
            bool quit = false;
            while (k >= 0) { if (handle(k)) quit = true; k = kbd.poll(); }
            if (quit) break;
            if (!paused && (frame % 8) == 0) tick();
            draw_hud();
            draw_map();
            frame++;
            pit_delay(20);
        }
        con.show_cursor();
        con.clear();
    }
};

// ============================================================
//  Shell
// ============================================================
class Shell {
    Vga& con;
    Keyboard& kbd;
    char line[128];

    void read_line() {
        int len = 0;
        for (;;) {
            char c = kbd.getchar();
            if (c == '\n') { con.putc('\n'); break; }
            if (c == '\b') { if (len > 0) { len--; con.putc('\b'); } continue; }
            if (len < (int)sizeof(line) - 1) {
                line[len++] = c;
                con.putc(c);
            }
        }
        line[len] = 0;
    }

    void cmd_help() {
        con.puts("Commands:\n");
        con.puts("  help     show this help\n");
        con.puts("  demo     show off C++ language features\n");
        con.puts("  factory  play a tiny factory game (mini Factorio)\n");
        con.puts("  clear    clear the screen\n");
        con.puts("  echo X   print the text X\n");
        con.puts("  about    what is acOSia\n");
        con.puts("  ver      version information\n");
        con.puts("  reboot   restart the machine\n");
    }
    void cmd_about() {
        con.puts("acOSia v2.0\n");
        con.puts("A freestanding C++ kernel running in 32-bit protected mode.\n");
        con.puts("It writes its own VGA text driver and PS/2 keyboard driver.\n");
        con.puts("No operating system and no standard library beneath it.\n");
    }
    void cmd_reboot() {
        con.puts("Rebooting...\n");
        while (inb(0x64) & 2) { }                // wait for 8042 input buffer
        outb(0x64, 0xFE);                        // pulse the CPU reset line
        for (;;) asm volatile ("hlt");
    }

    // Show real C++ features executing on bare metal.
    void cmd_demo() {
        con.puts("=== C++ language features, running on bare metal ===\n");

        con.puts("  [template]   kmax(42, 17)        = ");
        con.put_dec(kmax(42, 17)); con.putc('\n');

        con.puts("  [template]   kmax('a','z')       = ");
        con.putc(kmax('a', 'z')); con.putc('\n');

        constexpr u32 f = fib(15);               // evaluated by the compiler
        con.puts("  [constexpr]  fib(15) @ compile   = ");
        con.put_dec((int)f); con.puts("   (no runtime cost)\n");

        Vec2 v = Vec2{ 1, 2 } + Vec2{ 3, 4 };    // operator+
        con.puts("  [operator+]  (1,2)+(3,4)         = (");
        con.put_dec(v.x); con.puts(", "); con.put_dec(v.y); con.puts(")\n");

        Rect r(10, 5); Square sq(6);             // virtual dispatch via base ptr
        Shape* shapes[2] = { &r, &sq };
        con.puts("  [virtual]    ");
        for (int i = 0; i < 2; i++) {
            con.puts(shapes[i]->name()); con.puts(" area=");
            con.put_dec(shapes[i]->area()); con.puts("   ");
        }
        con.putc('\n');

        con.puts("  [colors]     ");             // exercise the VGA color attribute
        for (u8 c = 1; c <= 15; c++) { con.set_color((u8)(c << 4)); con.puts("  "); }
        con.set_color(0x0F); con.putc('\n');

        ScopeGuard g{ con };                     // its destructor prints on return
        con.puts("  all of the above: no std lib, no OS, ~9 KB of C++.\n");
    }

    void cmd_factory() {
        Game g(con, kbd);
        g.run();
    }

    void dispatch() {
        const char* s = line;
        if (s[0] == 0)             return;                    // empty line
        if (streq(s, "help"))   { cmd_help();   return; }
        if (streq(s, "demo"))    { cmd_demo();    return; }
        if (streq(s, "factory")) { cmd_factory(); return; }
        if (streq(s, "clear"))   { con.clear();   return; }
        if (streq(s, "about"))  { cmd_about();  return; }
        if (streq(s, "ver"))    { cmd_about();  return; }
        if (streq(s, "reboot")) { cmd_reboot(); return; }
        if (s[0]=='e' && s[1]=='c' && s[2]=='h' && s[3]=='o') {
            if (s[4] == 0)   { con.putc('\n');                    return; }
            if (s[4] == ' ') { con.puts(s + 5); con.putc('\n');   return; }
        }
        con.puts("Unknown command: ");
        con.puts(s);
        con.putc('\n');
    }
public:
    Shell(Vga& c, Keyboard& k) : con(c), kbd(k) {}
    void run() {
        for (;;) {
            con.puts("acOSia> ");
            read_line();
            dispatch();
        }
    }
};

// ============================================================
//  Kernel entry point (called from kernel_entry.asm)
// ============================================================
extern "C" void kmain() {
    Vga con;
    Keyboard kbd;

    con.set_color(0x0F);
    con.clear();
    con.puts("========================================\n");
    con.puts("        a c O S i a   v2.0  (C++)\n");
    con.puts("   a 32-bit protected-mode C++ kernel\n");
    con.puts("========================================\n");
    con.puts("Type 'demo' for a C++ tour, 'factory' to play, or 'help'.\n\n");

    Shell shell(con, kbd);
    shell.run();                                 // never returns
}
