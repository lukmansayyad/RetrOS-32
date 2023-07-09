/**
 * @file composition.c
 * @author Joe Bayer (joexbayer)
 * @brief Window Server composition and window manage api.
 * @version 0.1
 * @date 2023-01-24
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#include <vbe.h>
#include <gfx/window.h>
#include <gfx/gfxlib.h>
#include <gfx/events.h>
#include <gfx/theme.h>
#include <keyboard.h>
#include <scheduler.h>
#include <util.h>
#include <memory.h>
#include <serial.h>
#include <rtc.h>
#include <timer.h>
#include <colors.h>
#include <mouse.h>
#include <gfx/component.h>
#include <sync.h>
#include <assert.h>
#include <kthreads.h>
#include <math.h>
#include <net/net.h>
#include <arch/interrupts.h>

#include <fs/ext.h>

#include <diskdev.h>
#include <net/netdev.h>

static struct window_server {
    uint8_t sleep_time;
    uint8_t* composition_buffer;
    struct window* order;
    mutex_t order_lock;
} wind = {
    .sleep_time = 2
};

/* Mouse globals */
static char gfx_mouse_state = 0;
static struct mouse m;

int gfx_check_changes(struct window* w)
{
    while(w != NULL){
        if(w->changed)
            return 1;
        w = w->next;
    }
    
    return 0;
}

void gfx_recursive_draw(struct window* w)
{
    if(w->next != NULL)
        gfx_recursive_draw(w->next);
    
    gfx_draw_window(wind.composition_buffer, w);
}

/**
 * @brief Push a window to the front of "wind.order" list.
 * Changing its z-axis position in the framebuffer.
 * 
 * Assuming w is NOT already the first element.
 * @param w 
 */
static void gfx_window_push_front(struct window* w)
{
    acquire(&wind.order_lock);

    assert(w != wind.order);

    /* remove w from wind.order list */
    for (struct window* i = wind.order; i != NULL; i = i->next){
        if(i->next == w){
            i->next = w->next;
            break;
        }
    }
    
    /* Replace wind.order with w, pushing old wind.order back. */
    wind.order->in_focus = 0;
    struct window* save = wind.order;
    wind.order = w;
    w->next = save;
    wind.order->in_focus = 1;

    w->changed = 1;

    release(&wind.order_lock);
}

/**
 * @brief Removes a window from the "wind.order" list.
 * Important keep the wind.order list intact even if removing the first element.
 * @param w 
 */
void gfx_composition_remove_window(struct window* w)
{
    acquire(&wind.order_lock);

    if(wind.order == w)
    {
        wind.order = w->next;
        if(wind.order != NULL){
            wind.order->changed = 1;
            wind.order->in_focus = 1;
        }
        goto gfx_composition_remove_window_exit;
    }

    struct window* iter = wind.order;
    while(iter != NULL && iter->next != w)
        iter = iter->next;
    if(iter == NULL){
        goto gfx_composition_remove_window_exit;
    }

    iter->next = w->next;
    
gfx_composition_remove_window_exit:

    dbgprintf("[GFX] Removing window\n");
    release(&wind.order_lock);
}

/**
 * @brief Adds new window in the "wind.order" list.
 * 
 * @param w 
 */
void gfx_composition_add_window(struct window* w)
{
    acquire(&wind.order_lock);

    if(wind.order == NULL){
        wind.order = w;
        wind.order->in_focus = 1;
        release(&wind.order_lock);
        return;
    }
    
    struct window* iter = wind.order;
    wind.order->in_focus = 0;
    wind.order = w;

    wind.order->in_focus = 1;
    wind.order->next = iter;

    release(&wind.order_lock);
}

/**
 * @brief raw mouse event handler for window API.
 * 
 * @param x 
 * @param y 
 * @param flags 
 */
