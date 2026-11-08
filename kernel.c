/* VibeOS - a tiny 32-bit x86 kernel.
 * Features: GDT, IDT, PIC, PIT timer, VGA text console, PS/2 keyboard
 * (AZERTY/QWERTY), serial mirror, and a small shell.
 */
#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Port I/O                                                            */
/* ------------------------------------------------------------------ */
static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1" : : "a"(v), "Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

/* ------------------------------------------------------------------ */
/* Tiny libc                                                           */
/* ------------------------------------------------------------------ */
void *memset(void *d, int c, size_t n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
static size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (uint8_t)*a - (uint8_t)*b; }

/* ------------------------------------------------------------------ */
/* Serial (COM1) - mirrors the screen, handy for debugging              */
/* ------------------------------------------------------------------ */
#define COM1 0x3F8
static void serial_init(void) {
    outb(COM1 + 1, 0x00); outb(COM1 + 3, 0x80); outb(COM1 + 0, 0x03); outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); outb(COM1 + 2, 0xC7); outb(COM1 + 4, 0x0B);
}
static void serial_putc(char c) { while (!(inb(COM1 + 5) & 0x20)) {} outb(COM1, (uint8_t)c); }
static int serial_has_data(void) { return inb(COM1 + 5) & 1; }

/* ------------------------------------------------------------------ */
/* VGA text console                                                    */
/* ------------------------------------------------------------------ */
#define VGA_W 80
#define VGA_H 25
static volatile uint16_t *const VGA = (volatile uint16_t *)0xB8000;
static int cx, cy;
static uint8_t color = 0x0F;

static void cursor_update(void) {
    uint16_t pos = (uint16_t)(cy * VGA_W + cx);
    outb(0x3D4, 14); outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15); outb(0x3D5, (uint8_t)(pos & 0xFF));
}

static void scroll(void) {
    for (int i = 0; i < (VGA_H - 1) * VGA_W; i++) VGA[i] = VGA[i + VGA_W];
    for (int i = (VGA_H - 1) * VGA_W; i < VGA_H * VGA_W; i++) VGA[i] = (uint16_t)((color << 8) | ' ');
    cy = VGA_H - 1;
}

static void clear_screen(void) {
    for (int i = 0; i < VGA_W * VGA_H; i++) VGA[i] = (uint16_t)((color << 8) | ' ');
    cx = cy = 0;
    cursor_update();
}

static void putc(char c) {
    if (c == '\n') { serial_putc('\r'); serial_putc('\n'); cx = 0; cy++; }
    else if (c == '\b') {
        serial_putc('\b'); serial_putc(' '); serial_putc('\b');
        if (cx > 0) { cx--; VGA[cy * VGA_W + cx] = (uint16_t)((color << 8) | ' '); }
    } else if (c == '\t') { do putc(' '); while (cx % 4); return; }
    else {
        serial_putc(c);
        VGA[cy * VGA_W + cx] = (uint16_t)((color << 8) | (uint8_t)c);
        if (++cx >= VGA_W) { cx = 0; cy++; }
    }
    if (cy >= VGA_H) scroll();
    cursor_update();
}

static void puts(const char *s) { while (*s) putc(*s++); }

static void put_dec(uint32_t n) {
    char buf[12]; int i = 0;
    if (!n) { putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i--) putc(buf[i]);
}
static void put_sdec(int32_t n) { if (n < 0) { putc('-'); put_dec((uint32_t)(-n)); } else put_dec((uint32_t)n); }
__attribute__((unused)) static void put_hex(uint32_t n) {
    puts("0x");
    for (int i = 28; i >= 0; i -= 4) putc("0123456789ABCDEF"[(n >> i) & 0xF]);
}

static void set_color(uint8_t fg, uint8_t bg) { color = (uint8_t)((bg << 4) | fg); }

/* ------------------------------------------------------------------ */
/* GDT                                                                 */
/* ------------------------------------------------------------------ */
struct gdt_entry { uint16_t limit_low, base_low; uint8_t base_mid, access, gran, base_high; } __attribute__((packed));
struct dt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));
static struct gdt_entry gdt[3];
static struct dt_ptr gdtp;
extern void gdt_flush(uint32_t);

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low = base & 0xFFFF; gdt[i].base_mid = (base >> 16) & 0xFF; gdt[i].base_high = (base >> 24) & 0xFF;
    gdt[i].limit_low = limit & 0xFFFF; gdt[i].gran = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access = access;
}
static void gdt_init(void) {
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF);   /* kernel code */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF);   /* kernel data */
    gdtp.limit = sizeof(gdt) - 1; gdtp.base = (uint32_t)&gdt;
    gdt_flush((uint32_t)&gdtp);
}

