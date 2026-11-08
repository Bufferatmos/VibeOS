# VibeOS

A tiny 32-bit x86 OS written in C + NASM. Boots in QEMU straight to a shell.

## Setup (Windows)
1. Open PowerShell as admin: `wsl --install` (reboot if asked)
2. In the Ubuntu terminal:
   ```
   sudo apt update && sudo apt install -y build-essential nasm gcc-multilib qemu-system-x86 make
   ```
3. Go to this folder and run:
   ```
   make run          # opens a QEMU window
   make run-serial   # shell in the terminal instead (Ctrl+A then X to quit)
   ```

## Files
- `boot.asm`  multiboot header, GDT/IDT loaders, interrupt stubs
- `kernel.c`  console, interrupts, timer, keyboard (AZERTY default), shell
- `linker.ld` memory layout
- `Makefile`

Shell commands: `help clear echo about uptime mem cpu calc color keymap rainbow reboot shutdown`
