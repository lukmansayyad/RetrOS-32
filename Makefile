CCFLAGS=-m32 -std=gnu11 -O1 \
		-g -Wall -Wextra -Wpedantic -Wstrict-aliasing \
		-Wno-pointer-arith -Wno-unused-parameter -nostdlib \
		-nostdinc -ffreestanding -fno-pie -fno-stack-protector \
		-I ./include/
ASFLAGS=
LDFLAGS= 
MAKEFLAGS += --no-print-directory

# ---------------- For counting how many files to compile ----------------
ifneq ($(words $(MAKECMDGOALS)),1)
.DEFAULT_GOAL = all
%:
	@$(MAKE) $@ --no-print-directory -rRf $(firstword $(MAKEFILE_LIST))
else
ifndef ECHO
T := $(shell $(MAKE) $(MAKECMDGOALS) --no-print-directory \
    -nrRf $(firstword $(MAKEFILE_LIST)) \
    ECHO="COUNTTHIS" | grep -c "COUNTTHIS")

N := x
C = $(words $N)$(eval N := x $N)
ECHO = echo -ne "\r[$(words $N)/$T] (`expr $C '*' 100 / $T`%)"
endif

# ---------------- For timing makefile ----------------
TIME_START := $(shell date +%s)
define TIME-END
@time_end=`date +%s` ; time_exec=`awk -v "TS=${TIME_START}" -v "TE=$$time_end" 'BEGIN{TD=TE-TS;printf "%02dd:%02dh:%02dm:%02ds\n",TD/(60*60*24),TD/(60*60)%24,TD/(60)%60,TD%60}'` ; echo "Build time: $${time_exec} for $@"
endef

# ---------------- For cross compilation (MacOS) ----------------
UNAME := $(shell uname)
ifeq ($(UNAME),Linux)
	CC=gcc
	AS=as
	LD=ld

	CCFLAGS += -elf_i386
	ASFLAGS += --32
	LDFLAGS += -m elf_i386
	GRUB=grub-mkrescue /usr/lib/grub/i386-pc/ -o myos.iso legacy/multiboot
else
	GRUB=grub-mkrescue /usr/local/lib/grub/i386-pc/ -o myos.iso legacy/multiboot
	CC=i386-elf-gcc
	AS=i386-elf-as
	LD=i386-elf-ld

endif


# ---------------- Objects to compile ----------------
PROGRAMOBJ = bin/shell.o bin/networking.o bin/dhcpd.o bin/error.o bin/tcpd.o bin/image.o

GFXOBJ = bin/window.o bin/component.o bin/composition.o bin/gfxlib.o bin/api.o bin/theme.o

KERNELOBJ = bin/kernel.o bin/terminal.o bin/helpers.o bin/pci.o bin/virtualdisk.o \
			bin/util.o bin/interrupts.o bin/irs_entry.o bin/timer.o bin/gdt.o bin/interpreter.o bin/vm.o bin/lex.o \
			bin/keyboard.o bin/pcb.o bin/memory.o bin/vmem.o bin/kmem.o bin/e1000.o bin/display.o \
			bin/sync.o bin/kthreads.o bin/ata.o bin/bitmap.o bin/rtc.o bin/tss.o \
			bin/diskdev.o bin/scheduler.o bin/work.o bin/rbuffer.o bin/errors.o bin/kclock.o \
			bin/serial.o bin/io.o bin/syscalls.o bin/list.o bin/hashmap.o bin/vbe.o bin/ksyms.o\
			bin/mouse.o bin/ipc.o ${PROGRAMOBJ} ${GFXOBJ} bin/font8.o bin/net.o bin/fs.o

BOOTOBJ = bin/bootloader.o

LIBOBJ = bin/printf.o bin/syscall.o bin/graphics.o

# ---------------- Makefile rules ----------------

.PHONY: all new image clean boot net kernel grub time tests build apps
all: iso
	$(TIME-END)

