CC      = gcc
AS      = nasm
LD      = ld
CFLAGS  = -m32 -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector \
          -fno-pie -fno-pic -fno-builtin -nostdlib -mno-sse -mno-mmx
LDFLAGS = -m elf_i386 -T linker.ld -z noexecstack -nostdlib

all: vibeos.elf

boot.o: boot.asm
	$(AS) -f elf32 $< -o $@

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

vibeos.elf: boot.o kernel.o linker.ld
	$(LD) $(LDFLAGS) boot.o kernel.o -o $@

# Opens a QEMU window
run: vibeos.elf
	qemu-system-i386 -kernel vibeos.elf

# No window: the shell shows up in your terminal (Ctrl+A then X to quit)
run-serial: vibeos.elf
	qemu-system-i386 -kernel vibeos.elf -display none -serial mon:stdio

clean:
	rm -f *.o *.elf