void gfx_mouse_event(int x, int y, char flags)
{
    for (struct window* i = wind.order; i != NULL; i = i->next)
        if(gfx_point_in_rectangle(i->x, i->y, i->x+i->width, i->y+i->height, x, y)){
            /* on click when left mouse down */
            if(flags & 1 && gfx_mouse_state == 0){
                gfx_mouse_state = 1;
                i->ops->mousedown(i, x, y);

                /* If clicked window is not in front, push it. */
                if(i != wind.order){
                    gfx_window_push_front(i);
                }

                /* TODO: push mouse gfx event to window */

            } else if(!(flags & 1) && gfx_mouse_state == 1) {
                /* If mouse state is "down" send click event */
                gfx_mouse_state = 0;
                i->ops->click(i, x, y);
                i->ops->mouseup(i, x, y);

                uint16_t new_x = CLAMP( (x - (i->x+8)), 0,  i->inner_width);
                uint16_t new_y = CLAMP( (y - (i->y+8)), 0,  i->inner_height);

                struct gfx_event e = {
                    .data = new_x,
                    .data2 = new_y,
                    .event = GFX_EVENT_MOUSE
                };
                gfx_push_event(wind.order, &e);
            }

            i->ops->hover(i, x, y);
            return;
        }
    
    /* No window was clicked. */
}
static int __is_fullscreen = 0;
static void* inner_window_save;
void gfx_set_fullscreen(struct window* w)
{
    if(w != wind.order){
        dbgprintf("Cannot fullscreen window that isnt in focus\n");
        return;
    }

    /* store and backup original window information */
    w->inner_width = vbe_info->width;
    w->inner_height = vbe_info->height;

    inner_window_save = w->inner;
    w->inner = wind.composition_buffer;
    w->pitch = vbe_info->width;
    w->y = 0;
    w->x = 0;

    dbgprintf("%s is now in fullscreen\n", w->name);

    __is_fullscreen = 1;
}

void gfx_unset_fullscreen(struct window* w)
{
    if(w != wind.order){
        dbgprintf("Cannot fullscreen window that isnt in focus\n");
        return;
    }

    /* store and backup original window information */
    w->inner_width = w->width-16;
    w->inner_height = w->height-16;

    w->inner = inner_window_save;
    w->pitch = w->inner_width;
    w->y = 10;
    w->x = 10;

    dbgprintf("%s is now not in fullscreen\n", w->name);

    __is_fullscreen = 0;
}

int gfx_decode_background_image(char* background, int width, int height){

    char* temp = (char*) kalloc(5000);
    inode_t inode = ext_open("bg.bin", 0);
    if(inode == 0){
        dbgprintf("[WSERVER] Could not open background file.\n");
        return -1;
    }

    int ret = ext_read(inode, temp, 5000);
    if(ret == 0){
        dbgprintf("[WSERVER] Could not read background file.\n");
        return -1;
    }
    ext_close(inode);

    int out;
    run_length_decode(temp, ret, wind.composition_buffer, &out);
    kfree(temp);

    int originalWidth = 320;   // Original image width
    int originalHeight = 240;  // Original image height
    int targetWidth = vbe_info->width;     // Target screen width
    int targetHeight = vbe_info->height;    // Target screen height

    /* upscale image */
    float scaleX = (float)targetWidth / originalWidth;
    float scaleY = (float)targetHeight / originalHeight;

    for (int i = 0; i < originalWidth; i++){
        for (int j = 0; j < originalHeight; j++){
            // Calculate the position on the screen based on the scaling factors
            int screenX = (int)(i * scaleX);
            int screenY = (int)(j * scaleY);

            // Retrieve the pixel value from the original image
            unsigned char pixelValue = wind.composition_buffer[j * originalWidth + i];

            // Set the pixel value on the screen at the calculated position
            for (int x = 0; x < scaleX; x++){
                for (int y = 0; y < scaleY; y++){
                    vesa_put_pixel(background, screenX + x, screenY + y, pixelValue);
                }
            }
        }
    }


    return ERROR_OK;
}

