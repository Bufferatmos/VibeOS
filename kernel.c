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
#define KEY_UP    0x11
#define KEY_DOWN  0x12
#define KEY_LEFT  0x13
#define KEY_RIGHT 0x14

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
    if (ext) {
        ext = 0;
        if (!release) {
            if (sc == 0x48) kb_push(KEY_UP);
            else if (sc == 0x50) kb_push(KEY_DOWN);
            else if (sc == 0x4B) kb_push(KEY_LEFT);
            else if (sc == 0x4D) kb_push(KEY_RIGHT);
        }
        return;
    }
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
            if (c == 0x1B && serial_has_data()) {
                char c2 = (char)inb(COM1);
                if (c2 == '[') {
                    int spin = 100000;
                    while (!serial_has_data() && --spin) {}
                    char c3 = serial_has_data() ? (char)inb(COM1) : 0;
                    if (c3 == 'A') kb_push(KEY_UP);
                    else if (c3 == 'B') kb_push(KEY_DOWN);
                    else if (c3 == 'C') kb_push(KEY_RIGHT);
                    else if (c3 == 'D') kb_push(KEY_LEFT);
                    continue;
                }
                kb_push(0x1B);
                c = c2;
            }
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
/* RAM filesystem (in-memory only; contents are lost on reboot)        */
/* ------------------------------------------------------------------ */
#define RAMFS_FILE_COUNT 16
#define RAMFS_NAME_CAP 16
#define RAMFS_DATA_CAP 192

struct ramfs_entry {
    int used;
    char name[RAMFS_NAME_CAP];
    char data[RAMFS_DATA_CAP];
    size_t size;
};
static struct ramfs_entry ramfs[RAMFS_FILE_COUNT];

