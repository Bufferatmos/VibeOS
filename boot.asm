; boot.asm - entry point, GDT/IDT loaders and interrupt stubs
MBALIGN  equ 1 << 0
MEMINFO  equ 1 << 1
FLAGS    equ MBALIGN | MEMINFO
MAGIC    equ 0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .bss
align 16
stack_bottom: resb 16384
stack_top:

section .text
global _start
extern kmain
extern irq_handler
extern exc_handler

_start:
    cli
    mov esp, stack_top
    push ebx            ; multiboot info pointer
    push eax            ; multiboot magic
    call kmain
.hang:
    cli
    hlt
    jmp .hang

global gdt_flush
gdt_flush:
    mov eax, [esp+4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret

global idt_load
idt_load:
    mov eax, [esp+4]
    lidt [eax]
    ret

; ---- CPU exceptions (0-31): print panic and halt ----
%macro ISR 1
global isr%1
isr%1:
    push dword %1
    jmp exc_common
%endmacro

exc_common:
    cld
    mov eax, [esp]
    push eax
    call exc_handler
.h: cli
    hlt
    jmp .h

ISR 0
ISR 1
ISR 2
ISR 3
ISR 4
ISR 5
ISR 6
ISR 7
ISR 8
ISR 9
ISR 10
ISR 11
ISR 12
ISR 13
ISR 14
ISR 15
ISR 16
ISR 17
ISR 18
ISR 19
ISR 20
ISR 21
ISR 22
ISR 23
ISR 24
ISR 25
ISR 26
ISR 27
ISR 28
ISR 29
ISR 30
ISR 31

; ---- hardware IRQs (remapped to 32-47) ----
%macro IRQ 2
global irq%1
irq%1:
    push dword %2
    jmp irq_common
%endmacro

irq_common:
    pushad
    cld
    mov eax, [esp+32]
    push eax
    call irq_handler
    add esp, 4
    popad
    add esp, 4
    iretd

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

section .data
global isr_table
isr_table:
    dd isr0
    dd isr1
    dd isr2
    dd isr3
    dd isr4
    dd isr5
    dd isr6
    dd isr7
    dd isr8
    dd isr9
    dd isr10
    dd isr11
    dd isr12
    dd isr13
    dd isr14
    dd isr15
    dd isr16
    dd isr17
    dd isr18
    dd isr19
    dd isr20
    dd isr21
    dd isr22
    dd isr23
    dd isr24
    dd isr25
    dd isr26
    dd isr27
    dd isr28
    dd isr29
    dd isr30
    dd isr31
global irq_table
irq_table:
    dd irq0
    dd irq1
    dd irq2
    dd irq3
    dd irq4
    dd irq5
    dd irq6
    dd irq7
    dd irq8
    dd irq9
    dd irq10
    dd irq11
    dd irq12
    dd irq13
    dd irq14
    dd irq15

section .note.GNU-stack noalloc noexec nowrite progbits
