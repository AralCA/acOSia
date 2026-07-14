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
//  Runtime helpers the compiler may emit calls to. In a hosted
//  program libc provides these; here we are libc.
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

    void dispatch() {
        const char* s = line;
        if (s[0] == 0)             return;                    // empty line
        if (streq(s, "help"))   { cmd_help();   return; }
        if (streq(s, "clear"))  { con.clear();  return; }
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
    con.puts("Type 'help' for a list of commands.\n\n");

    Shell shell(con, kbd);
    shell.run();                                 // never returns
}
