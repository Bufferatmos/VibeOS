# VibeOS

A tiny 32-bit x86 hobby OS written in C and NASM. It boots in QEMU straight to a shell.

## Current features

- Multiboot entry, GDT, IDT, PIC, and PIT timer
- VGA text console with serial output
- PS/2 keyboard with AZERTY and QWERTY maps
- Shell calculator and system information commands
- A small RAM filesystem for creating, listing, reading, and deleting files

## Build and run

On Ubuntu or WSL, install the toolchain:

```sh
sudo apt update
sudo apt install -y build-essential nasm gcc-multilib qemu-system-x86 make
```

From the repository directory:

```sh
make              # build vibeos.elf
make run          # open the QEMU window
make run-serial   # show the shell in the terminal
```

Close the serial QEMU session with `Ctrl+A`, then `X`.

## Shell commands

Run `help` in VibeOS for the full list.

- `help`, `clear`, `echo <text>`
- `about`, `uptime`, `mem`, `cpu`
- `calc <a> <op> <b>` with `+`, `-`, `*`, `/`, or `%`
- `color <0-15>`, `keymap [azerty|qwerty]`, `rainbow`
- `ls`, `touch <name>`, `write <name> <text>`, `cat <name>`, `rm <name>`, `df`
- `reboot`, `shutdown`

## RAM filesystem

The filesystem is intentionally small and volatile: it holds up to 16 files, each with up to 191 bytes of text. Names can be up to 15 characters and use letters, digits, dots, dashes, or underscores. Contents disappear when VibeOS reboots.

Example:

```text
vibe@os> touch todo
vibe@os> write todo learn paging
vibe@os> ls
todo  12 bytes
vibe@os> cat todo
learn paging
vibe@os> rm todo
```

## Source files

- `boot.asm` — Multiboot header, GDT/IDT loaders, and interrupt stubs
- `kernel.c` — console, interrupts, timer, keyboard, RAM filesystem, and shell
- `linker.ld` — kernel memory layout
- `Makefile` — build and QEMU run targets

## Next milestones

Paging and a physical memory allocator, followed by persistent storage and user programs.
