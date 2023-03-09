#include <windowmanager.h>
#include <util.h>
#include <pci.h>
#include <terminal.h>
#include <keyboard.h>
#include <interrupts.h>
#include <timer.h>
#include <screen.h>
#include <pcb.h>
#include <memory.h>
#include <net/skb.h>
#include <net/arp.h>
#include <ata.h>
#include <bitmap.h>
#include <net/socket.h>
#include <net/dns.h>
#include <fs/fs.h>
#include <serial.h>
#include <syscall_helper.h>
#include <syscalls.h>
#include <kthreads.h>
#include <scheduler.h>
#include <vbe.h>
#include <mouse.h>
#include <ipc.h>

#include <gfx/window.h>
#include <gfx/composition.h>
#include <gfx/api.h>

/* This functions always needs to be on top? */
void kernel(uint32_t magic) 
{
	CLI();
	vbe_info = (struct vbe_mode_info_structure*) magic;

	kernel_size = _end-_code;
	init_serial();
	dbgprintf("[VBE] INFO:\n");
	dbgprintf("[VBE] Height: %d\n", vbe_info->height);
	dbgprintf("[VBE] Width: %d\n", vbe_info->width);
	dbgprintf("[VBE] Pitch: %d\n", vbe_info->pitch);
	dbgprintf("[VBE] Bpp: %d\n", vbe_info->bpp);
	dbgprintf("[VBE] Framebuffer: 0x%x\n", vbe_info->framebuffer);
	dbgprintf("[VBE] Memory Size: %d (0x%x)\n", vbe_info->width*vbe_info->height*(vbe_info->bpp/8), vbe_info->width*vbe_info->height*(vbe_info->bpp/8));

	init_memory();
	init_interrupts();
	gfx_init();
	init_keyboard();
	mouse_init();
	pcb_init();
	ipc_msg_box_init();
	pci_init();

	init_sk_buffers();
	init_arp();
	init_sockets();
	init_dns();

	init_fs();
	
	register_kthread(&shell_main, "Shell");
	register_kthread(&networking_main, "Networking");
	register_kthread(&dhcpd, "dhcpd");
	register_kthread(&gfx_compositor_main, "wServer");
	register_kthread(&error_main, "Error");
	register_kthread(&gfx_window_debugger, "Debugger");
	
	start("Shell");
	start("wServer");

	#pragma GCC diagnostic ignored "-Wcast-function-type"
	add_system_call(SYSCALL_PRTPUT, (syscall_t)&terminal_putchar);
	add_system_call(SYSCALL_EXIT, (syscall_t)&exit);
	add_system_call(SYSCALL_SLEEP, (syscall_t)&sleep);
	add_system_call(SYSCALL_GFX_WINDOW, (syscall_t)&gfx_new_window);
	add_system_call(SYSCALL_GFX_GET_TIME,  (syscall_t)&get_current_time);
	add_system_call(SYSCALL_GFX_DRAW, (syscall_t)&gfx_syscall_hook);
	add_system_call(SYSCALL_GFX_SET_TITLE, (syscall_t)&__gfx_set_title);


	add_system_call(SYSCALL_FREE, (syscall_t)&free);
	add_system_call(SYSCALL_MALLOC, (syscall_t)&malloc);

	add_system_call(SYSCALL_OPEN, (syscall_t)&fs_open);
	add_system_call(SYSCALL_READ, (syscall_t)&fs_read);
	add_system_call(SYSCALL_WRITE, (syscall_t)&fs_write);
	#pragma GCC diagnostic pop
	

	dbgprintf("[KERNEL] TEXT: %d\n", _code_end-_code);
	dbgprintf("[KERNEL] RODATA: %d\n", _ro_e-_ro_s);
	dbgprintf("[KERNEL] DATA: %d\n", _data_e-_data_s);
	dbgprintf("[KERNEL] BSS: %d\n", _bss_e-_bss_s);
	dbgprintf("[KERNEL] Total: %d (%d sectors)\n", _end-_code, ((_end-_code)/512)+2);
	dbgprintf("[KERNEL] Kernel reaching too: 0x%x\n", _end-_code);

	load_page_directory(kernel_page_dir);
	enable_paging();

	dbgprintf("[KERNEL] Enabled paging!\n");
	
	vesa_init();

	STI();
	init_timer(1);

	dbgprintf("[CLI] %d\n", cli_cnt);

	pcb_start();

	while(1){};

}