static int ramfs_name_valid(const char *name) {
    size_t n = 0;
    if (!name || !*name) return 0;
    for (; name[n]; n++) {
        unsigned char c = (unsigned char)name[n];
        if (n >= RAMFS_NAME_CAP - 1) return 0;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 0;
    }
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static struct ramfs_entry *ramfs_find(const char *name) {
    if (!ramfs_name_valid(name)) return 0;
    for (int i = 0; i < RAMFS_FILE_COUNT; i++)
        if (ramfs[i].used && !strcmp(ramfs[i].name, name)) return &ramfs[i];
    return 0;
}

static struct ramfs_entry *ramfs_create(const char *name) {
    for (int i = 0; i < RAMFS_FILE_COUNT; i++) {
        if (!ramfs[i].used) {
            memset(&ramfs[i], 0, sizeof(ramfs[i]));
            memcpy(ramfs[i].name, name, strlen(name) + 1);
            ramfs[i].used = 1;
            return &ramfs[i];
        }
    }
    return 0;
}

static void ramfs_ls(void) {
    int found = 0;
    for (int i = 0; i < RAMFS_FILE_COUNT; i++) {
        if (!ramfs[i].used) continue;
        found = 1;
        puts(ramfs[i].name);
        puts("  ");
        put_dec((uint32_t)ramfs[i].size);
        puts(" bytes\n");
    }
    if (!found) puts("RAMFS is empty. Try: touch <name>\n");
}

static void ramfs_df(void) {
    uint32_t files = 0, bytes = 0;
    for (int i = 0; i < RAMFS_FILE_COUNT; i++) {
        if (!ramfs[i].used) continue;
        files++;
        bytes += (uint32_t)ramfs[i].size;
    }
    puts("RAMFS: ");
    put_dec(files);
    puts("/");
    put_dec(RAMFS_FILE_COUNT);
    puts(" files, ");
    put_dec(bytes);
    puts("/");
    put_dec(RAMFS_FILE_COUNT * (RAMFS_DATA_CAP - 1));
    puts(" bytes used\n");
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

/* ------------------------------------------------------------------ */
/* Text-mode UI ("desktop"): windows, file manager, tiny editor        */
/* ------------------------------------------------------------------ */
#define A_DESK  0x19   /* light blue on blue (desktop pattern) */
#define A_BAR   0x70   /* black on grey */
#define A_WIN   0x70
#define A_BORD  0x7F   /* white on grey */
#define A_TITLE 0x1F   /* white on blue */
#define A_SEL   0x1F
#define A_DIM   0x78   /* dark grey on grey */
#define A_ERR   0x74   /* red on grey */
#define UI_ROWS 10     /* visible rows in the file list */

static void cpu_vendor(char *v) {
    uint32_t b, c, d, dummy;
    __asm__ volatile("cpuid" : "=a"(dummy), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    memcpy(v, &b, 4); memcpy(v + 4, &d, 4); memcpy(v + 8, &c, 4); v[12] = 0;
}

static int fmt_u(char *o, uint32_t n) {
    char t[11]; int i = 0, k = 0;
    if (!n) t[i++] = '0';
    while (n) { t[i++] = (char)('0' + n % 10); n /= 10; }
    while (i) o[k++] = t[--i];
    o[k] = 0;
    return k;
}
static void fmt2(char *o, uint32_t v) { o[0] = (char)('0' + (v / 10) % 10); o[1] = (char)('0' + v % 10); o[2] = 0; }
static void str_cat(char *d, const char *s) { while (*d) d++; while ((*d++ = *s++)) {} }
static void cat_u(char *d, uint32_t n) { char t[12]; fmt_u(t, n); str_cat(d, t); }

static void ui_cell(int x, int y, uint8_t ch, uint8_t at) {
    if (x < 0 || x >= VGA_W || y < 0 || y >= VGA_H) return;
    VGA[y * VGA_W + x] = (uint16_t)((at << 8) | ch);
}
static void ui_fill(int x, int y, int w, int h, uint8_t ch, uint8_t at) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) ui_cell(x + i, y + j, ch, at);
}
static void ui_text(int x, int y, const char *s, int maxw, uint8_t at) {
    for (int i = 0; s[i] && (maxw < 0 || i < maxw); i++) ui_cell(x + i, y, (uint8_t)s[i], at);
}

/* window with a double-line border (CP437 box characters) and a title */
static void ui_box(int x, int y, int w, int h, const char *title) {
    ui_fill(x, y, w, h, ' ', A_WIN);
    ui_cell(x, y, 0xC9, A_BORD);          ui_cell(x + w - 1, y, 0xBB, A_BORD);
    ui_cell(x, y + h - 1, 0xC8, A_BORD);  ui_cell(x + w - 1, y + h - 1, 0xBC, A_BORD);
    for (int i = 1; i < w - 1; i++) { ui_cell(x + i, y, 0xCD, A_BORD); ui_cell(x + i, y + h - 1, 0xCD, A_BORD); }
    for (int j = 1; j < h - 1; j++) { ui_cell(x, y + j, 0xBA, A_BORD); ui_cell(x + w - 1, y + j, 0xBA, A_BORD); }
    if (title) ui_text(x + 2, y, title, w - 4, A_TITLE);
}

/* word-less text wrapping inside a w x h area; returns end position */
static void ui_wrap(int x, int y, int w, int h, const char *s, uint8_t at, int *ex, int *ey) {
    int px = 0, py = 0;
    for (; *s; s++) {
        if (*s == '\n') { px = 0; py++; continue; }
        if (px >= w) { px = 0; py++; }
        if (py >= h) break;
        ui_cell(x + px, y + py, (uint8_t)*s, at);
        px++;
    }
    if (ex) *ex = px;
    if (ey) *ey = py;
}

/* wait for a key; returns 0 after 0.5 s so the clock keeps refreshing */
static int ui_getkey(void) {
    uint32_t t0 = ticks;
    while (kb_head == kb_tail) {
        __asm__ volatile("hlt");
        if (ticks - t0 >= 50) return 0;
    }
    return (uint8_t)kb_getc();
}

static int ui_list(int *idx) {
    int n = 0;
    for (int i = 0; i < RAMFS_FILE_COUNT; i++) if (ramfs[i].used) idx[n++] = i;
    return n;
}

static void ui_kv(int y, const char *key, const char *val) {
    ui_text(42, y, key, -1, A_DIM);
    ui_text(50, y, val, 27, A_WIN);
}

static void ui_draw(const int *idx, int count, int sel, int top, const char *msg) {
    static char vendor[13];
    char b[64], t[4];
    uint32_t secs = ticks / PIT_HZ, files = 0, bytes = 0;
    if (!vendor[0]) cpu_vendor(vendor);
    for (int i = 0; i < RAMFS_FILE_COUNT; i++) if (ramfs[i].used) { files++; bytes += (uint32_t)ramfs[i].size; }

    ui_fill(0, 1, VGA_W, VGA_H - 2, 0xB1, A_DESK);

    /* top bar */
    ui_fill(0, 0, VGA_W, 1, ' ', A_BAR);
    ui_text(1, 0, "VibeOS", -1, 0x7C);
    ui_text(9, 0, "Desktop", -1, A_BAR);
    b[0] = 0; str_cat(b, "Up "); fmt2(t, (secs / 60) % 100); str_cat(b, t); str_cat(b, ":"); fmt2(t, secs % 60); str_cat(b, t);
    str_cat(b, "   "); str_cat(b, keymap_is_azerty ? "AZERTY" : "QWERTY");
    ui_text(VGA_W - 1 - (int)strlen(b), 0, b, -1, A_BAR);

    /* files window */
    ui_box(1, 2, 38, 13, " Files (RAMFS) ");
    ui_text(3, 3, "Name", -1, A_DIM); ui_text(33, 3, "Size", -1, A_DIM);
    if (!count) ui_text(3, 5, "No files yet. Press N to add one.", 34, A_WIN);
    for (int r = 0; r < UI_ROWS; r++) {
        int i = top + r;
        if (i >= count) break;
        struct ramfs_entry *f = &ramfs[idx[i]];
        uint8_t at = (i == sel) ? A_SEL : A_WIN;
        ui_fill(2, 4 + r, 36, 1, ' ', at);
        ui_text(3, 4 + r, f->name, 15, at);
        b[0] = 0; cat_u(b, (uint32_t)f->size); str_cat(b, " B");
        ui_text(37 - (int)strlen(b), 4 + r, b, -1, at);
    }

    /* preview window */
    ui_box(1, 15, 38, 8, " Preview ");
    if (count) ui_wrap(3, 16, 34, 6, ramfs[idx[sel]].data, A_WIN, 0, 0);
    else ui_text(3, 16, "(nothing selected)", -1, A_DIM);

    /* system window */
    ui_box(40, 2, 39, 10, " System ");
    b[0] = 0; cat_u(b, secs / 60); str_cat(b, "m "); cat_u(b, secs % 60); str_cat(b, "s");
    ui_kv(3, "Uptime", b);
    b[0] = 0; cat_u(b, ticks); ui_kv(4, "Ticks", b);
    if (mbi && (mbi->flags & 1)) {
        b[0] = 0; cat_u(b, mbi->mem_upper / 1024); str_cat(b, " MB + "); cat_u(b, mbi->mem_lower); str_cat(b, " KB");
    } else { b[0] = 0; str_cat(b, "unknown"); }
    ui_kv(5, "RAM", b);
    b[0] = 0; cat_u(b, files); str_cat(b, "/"); cat_u(b, RAMFS_FILE_COUNT); str_cat(b, " files");
    ui_kv(6, "Files", b);
    b[0] = 0; cat_u(b, bytes); str_cat(b, "/"); cat_u(b, RAMFS_FILE_COUNT * (RAMFS_DATA_CAP - 1)); str_cat(b, " bytes");
    ui_kv(7, "Used", b);
    ui_kv(8, "Keymap", keymap_is_azerty ? "AZERTY" : "QWERTY");
    ui_kv(9, "CPU", vendor);

    /* keys window */
    ui_box(40, 12, 39, 11, " Keys ");
    ui_text(42, 13, "\x18 \x19        select a file", -1, A_WIN);
    ui_text(42, 14, "Enter / E   edit the file", -1, A_WIN);
    ui_text(42, 15, "N           new file", -1, A_WIN);
    ui_text(42, 16, "D           delete file", -1, A_WIN);
    ui_text(42, 17, "K           switch AZERTY/QWERTY", -1, A_WIN);
    ui_text(42, 18, "Q / Esc     back to the shell", -1, A_WIN);
    ui_text(42, 20, "In the editor:", -1, A_DIM);
    ui_text(42, 21, "Tab = save      Esc = cancel", -1, A_WIN);

    /* status bar */
    ui_fill(0, 24, VGA_W, 1, ' ', A_BAR);
    if (msg) ui_text(1, 24, msg, 78, A_ERR);
    else ui_text(1, 24, "Q Quit   N New   Enter Edit   D Delete   K Keymap", 78, A_BAR);
}

static int ui_input(const char *title, const char *prompt, char *buf, int max) {
    int n = 0;
    buf[0] = 0;
    for (;;) {
        ui_box(18, 9, 44, 6, title);
        ui_text(20, 11, prompt, 40, A_WIN);
        ui_fill(20, 12, 40, 1, ' ', A_SEL);
        ui_text(20, 12, buf, 38, A_SEL);
        ui_cell(20 + n, 12, '_', A_SEL);
        ui_text(20, 13, "Enter = OK    Esc = cancel", 40, A_DIM);
        int k = ui_getkey();
        if (!k) continue;
        if (k == 27) return 0;
        if (k == '\n') return n > 0;
        if (k == '\b') { if (n > 0) buf[--n] = 0; }
        else if (k >= 32 && k < 127 && n < max) { buf[n++] = (char)k; buf[n] = 0; }
    }
}

static int ui_confirm(const char *title, const char *line) {
    int k;
    ui_box(18, 9, 44, 6, title);
    ui_text(20, 11, line, 40, A_WIN);
    ui_text(20, 13, "Y = yes    any other key = no", 40, A_DIM);
    do { k = ui_getkey(); } while (!k);
    return k == 'y' || k == 'Y' || k == 'o' || k == 'O';
}

/* tiny editor: type to append, Backspace deletes, Enter = new line */
static int ui_edit(struct ramfs_entry *f) {
    char buf[RAMFS_DATA_CAP], title[32], cnt[16];
    size_t n = f->size;
    memcpy(buf, f->data, n);
    buf[n] = 0;
    title[0] = 0; str_cat(title, " Edit: "); str_cat(title, f->name); str_cat(title, " ");
    for (;;) {
        int ex, ey;
        ui_box(4, 3, 72, 18, title);
        ui_wrap(6, 5, 68, 12, buf, A_WIN, &ex, &ey);
        if (ex >= 68) { ex = 0; ey++; }
        if (ey < 12) ui_cell(6 + ex, 5 + ey, '_', A_BORD);
        ui_fill(5, 18, 70, 1, 0xC4, A_BORD);
        ui_text(6, 19, "Tab = save     Esc = cancel", -1, A_DIM);
        cnt[0] = 0; cat_u(cnt, (uint32_t)n); str_cat(cnt, "/"); cat_u(cnt, RAMFS_DATA_CAP - 1);
        ui_text(73 - (int)strlen(cnt), 19, cnt, -1, A_WIN);
        int k = ui_getkey();
        if (!k) continue;
        if (k == 27) return 0;
        if (k == '\t') { memcpy(f->data, buf, n + 1); f->size = n; return 1; }
        if (k == '\b') { if (n > 0) buf[--n] = 0; }
        else if ((k == '\n' || k >= 32) && k != 127 && n < RAMFS_DATA_CAP - 1) { buf[n++] = (char)k; buf[n] = 0; }
    }
}

static void ui_run(void) {
    int idx[RAMFS_FILE_COUNT];
    int sel = 0, top = 0, count;
    const char *msg = 0;
    outb(0x3D4, 0x0A); outb(0x3D5, 0x20);          /* hide the text cursor */
    for (;;) {
        count = ui_list(idx);
        if (sel >= count) sel = count ? count - 1 : 0;
        if (sel < top) top = sel;
        if (sel >= top + UI_ROWS) top = sel - UI_ROWS + 1;
        ui_draw(idx, count, sel, top, msg);
        msg = 0;
        int k = ui_getkey();
        if (!k) continue;
        if (k >= 'A' && k <= 'Z') k += 32;
        if (k == 'q' || k == 27) break;
        else if (k == KEY_UP) { if (sel > 0) sel--; }
        else if (k == KEY_DOWN) { if (sel < count - 1) sel++; }
        else if (k == 'k') {
            set_keymap(!keymap_is_azerty);
            msg = keymap_is_azerty ? "Keyboard layout: AZERTY" : "Keyboard layout: QWERTY";
        } else if (k == 'n') {
            char name[RAMFS_NAME_CAP];
            if (ui_input(" New file ", "File name:", name, RAMFS_NAME_CAP - 1)) {
                if (!ramfs_name_valid(name)) msg = "Invalid name (letters, digits, . - _ ; max 15)";
                else if (ramfs_find(name)) msg = "That file already exists";
                else {
                    struct ramfs_entry *f = ramfs_create(name);
                    if (!f) msg = "RAMFS is full (16 files)";
                    else {
                        count = ui_list(idx);
                        for (int i = 0; i < count; i++) if (&ramfs[idx[i]] == f) sel = i;
                        if (ui_edit(f)) msg = "File saved";
                    }
                }
            }
        } else if ((k == '\n' || k == 'e') && count) {
            if (ui_edit(&ramfs[idx[sel]])) msg = "File saved";
        } else if (k == 'd' && count) {
            struct ramfs_entry *f = &ramfs[idx[sel]];
            char q[40];
            q[0] = 0; str_cat(q, "Delete "); str_cat(q, f->name); str_cat(q, " ?");
            if (ui_confirm(" Delete ", q)) { memset(f, 0, sizeof(*f)); msg = "File deleted"; }
        }
    }
    outb(0x3D4, 0x0A); outb(0x3D5, 0x0E);          /* show the cursor again */
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0F);
    set_color(15, 0);
    clear_screen();
}