/* ------------------------------------------------------------------ */
/* IDT + PIC + PIT                                                     */
/* ------------------------------------------------------------------ */
struct idt_entry { uint16_t base_lo, sel; uint8_t zero, flags; uint16_t base_hi; } __attribute__((packed));
static struct idt_entry idt[256];
static struct dt_ptr idtp;
extern void idt_load(uint32_t);
extern uint32_t isr_table[32];
extern uint32_t irq_table[16];

static void idt_set(int i, uint32_t handler) {
    idt[i].base_lo = handler & 0xFFFF; idt[i].base_hi = (handler >> 16) & 0xFFFF;
    idt[i].sel = 0x08; idt[i].zero = 0; idt[i].flags = 0x8E;
}

static void pic_init(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFC);   /* only IRQ0 (timer) + IRQ1 (keyboard) */
    outb(0xA1, 0xFF);
}

#define PIT_HZ 100
static volatile uint32_t ticks;
static void pit_init(void) {
    uint32_t div = 1193182 / PIT_HZ;
    outb(0x43, 0x36); outb(0x40, (uint8_t)(div & 0xFF)); outb(0x40, (uint8_t)(div >> 8));
}

static void idt_init(void) {
    for (int i = 0; i < 32; i++) idt_set(i, isr_table[i]);
    for (int i = 0; i < 16; i++) idt_set(32 + i, irq_table[i]);
    idtp.limit = sizeof(idt) - 1; idtp.base = (uint32_t)&idt;
    idt_load((uint32_t)&idtp);
}

static const char *exc_names[32] = {
    "Divide by zero", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range", "Invalid opcode",
    "Device not available", "Double fault", "Coprocessor overrun", "Invalid TSS", "Segment not present",
    "Stack fault", "General protection fault", "Page fault", "Reserved", "x87 FPU error",
    "Alignment check", "Machine check", "SIMD FP exception", "Virtualization", "Control protection",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Hypervisor injection",
    "VMM communication", "Security exception", "Reserved"
};

void exc_handler(uint32_t vec) {
    set_color(15, 4);
    puts("\n*** KERNEL PANIC: ");
    puts(vec < 32 ? exc_names[vec] : "Unknown");
    puts(" (exception ");
    put_dec(vec);
    puts(") ***\nSystem halted.\n");
}

/* ------------------------------------------------------------------ */
/* Keyboard (PS/2, scancode set 1)                                     */
/* ------------------------------------------------------------------ */
static uint8_t km_normal[128], km_shift[128];
static int keymap_is_azerty;

static const uint8_t qwerty_n[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
};
static const uint8_t qwerty_s[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' '
};

struct patch { uint8_t sc, n, s; };
/* CP437 bytes: e-acute 0x82, e-grave 0x8A, c-cedilla 0x87, a-grave 0x85, u-grave 0x97 */
static const struct patch azerty_patch[] = {
    {0x02,'&','1'}, {0x03,0x82,'2'}, {0x04,'"','3'}, {0x05,'\'','4'}, {0x06,'(','5'}, {0x07,'-','6'},
    {0x08,0x8A,'7'}, {0x09,'_','8'}, {0x0A,0x87,'9'}, {0x0B,0x85,'0'}, {0x0C,')',0xF8}, {0x0D,'=','+'},
    {0x10,'a','A'}, {0x11,'z','Z'}, {0x1E,'q','Q'}, {0x2C,'w','W'}, {0x27,'m','M'},
    {0x1A,'^','^'}, {0x1B,'$',0x9C}, {0x28,0x97,'%'}, {0x29,0xFD,0xFD}, {0x2B,'*',0xE6},
    {0x32,',','?'}, {0x33,';','.'}, {0x34,':','/'}, {0x35,'!',0x15}, {0x56,'<','>'},
};

static void set_keymap(int azerty) {
    memcpy(km_normal, qwerty_n, 128); memcpy(km_shift, qwerty_s, 128);
    if (azerty) for (size_t i = 0; i < sizeof(azerty_patch) / sizeof(azerty_patch[0]); i++) {
        km_normal[azerty_patch[i].sc] = azerty_patch[i].n;
        km_shift[azerty_patch[i].sc] = azerty_patch[i].s;
    }
    keymap_is_azerty = azerty;
}

