CCFLAGS=-m32 -std=gnu99 -O2 -g -Wall -Wextra -Wpedantic -Wstrict-aliasing -Wno-pointer-arith -Wno-unused-parameter -nostdlib -nostdinc -ffreestanding -fno-pie -fno-stack-protector -fno-builtin-function -fno-builtin -I ./includes/
ASFLAGS=
LDFLAGS=


UNAME := $(shell uname)
ifeq ($(UNAME),Linux)
	CC=gcc
	AS=as
	LD=ld

	CCFLAGS += -elf_i386
	ASFLAGS += --32
	LDFLAGS += -m elf_i386
else
	CC=i386-elf-gcc
	AS=i386-elf-as
	LD=i386-elf-ld
endif

KERNELOBJ = entry.o kernel.o terminal.o pci.o util.o interrupts.o irs_entry.o
BOOTOBJ = bootloader.o

.PHONY: all new image clean
all: new

new: clean image

bootblock: $(BOOTOBJ)
	$(LD) $(LDFLAGS) -o ./bin/bootblock ./bin/$^ -Ttext 0x7C00 --oformat=binary

kernel: $(KERNELOBJ)
	$(LD) -o ./bin/kernel $(addprefix ./bin/,$^) $(LDFLAGS) -T ./kernel/linker.ld

image: bootblock kernel
	dd if=/dev/zero of=boot.iso bs=512 count=961
	dd if=./bin/bootblock of=boot.iso conv=notrunc bs=512 seek=0 count=1
	dd if=./bin/kernel of=boot.iso conv=notrunc bs=512 seek=1 count=960

clean:
	rm -f ./bin/*.o
	rm -f ./bin/bootblock
	rm -f ./bin/kernel
	rm -f *.iso

cleanvid:
	rm *.vdi

vdi:
	qemu-img convert -f raw -O vdi boot.iso boot.vdi

run:
	qemu-system-i386 boot.iso

# For assembling and compiling all .c and .s files.
%.o: */%.c
	$(CC) -o bin/$@ -c $< $(CCFLAGS)

%.o: */%.s
	$(AS) -o bin/$@ -c $< $(ASFLAGS)