ls:
	find -name '*.[c|h]' | xargs wc -l

bootblock: $(BOOTOBJ)
	@$(LD) $(LDFLAGS) -o bin/bootblock $^ -Ttext 0x7C00 --oformat=binary

multiboot_kernel: bin/multiboot.o $(KERNELOBJ)
	@$(LD) -o bin/kernelout $^ $(LDFLAGS) -T ./boot/multiboot.ld

kernel: bin/kcrt0.o $(KERNELOBJ)
	@$(LD) -o bin/kernelout $^ $(LDFLAGS) -T ./kernel/linker.ld

.depend: **/*.[cSh]
	@$(CC) $(CCFLAGS) -MM -MG **/*.[cS] > $@
	@echo [KERNEL] Creating dependencies
	
-include .depend

# For assembling and compiling all .c and .s files.
bin/%.o: */%.c
	@$(ECHO) [KERNEL]     Compiling $<
	$(CC) -o $@ -c $< $(CCFLAGS)

bin/%.o: */*/%.c
	@$(ECHO) [PROGRAM]    Compiling $<
	@$(CC) -o $@ -c $< $(CCFLAGS)

bin/%.o: */%.s
	@$(ECHO) [KERNEL]     Compiling $<
	@$(AS) -o $@ -c $< $(ASFLAGS)

bin/build: ./tools/build.c
	@gcc ./tools/build.c -o ./bin/build
	@echo [BUILD]      Compiling $<

bin/mkfs: bin/fs.o bin/bitmap.o ./tools/mkfs.c
	@gcc tools/mkfs.c bin/bitmap.o fs/bin/inode.o -I include/  -O2 -m32 -Wall -g --no-builtin -o ./bin/mkfs
	@echo [BUILD]      Compiling $<

tools: bin/build bin/mkfs

tests: compile
	@make -C ./tests/

bin/net.o: ./net/*.c
	@make -C ./net/

bin/fs.o: ./fs/*.c
	@make -C ./fs/

build: tools
	@echo [BUILD]      Building ISO file and attaching filesystem.
	@rm -f boot.iso
	@./bin/mkfs
	@./bin/build

apps:
	@make -C ./apps/
	xxd -i apps/editor/edit.o > include/editor.h

iso: compile tests apps tools build
	$(TIME-END)

filesystem:
	@dd if=/dev/zero of=filesystem.image bs=512 count=390

compile: bindir $(LIBOBJ) bootblock kernel apps
	@echo "[Compile] Finished."
	$(TIME-END)

img: iso
	mv boot.iso boot.img

clean:
	make -C ./net clean
	make -C ./fs clean
	make -C ./apps clean
	make -C ./tests clean
	rm -f ./bin/*.o
	rm -f ./bin/bootblock
	rm -f ./bin/kernelout
	rm -f .depend
	rm -f filesystem.image
	rm -f filesystem.test

test: clean compile tests

bindir:
	@mkdir -p bin

grub: apps multiboot_kernel
	cp bin/kernelout legacy/multiboot/boot/myos.bin
	$(GRUB)


docker-rebuild:
	docker-compose build --no-cache

docker:
	sudo docker-compose up

vdi: cleanvid docker
	qemu-img convert -f raw -O vdi boot.iso boot.vdi

qemu:
	sudo qemu-system-i386 -device e1000,netdev=net0 -serial stdio -netdev user,id=net0 -object filter-dump,id=net0,netdev=net0,file=dump.dat boot.iso

run: docker qemu
endif

# qemu-system-i386 -cdrom myos.iso -serial stdio -drive file=boot.iso,if=ide,index=0,media=disk
#qemu-system-i386.exe -cdrom .\myos.iso -drive if=none,id=usbstick,format=raw,file=filesystem.image -usb -device usb-ehci,id=ehci -device usb-storage,bus=ehci.0,drive=usbstick