#define KB_BUF 256
static volatile char kb_buf[KB_BUF];
static volatile uint32_t kb_head, kb_tail;
static void kb_push(char c) {
    uint32_t next = (kb_head + 1) % KB_BUF;
    if (next != kb_tail) { kb_buf[kb_head] = c; kb_head = next; }
}
static char kb_getc(void) {
    while (kb_head == kb_tail) __asm__ volatile("hlt");
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF;
    return c;
}

static void keyboard_irq(void) {
    static int shift, caps, ext;
    uint8_t sc = inb(0x60);
    if (sc == 0xE0) { ext = 1; return; }
    int release = sc & 0x80; sc &= 0x7F;
    if (ext) { ext = 0; return; }
    if (sc == 0x2A || sc == 0x36) { shift = !release; return; }
    if (release) return;
    if (sc == 0x3A) { caps = !caps; return; }
    uint8_t ch = shift ? km_shift[sc] : km_normal[sc];
    if (caps && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) ch ^= 0x20;
    if (ch) kb_push((char)ch);
}

void irq_handler(uint32_t vec) {
    if (vec == 32) {
        ticks++;
        /* poll serial so the shell also works from the terminal */
        while (serial_has_data()) {
            char c = (char)inb(COM1);
            if (c == '\r') c = '\n';
            if (c == 0x7F) c = '\b';
            kb_push(c);
        }
    } else if (vec == 33) {
        keyboard_irq();
    }
    if (vec >= 40) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* ------------------------------------------------------------------ */
/* Shell                                                               */
/* ------------------------------------------------------------------ */
struct mb_info { uint32_t flags, mem_lower, mem_upper; };
static struct mb_info *mbi;

static void readline(char *buf, int max) {
    int n = 0;
    for (;;) {
        char c = kb_getc();
        if (c == '\n') { putc('\n'); buf[n] = 0; return; }
        if (c == '\b') { if (n > 0) { n--; putc('\b'); } continue; }
        if (c == '\t') continue;
        if ((uint8_t)c >= 32 && n < max - 1) { buf[n++] = c; putc(c); }
    }
}

static int parse_int(const char *s, int *out) {
    int neg = 0, v = 0, any = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = 1; }
    if (!any || *s) return 0;
    *out = neg ? -v : v; return 1;
}

static void sleep_ticks(uint32_t t) { uint32_t end = ticks + t; while (ticks < end) __asm__ volatile("hlt"); }

static void banner(void) {
    set_color(11, 0);
    puts("  __     ___ _          ___  ____  \n");
    puts("  \\ \\   / (_) |__   ___ / _ \\/ ___| \n");
    puts("   \\ \\ / /| | '_ \\ / _ \\ | | \\___ \\ \n");
    puts("    \\ V / | | |_) |  __/ |_| |___) |\n");
    puts("     \\_/  |_|_.__/ \\___|\\___/|____/ \n");
    set_color(15, 0);
    puts("\n  Welcome to VibeOS. Type 'help'.\n\n");
}