static char* background;
#define TIME_PREFIX(unit) unit < 10 ? "0" : ""
void gfx_compositor_main()
{
    int buffer_size = vbe_info->width*vbe_info->height*(vbe_info->bpp/8)+1;

    dbgprintf("[WSERVER] %d bytes allocated for composition buffer.\n", buffer_size);
    wind.composition_buffer = (uint8_t*) palloc(buffer_size);
    background = (uint8_t*) kalloc(buffer_size);


    gfx_decode_background_image(background, vbe_info->width, vbe_info->height);

    //create_checkerboard(background, vbe_info->width, vbe_info->height, 50, COLOR_VGA_GREEN, COLOR_VGA_MISC);
    //create_circle_pattern(background, vbe_info->width, vbe_info->height, vbe_info->width/2, vbe_info->height/2, 100, COLOR_VGA_MISC);

    /* Main composition loop */
    while(1){
        struct gfx_theme* theme = kernel_gfx_current_theme();
        
        /**
         * Problem with interrupts from mouse?
         * Manages to corrupt some register or variables.
         * 
         * Should also NOT redraw entire screen, only things that change.
         */
        //ENTER_CRITICAL();
        int test = rdtsc();
        int mouse_changed = mouse_get_event(&m);
        int window_changed = gfx_check_changes(wind.order);
        struct time time;
        get_current_time(&time);
        
        
        unsigned char key = kb_get_char();
        if(key != 0){

            if(key == F10){
                if(!__is_fullscreen) {
                    gfx_set_fullscreen(wind.order);
                } else {
                    gfx_unset_fullscreen(wind.order);
                }
                struct gfx_event e = {
                    .data = wind.order->inner_width,
                    .data2 = wind.order->inner_height,
                    .event = GFX_EVENT_RESOLUTION
                };
                gfx_push_event(wind.order, &e);
            } if (key == F5) {
                start("shell");
            } else {
                struct gfx_event e = {
                    .data = key,
                    .event = GFX_EVENT_KEYBOARD
                };
                gfx_push_event(wind.order, &e);
            }
        }

        if(window_changed && !__is_fullscreen){
            
            //memset(wind.composition_buffer, theme->os.background/*41*/, buffer_size);
            memcpy(wind.composition_buffer, background, buffer_size);

            vesa_fillrect(wind.composition_buffer, 0, 0, vbe_info->width, 16, theme->window.background);
            /* black line under rect */
            vesa_fillrect(wind.composition_buffer, 0, 16, vbe_info->width, 1, 0);

            vesa_printf(wind.composition_buffer, 4, 4, 0, "%s", "HOME");
            /* open text */
            vesa_printf(wind.composition_buffer, 40, 4, 0, "%s", "Open");
        }
        
        vesa_fillrect(wind.composition_buffer, vbe_info->width-strlen("00:00:00 00/00/00")*8 - 16, 4, strlen("00:00:00 00/00/00")*8, 8, theme->window.background);
        vesa_printf(wind.composition_buffer, vbe_info->width-strlen("00:00:00 00/00/00")*8 - 16, 4 ,  0, "%s%d:%s%d:%s%d %s%d/%s%d/%d", TIME_PREFIX(time.hour), time.hour, TIME_PREFIX(time.minute), time.minute, TIME_PREFIX(time.second), time.second, TIME_PREFIX(time.day), time.day, TIME_PREFIX(time.month), time.month, time.year);

 

        //LEAVE_CRITICAL();
        if(__is_fullscreen){
        } else {
            gfx_recursive_draw(wind.order);
        }


        kernel_yield();

        //if(mouse_changed || window_changed)
        ENTER_CRITICAL();
        memcpy((uint8_t*)vbe_info->framebuffer, wind.composition_buffer, buffer_size-1);
        LEAVE_CRITICAL();
        /* Copy buffer over to framebuffer. */

        if(mouse_changed){
            gfx_mouse_event(m.x, m.y, m.flags);
        }
        vesa_put_icon16((uint8_t*)vbe_info->framebuffer, m.x, m.y);
    }
}

void gfx_init()
{
    mutex_init(&wind.order_lock);
}
