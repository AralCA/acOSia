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

    void put_dec(u32 v) {
        char b[12]; int i = 0;
        if (v == 0) { putc('0'); return; }
        while (v) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
        while (i--) putc(b[i]);
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

// ============================================================
//  Interrupts: IDT, PIC remap, timer (IRQ0) and keyboard (IRQ1)
//  ------------------------------------------------------------
//  Handlers use GCC's interrupt attribute so they stay in C++.
//  All mutable state lives at a fixed scratch address, so we
//  still need no .bss (which the flat binary does not load).
// ============================================================
struct __attribute__((packed)) IdtEntry {
    u16 off_lo; u16 sel; u8 zero; u8 type; u16 off_hi;
};
struct Kernel {
    IdtEntry idt[256];
    volatile u32 ticks;
    volatile u8  kbuf[32];
    volatile u32 khead, ktail;
};
static Kernel* const K = (Kernel*)0x00030000;

struct InterruptFrame { u32 ip, cs, flags, sp, ss; };

static void kbuf_push(u8 c) {
    u32 nh = (K->khead + 1) & 31;
    if (nh != K->ktail) { K->kbuf[K->khead] = c; K->khead = nh; }
}

__attribute__((interrupt))
static void isr_exception(InterruptFrame*) {
    const char* m = "** CPU exception - halted **";
    volatile u16* v = (volatile u16*)0xB8000;
    for (int i = 0; m[i]; i++) v[i] = (u16)(u8)m[i] | 0x4F00;   // white on red
    for (;;) asm volatile ("cli; hlt");
}
__attribute__((interrupt))
static void isr_timer(InterruptFrame*) {
    K->ticks++;
    outb(0x20, 0x20);                                          // end-of-interrupt to master PIC
}
__attribute__((interrupt))
static void isr_keyboard(InterruptFrame*) {
    u8 sc = inb(0x60);
    if (!(sc & 0x80)) {                                        // ignore key releases
        char c = scancode_map[sc & 0x7F];
        if (c) kbuf_push((u8)c);
    }
    outb(0x20, 0x20);
}
__attribute__((interrupt))
static void isr_irq_default(InterruptFrame*) {
    outb(0x20, 0x20);
}

static void set_gate(int n, u32 addr) {
    K->idt[n].off_lo = (u16)(addr & 0xFFFF);
    K->idt[n].sel    = 0x08;                                   // GDT code selector
    K->idt[n].zero   = 0;
    K->idt[n].type   = 0x8E;                                   // present, ring 0, 32-bit interrupt gate
    K->idt[n].off_hi = (u16)((addr >> 16) & 0xFFFF);
}

static void interrupts_init() {
    K->ticks = 0; K->khead = 0; K->ktail = 0;

    for (int i = 0;  i < 32; i++) set_gate(i, (u32)&isr_exception);   // CPU exceptions
    for (int i = 32; i < 48; i++) set_gate(i, (u32)&isr_irq_default); // hardware IRQs
    set_gate(32, (u32)&isr_timer);                                   // IRQ0
    set_gate(33, (u32)&isr_keyboard);                                // IRQ1

    // remap the PIC: IRQ0..15 -> vectors 0x20..0x2F (clear of CPU exceptions)
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFC); outb(0xA1, 0xFF);                              // unmask IRQ0 and IRQ1 only

    // PIT channel 0 at about 100 Hz
    u32 div = 1193182u / 100u;
    outb(0x43, 0x36);
    outb(0x40, (u8)(div & 0xFF));
    outb(0x40, (u8)((div >> 8) & 0xFF));

    struct __attribute__((packed)) { u16 limit; u32 base; } idtr =
        { (u16)(sizeof(K->idt) - 1), (u32)K->idt };
    asm volatile ("lidt %0" :: "m"(idtr));
    asm volatile ("sti");
}

// ============================================================
//  PS/2 keyboard: reads from the interrupt-filled ring buffer
// ============================================================
class Keyboard {
public:
    // Block until a key is available, sleeping between interrupts.
    char getchar() {
        for (;;) {
            if (K->ktail != K->khead) {
                char c = (char)K->kbuf[K->ktail];
                K->ktail = (K->ktail + 1) & 31;
                return c;
            }
            asm volatile ("hlt");
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
        con.puts("  uptime   time since boot, from the timer interrupt\n");
        con.puts("  reboot   restart the machine\n");
    }
    void cmd_about() {
        con.puts("acOSia v2.0\n");
        con.puts("A freestanding C++ kernel running in 32-bit protected mode.\n");
        con.puts("It has its own VGA driver, an IDT, and an interrupt-driven keyboard.\n");
        con.puts("No operating system and no standard library beneath it.\n");
    }
    void cmd_reboot() {
        con.puts("Rebooting...\n");
        while (inb(0x64) & 2) { }                // wait for 8042 input buffer
        outb(0x64, 0xFE);                        // pulse the CPU reset line
        for (;;) asm volatile ("hlt");
    }
    void cmd_uptime() {
        u32 t = K->ticks;                        // incremented by the 100 Hz timer IRQ
        con.puts("up ");
        con.put_dec(t / 100);
        con.putc('.');
        u32 cs = t % 100;
        if (cs < 10) con.putc('0');
        con.put_dec(cs);
        con.puts(" s  (");
        con.put_dec(t);
        con.puts(" timer ticks)\n");
    }

    void dispatch() {
        const char* s = line;
        if (s[0] == 0)             return;                    // empty line
        if (streq(s, "help"))   { cmd_help();   return; }
        if (streq(s, "clear"))  { con.clear();  return; }
        if (streq(s, "about"))  { cmd_about();  return; }
        if (streq(s, "ver"))    { cmd_about();  return; }
        if (streq(s, "uptime")) { cmd_uptime(); return; }
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
    interrupts_init();

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