static void cmd_cpu(void) {
    uint32_t b, c, d, dummy;
    char v[13];
    __asm__ volatile("cpuid" : "=a"(dummy), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    memcpy(v, &b, 4); memcpy(v + 4, &d, 4); memcpy(v + 8, &c, 4); v[12] = 0;
    puts("CPU vendor: "); puts(v); putc('\n');
}

static void run(char *line) {
    char *arg = line;
    while (*arg && *arg != ' ') arg++;
    if (*arg) { *arg++ = 0; while (*arg == ' ') arg++; }
    if (!*line) return;

    if (!strcmp(line, "help")) {
        puts("Commands:\n"
             "  help            this list\n"
             "  clear           clear the screen\n"
             "  echo <text>     print text\n"
             "  about           about VibeOS\n"
             "  uptime          time since boot\n"
             "  mem             memory info\n"
             "  cpu             CPU vendor\n"
             "  calc a op b     + - * / %  (e.g. calc 6 * 7)\n"
             "  color <0-15>    change text color\n"
             "  keymap [azerty|qwerty]\n"
             "  rainbow         colors!\n"
             "  reboot | shutdown\n");
    } else if (!strcmp(line, "clear")) {
        clear_screen();
    } else if (!strcmp(line, "echo")) {
        puts(arg); putc('\n');
    } else if (!strcmp(line, "about")) {
        puts("VibeOS 0.1 - a hobby kernel, vibe-coded.\n32-bit x86, VGA text, PS/2 keyboard, PIT timer.\n");
    } else if (!strcmp(line, "uptime")) {
        uint32_t s = ticks / PIT_HZ;
        put_dec(s / 60); puts("m "); put_dec(s % 60); puts("s ("); put_dec(ticks); puts(" ticks)\n");
    } else if (!strcmp(line, "mem")) {
        if (mbi && (mbi->flags & 1)) {
            puts("Lower: "); put_dec(mbi->mem_lower); puts(" KB\n");
            puts("Upper: "); put_dec(mbi->mem_upper / 1024); puts(" MB\n");
        } else puts("No memory info from bootloader.\n");
    } else if (!strcmp(line, "cpu")) {
        cmd_cpu();
    } else if (!strcmp(line, "calc")) {
        char *a = arg, *op = 0, *b = 0;
        while (*arg && *arg != ' ') arg++;
        if (*arg) { *arg++ = 0; while (*arg == ' ') arg++; op = arg; }
        while (*arg && *arg != ' ') arg++;
        if (*arg) { *arg++ = 0; while (*arg == ' ') arg++; b = arg; }
        int x, y;
        if (!op || !b || !parse_int(a, &x) || !parse_int(b, &y) || op[1]) { puts("usage: calc a op b\n"); return; }
        switch (op[0]) {
            case '+': put_sdec(x + y); putc('\n'); break;
            case '-': put_sdec(x - y); putc('\n'); break;
            case '*': put_sdec(x * y); putc('\n'); break;
            case '/': if (!y) puts("division by zero\n"); else { put_sdec(x / y); putc('\n'); } break;
            case '%': if (!y) puts("division by zero\n"); else { put_sdec(x % y); putc('\n'); } break;
            default: puts("unknown operator\n");
        }
    } else if (!strcmp(line, "color")) {
        int n;
        if (parse_int(arg, &n) && n >= 0 && n <= 15) { set_color((uint8_t)n, 0); puts("Color changed.\n"); }
        else puts("usage: color <0-15>\n");
    } else if (!strcmp(line, "keymap")) {
        if (!strcmp(arg, "azerty")) set_keymap(1);
        else if (!strcmp(arg, "qwerty")) set_keymap(0);
        puts("Keymap: "); puts(keymap_is_azerty ? "azerty" : "qwerty"); putc('\n');
    } else if (!strcmp(line, "rainbow")) {
        uint8_t old = color;
        const char *t = "VibeOS rainbow!";
        for (int row = 0; row < 3; row++) {
            for (size_t i = 0; i < strlen(t); i++) { set_color((uint8_t)(1 + (i + row) % 15), 0); putc(t[i]); }
            putc('\n');
        }
        color = old;
    } else if (!strcmp(line, "reboot")) {
        puts("Rebooting...\n"); sleep_ticks(30);
        while (inb(0x64) & 2) {}
        outb(0x64, 0xFE);
    } else if (!strcmp(line, "shutdown") || !strcmp(line, "halt")) {
        puts("Shutting down...\n"); sleep_ticks(30);
        outw(0x604, 0x2000);      /* QEMU */
        outw(0xB004, 0x2000);     /* older QEMU / Bochs */
        puts("It is now safe to turn off your computer.\n");
        for (;;) __asm__ volatile("cli; hlt");
    } else {
        set_color(12, 0); puts("Unknown command: "); puts(line); set_color(15, 0);
        puts("\nType 'help'.\n");
    }
}

/* ------------------------------------------------------------------ */
/* Entry                                                               */
/* ------------------------------------------------------------------ */
void kmain(uint32_t magic, struct mb_info *info) {
    serial_init();
    set_keymap(1);              /* AZERTY by default, use 'keymap qwerty' to switch */
    clear_screen();
    if (magic == 0x2BADB002) mbi = info;

    gdt_init();
    idt_init();
    pic_init();
    pit_init();
    __asm__ volatile("sti");

    banner();
    char line[128];
    for (;;) {
        set_color(10, 0); puts("vibe"); set_color(7, 0); puts("@os"); set_color(15, 0); puts("> ");
        readline(line, sizeof(line));
        run(line);
    }
}
