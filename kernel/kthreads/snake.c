/**
 * @file snake.c
 * @author Joe Bayer (joexbayer)
 * @brief Textmode only snake game
 * @version 0.1
 * @date 2024-01-10
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#define SNAKE_LENGTH 100
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

#include <kernel.h>
#include <terminal.h>
#include <libc.h>
#include <screen.h>
#include <colors.h>
#include <keyboard.h>
#include <pcb.h>
#include <rtc.h>
#include <timer.h>
#include <ksyms.h>
#include <scheduler.h>

struct point {
    int x, y;
};

struct point snake[SNAKE_LENGTH];
int length = 5;
struct point fruit;
int direction = ARROW_RIGHT;

void init_game() {
    for (int i = 0; i < length; i++) {
        snake[i].x = 10 - i;
        snake[i].y = 10;
    }

    fruit.x = 20;
    fruit.y = 15;
}

void draw() {
    scr_clear();

    for (int i = 0; i < length; i++) {
        scrput(snake[i].x, snake[i].y, 'O', 0x0A);
    }

    scrput(fruit.x, fruit.y, 'F', 0x0C);
}

int get_input() {
    return $process->current->term->ops->getchar($process->current->term);
}

int update() {
    int input = get_input();
    if(input == CTRLC) return -1;

    if (input != 255) {
        direction = input; 
    }

    for (int i = length - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }

    if (direction == ARROW_UP) snake[0].y--;
    if (direction == ARROW_DOWN) snake[0].y++;
    if (direction == ARROW_LEFT) snake[0].x--;
    if (direction == ARROW_RIGHT) snake[0].x++;

    if (snake[0].x == fruit.x && snake[0].y == fruit.y) {
        length++;
        fruit.x = rand() % SCREEN_WIDTH;
        fruit.y = rand() % SCREEN_HEIGHT;
    }

    /* Collision with walls */
    if (snake[0].x < 0 || snake[0].x >= SCREEN_WIDTH ||
        snake[0].y < 0 || snake[0].y >= SCREEN_HEIGHT) {
        return -1;
    }

    /* Self-collision */
    for (int i = 1; i < length; i++) {
        if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
            return -1;
        }
    }

    return 0;
}

void game_loop() {
    while (1) {
        draw();
        if(update() == -1) return;

        kernel_sleep(100);
    }
}

static int snakegame() {
    struct kernel_context* ctx = kernel_get_context();
    if(ctx->graphic_mode == KERNEL_FLAG_GRAPHICS) {
        twritef("This game is not compatible with graphics mode\n");
        return 0;
    }

    init_game();
    game_loop();
    return 0;
}
EXPORT_KSYMBOL(snakegame);