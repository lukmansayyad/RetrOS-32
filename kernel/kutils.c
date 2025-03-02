/**
 * @file kutils.c
 * @author Joe Bayer (joexbayer)
 * @brief Kernel utilities.
 * @version 0.1
 * @date 2024-01-10
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include <libc.h>
#include <memory.h>
#include <serial.h>
#include <ksyms.h>
#include <terminal.h>
#include <gfx/gfxlib.h>
#include <vbe.h>
#include <pcb.h>
#include <arch/io.h>
#include <script.h>
#include <kutils.h>

static char *units[] = {"b ", "kb", "mb"};

void system_reboot()
{
    ENTER_CRITICAL();
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inportb(0x64);
    outportb(0x64, 0xFE);
    HLT();
}

/* Function to align a given size to the size of a void* */
int align_to_pointer_size(int size)
{
    /* Calculate the alignment requirement */
    int alignment = sizeof(void*);

    /* Align the size */
    int aligned_size = (size + alignment - 1) & ~(alignment - 1);

    return aligned_size;
}

int exec_cmd(char* str)
{
    struct args args = {
        .argc = 0
    };

    for (int i = 0; i < 10; i++){
        args.argv[i] = args.data[i];
    }
    
	args.argc = parse_arguments(str, args.data);
	if(args.argc == 0) return -1;

    for (int i = 0; i < args.argc; i++){
        dbgprintf("%d: %s\n", args.argc, args.argv[i]);
    }

	void (*ptr)(int argc, char* argv[]) = (void (*)(int argc, char* argv[])) ksyms_resolve_symbol(args.argv[0]);
	if(ptr == NULL){
		return -1;
	}

    dbgprintf("Executing %s\n", args.argv[0]);
    /* execute command */
	ptr(args.argc, args.argv);
    dbgprintf("Done executing %s\n", args.argv[0]);

	gfx_commit();

	return 0;
}
EXPORT_KSYMBOL(exec_cmd);

/**
 * @brief Converts a amount of bytes to a human readable format 
 * 
 * @param bytes amount of bytes
 * @return struct unit 
 */
struct unit calculate_size_unit(int bytes)
{
    int index = 0;
    double size = bytes;

    while (size >= 1024 && index < 2) {
        size /= 1024;
        index++;
    }

    struct unit unit = {
        .size = size,
        .unit = units[index]
    };

    return unit;
}

unsigned int advanced_hash(char *input)
{
    unsigned int hash = 0;
    int c;

    /* Loop through each character in the password */
    while ((c = *input++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

void kernel_panic(const char* reason)
{
    ENTER_CRITICAL();
    
    //backtrace();

    const char* message = "KERNEL PANIC";
    int message_len = strlen(message);
   
    PANIC();
    for (int i = 0; i < message_len; i++){
        vesa_put_char16((uint8_t*)vbe_info->framebuffer, message[i], 16+(i*16), vbe_info->height/3 - 24, 15);
    }
    
    struct pcb* pcb = $process->current;
    vesa_printf((uint8_t*)vbe_info->framebuffer, 16, vbe_info->height/3, 15, "A critical error has occurred and your system is unable to continue operating.\nThe cause of this failure appears to be an essential system component.\n\nReason:\n%s\n\n###### PCB ######\npid: %d\nname: %s\nesp: 0x%x\nebp: 0x%x\nkesp: 0x%x\nkebp: 0x%x\neip: 0x%x\nstate: %s\nstack limit: 0x%x\nstack size: 0x%x (0x%x - 0x%x)\nPage Directory: 0x%x\nCS: %d\nDS:%d\n\n\nPlease power off and restart your device.\nRestarting may resolve the issue if it was caused by a temporary problem.\nIf this screen appears again after rebooting, it indicates a more serious issue.",
    reason, pcb->pid, pcb->name, pcb->ctx.esp, pcb->ctx.ebp, pcb->kesp, pcb->kebp, pcb->ctx.eip, pcb_status[pcb->state], pcb->stackptr, (int)((pcb->stackptr+0x2000-1) - pcb->ctx.esp), (pcb->stackptr+0x2000-1), pcb->ctx.esp,  pcb->page_dir, pcb->cs, pcb->ds);

    PANIC();
}

int kref_init(struct kref* ref)
{
    ref->refs = 0;
    ref->spinlock = 0;

    return 0;
}

int kref_get(struct kref* ref)
{
    spin_lock(&ref->spinlock);

    ref->refs++;

    spin_unlock(&ref->spinlock);

    return ref->refs;
}

int kref_put(struct kref* ref)
{
    spin_lock(&ref->spinlock);

    ref->refs--;

    spin_unlock(&ref->spinlock);

    return ref->refs;
}

#define MAX_FMT_STR_SIZE 256

/* Custom sprintf function */
int32_t csprintf(char *buffer, const char *fmt, va_list args)
{
    int written = 0; /* Number of characters written */
    char str[MAX_FMT_STR_SIZE];
    int num = 0;

    while (*fmt != '\0' && written < MAX_FMT_STR_SIZE) {
        if (*fmt == '%') {
            memset(str, 0, MAX_FMT_STR_SIZE); /* Clear the buffer */
            fmt++; /* Move to the format specifier */

            if (written < MAX_FMT_STR_SIZE - 1) {
                switch (*fmt) {
                    case 'd':
                    case 'i':
                        num = va_arg(args, int);
                        itoa(num, str);
                        break;
                    case 'x':
                    case 'X':
                        num = va_arg(args, unsigned int);
                        written += itohex(num, str);
                        break;
                    case 'p': /* p for padded int */
                        num = va_arg(args, int);
                        itoa(num, str);

                        if (strlen(str) < 5) {
                            int pad = 5 - strlen(str);
                            for (int i = 0; i < pad; i++) {
                                buffer[written++] = '0';
                            }
                        }
                        break;
                    case 's':{
                            char *str_arg = va_arg(args, char*);
                            while (*str_arg != '\0' && written < MAX_FMT_STR_SIZE - 1) {
                                buffer[written++] = *str_arg++;
                            }
                        }
                        break;
                    case 'c':
                        if (written < MAX_FMT_STR_SIZE - 1) {
                            buffer[written++] = (char)va_arg(args, int);
                        }
                        break;
                    /* Add additional format specifiers as needed */
                }

                /* Copy formatted string to buffer */
                for (int i = 0; str[i] != '\0'; i++) {
                    buffer[written++] = str[i];
                }
            }
        } else {
            /* Directly copy characters that are not format specifiers */
            if (written < MAX_FMT_STR_SIZE - 1) {
                buffer[written++] = *fmt;
            }
        }
        fmt++;
    }

    /* Ensure the buffer is null-terminated */
    buffer[written < MAX_FMT_STR_SIZE ? written : MAX_FMT_STR_SIZE - 1] = '\0';

    return written;
}

int script_parse(char* str)
{
    char* start = str;
    int line = 0, ret;

    if(*str == 0){
        return -1;
    }

    /* This assumes that the given string is \0 terminated. */
    do {
        if(*str == '\n'){
            *str = 0;
            
            ret = exec_cmd(start);
            if(ret < 0){
                twritef("script: error on '%s' line %d\n", start, line);
                return -1;
            }
        
            line++;
            start = str+1;
        }
        str++;
    } while (*str != 0);
    
    /* Try to execute the last line incase it ended with a \0 */
    ret = exec_cmd(start);
    if(ret < 0){
        twritef("script: error on '%s' line %d\n", start, line);
        return -1;
    }
        

    return 0;
}