static void banner(void) {
    set_color(11, 0);
    puts("  __     ___ _          ___  ____  \n");
    puts("  \\ \\   / (_) |__   ___ / _ \\/ ___| \n");
    puts("   \\ \\ / /| | '_ \\ / _ \\ | | \\___ \\ \n");
    puts("    \\ V / | | |_) |  __/ |_| |___) |\n");
    puts("     \\_/  |_|_.__/ \\___|\\___/|____/ \n");
    set_color(15, 0);
    puts("\n  Welcome to VibeOS. Type 'help' or 'ui'.\n\n");
}

static void cmd_cpu(void) {
    char v[13];
    cpu_vendor(v);
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
             "  ui              open the text-mode desktop\n"
             "  ls              list RAM files\n"
             "  touch <name>    create an empty RAM file\n"
             "  write <name> <text>  create or replace file text\n"
             "  cat <name>      print a RAM file\n"
             "  rm <name>       delete a RAM file\n"
             "  df              show RAM filesystem usage\n"
             "  reboot | shutdown\n");
    } else if (!strcmp(line, "clear")) {
        clear_screen();
    } else if (!strcmp(line, "echo")) {
        puts(arg); putc('\n');
    } else if (!strcmp(line, "ls")) {
        ramfs_ls();
    } else if (!strcmp(line, "df")) {
        ramfs_df();
    } else if (!strcmp(line, "touch")) {
        if (!ramfs_name_valid(arg)) {
            puts("usage: touch <name> (letters, digits, dot, dash, underscore; max 15)\n");
        } else if (ramfs_find(arg)) {
            puts("File already exists: "); puts(arg); putc('\n');
        } else {
            struct ramfs_entry *file = ramfs_create(arg);
            if (!file) puts("RAMFS is full (16 files).\n");
            else { puts("Created empty file: "); puts(file->name); putc('\n'); }
        }
    } else if (!strcmp(line, "write")) {
        char *name = arg;
        char *body = arg;
        while (*body && *body != ' ') body++;
        if (!*body) {
            puts("usage: write <name> <text>\n");
        } else {
            *body++ = 0;
            while (*body == ' ') body++;
            size_t len = strlen(body);
            if (!ramfs_name_valid(name)) {
                puts("Invalid filename (max 15 letters, digits, dot, dash, underscore).\n");
            } else if (len >= RAMFS_DATA_CAP) {
                puts("Text is too long (maximum 191 bytes).\n");
            } else {
                struct ramfs_entry *file = ramfs_find(name);
                int created = 0;
                if (!file) { file = ramfs_create(name); created = 1; }
                if (!file) {
                    puts("RAMFS is full (16 files).\n");
                } else {
                    memcpy(file->data, body, len + 1);
                    file->size = len;
                    puts(created ? "Created and wrote " : "Updated ");
                    puts(file->name); putc('\n');
                }
            }
        }
    } else if (!strcmp(line, "cat")) {
        if (!ramfs_name_valid(arg)) {
            puts("usage: cat <name>\n");
        } else {
            struct ramfs_entry *file = ramfs_find(arg);
            if (!file) {
                puts("No such file: "); puts(arg); putc('\n');
            } else {
                puts(file->data); putc('\n');
            }
        }
    } else if (!strcmp(line, "rm")) {
        if (!ramfs_name_valid(arg)) {
            puts("usage: rm <name>\n");
        } else {
            struct ramfs_entry *file = ramfs_find(arg);
            if (!file) {
                puts("No such file: "); puts(arg); putc('\n');
            } else {
                puts("Deleted "); puts(file->name); putc('\n');
                memset(file, 0, sizeof(*file));
            }
        }
    } else if (!strcmp(line, "ui")) {
        ui_run();
    } else if (!strcmp(line, "about")) {
        puts("VibeOS 0.3 - a hobby kernel, vibe-coded.\n32-bit x86, VGA text, PS/2 keyboard, PIT timer, RAM files, text UI.\n");
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
    char line[256];
    for (;;) {
        set_color(10, 0); puts("vibe"); set_color(7, 0); puts("@os"); set_color(15, 0); puts("> ");
        readline(line, sizeof(line));
        run(line);
    }
}
