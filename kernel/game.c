/*
 * MyOS - "Snake", written straight into VGA text memory.
 *
 * A tiny example of what a program looks like on top of the kernel: it
 * reads the keyboard buffer, uses the timer for pacing, and draws by
 * writing 16-bit cells (character + colour) into 0xB8000.
 */
#include "kernel.h"

#define SCREEN_W 80
#define SCREEN_H 25
#define FIELD_X0 1
#define FIELD_X1 78
#define FIELD_Y0 1
#define FIELD_Y1 23
#define MAX_LENGTH 600

/* CP437 box drawing characters */
#define CH_TOP_LEFT     0xC9
#define CH_TOP_RIGHT    0xBB
#define CH_BOTTOM_LEFT  0xC8
#define CH_BOTTOM_RIGHT 0xBC
#define CH_HORIZONTAL   0xCD
#define CH_VERTICAL     0xBA
#define CH_BLOCK        0xDB
#define CH_DOT          0xFE

static volatile u16 *const vga = (volatile u16 *)0xB8000;

static void put_char(int x, int y, char c, u8 color)
{
    vga[y * SCREEN_W + x] = (u16)((color << 8) | (u8)c);
}

static void put_text(int x, int y, const char *s, u8 color)
{
    while (*s)
        put_char(x++, y, *s++, color);
}

static u32 random_state = 12345;

static u32 random_number(void)
{
    random_state = random_state * 1103515245u + 12345u;
    return (random_state >> 16) & 0x7FFF;
}

struct cell {
    int x, y;
};

static struct cell snake[MAX_LENGTH];
static int snake_length;
static int direction_x, direction_y;
static struct cell food;
static u32 score;
static int finished;

static void draw_field(u8 border)
{
    put_char(0, 0, CH_TOP_LEFT, border);
    put_char(SCREEN_W - 1, 0, CH_TOP_RIGHT, border);
    put_char(0, SCREEN_H - 1, CH_BOTTOM_LEFT, border);
    put_char(SCREEN_W - 1, SCREEN_H - 1, CH_BOTTOM_RIGHT, border);

    for (int x = 1; x < SCREEN_W - 1; x++) {
        put_char(x, 0, CH_HORIZONTAL, border);
        put_char(x, SCREEN_H - 1, CH_HORIZONTAL, border);
    }
    for (int y = 1; y < SCREEN_H - 1; y++) {
        put_char(0, y, CH_VERTICAL, border);
        put_char(SCREEN_W - 1, y, CH_VERTICAL, border);
    }
}

static int snake_occupies(int x, int y)
{
    for (int i = 0; i < snake_length; i++)
        if (snake[i].x == x && snake[i].y == y)
            return 1;
    return 0;
}

static void place_food(void)
{
    do {
        food.x = FIELD_X0 + (int)(random_number() % (FIELD_X1 - FIELD_X0 + 1));
        food.y = FIELD_Y0 + (int)(random_number() % (FIELD_Y1 - FIELD_Y0 + 1));
    } while (snake_occupies(food.x, food.y));

    put_char(food.x, food.y, CH_DOT, VGA_COLOR(COLOR_LIGHT_RED, COLOR_BLACK));
}

static void draw_score(void)
{
    char buffer[24];
    int n = 0;
    u32 value = score;

    buffer[n++] = 'S';
    buffer[n++] = 'c';
    buffer[n++] = 'o';
    buffer[n++] = 'r';
    buffer[n++] = 'e';
    buffer[n++] = ':';
    buffer[n++] = ' ';

    char digits[12];
    int d = 0;
    if (value == 0)
        digits[d++] = '0';
    while (value) {
        digits[d++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (d)
        buffer[n++] = digits[--d];
    buffer[n] = '\0';

    put_text(SCREEN_W - 2 - n, 0, buffer, VGA_COLOR(COLOR_YELLOW, COLOR_BLACK));
}

static void game_over(void)
{
    put_text(28, 11, "G A M E   O V E R", VGA_COLOR(COLOR_WHITE, COLOR_RED));
    draw_score();
    put_text(24, 13, "press any key to return", VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK));
    finished = 1;
}

static void step_snake(void)
{
    struct cell head = snake[0];
    head.x += direction_x;
    head.y += direction_y;

    if (head.x < FIELD_X0 || head.x > FIELD_X1 ||
        head.y < FIELD_Y0 || head.y > FIELD_Y1) {
        game_over();
        return;
    }

    int eating = (head.x == food.x && head.y == food.y);

    /* The tail moves out of the way, so it is not a collision (unless we grow). */
    int limit = eating ? snake_length : snake_length - 1;
    for (int i = 0; i < limit; i++) {
        if (snake[i].x == head.x && snake[i].y == head.y) {
            game_over();
            return;
        }
    }

    if (!eating) {
        struct cell tail = snake[snake_length - 1];
        put_char(tail.x, tail.y, ' ', VGA_COLOR(COLOR_BLACK, COLOR_BLACK));
        for (int i = snake_length - 1; i > 0; i--)
            snake[i] = snake[i - 1];
    } else {
        if (snake_length < MAX_LENGTH) {
            for (int i = snake_length; i > 0; i--)
                snake[i] = snake[i - 1];
            snake_length++;
        }
        score++;
        draw_score();
    }

    snake[0] = head;
    put_char(head.x, head.y, CH_BLOCK, VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK));

    if (eating)
        place_food();
}

void game_snake(void)
{
    /* reset state */
    console_cursor_hide(1);
    console_clear();
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            vga[y * SCREEN_W + x] = (u16)((VGA_COLOR(COLOR_BLACK, COLOR_BLACK) << 8) | ' ');

    draw_field(VGA_COLOR(COLOR_LIGHT_CYAN, COLOR_BLACK));
    put_text(3, 0, " MyOS Snake ", VGA_COLOR(COLOR_WHITE, COLOR_BLUE));

    snake_length = 5;
    for (int i = 0; i < snake_length; i++) {
        snake[i].x = 20 - i;
        snake[i].y = 12;
    }
    direction_x = 1;
    direction_y = 0;
    score = 0;
    finished = 0;
    random_state = timer_ticks() | 1;

    for (int i = 0; i < snake_length; i++)
        put_char(snake[i].x, snake[i].y, CH_BLOCK, VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK));

    place_food();
    draw_score();
    put_text(3, 0, " arrows to move, Q to quit ", VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLUE));

    int quit = 0;
    u32 last_move = timer_ticks();
    u32 interval = 8;              /* ticks per step: 100 Hz / 8 ≈ 12 steps/s */

    while (!finished) {
        int key = keyboard_getkey();

        if (key >= 0) {
            if (key == 'q' || key == 'Q') {
                quit = 1;
                finished = 1;
                break;
            }
            if (key == KEY_UP && direction_y == 0) {
                direction_x = 0;
                direction_y = -1;
            } else if (key == KEY_DOWN && direction_y == 0) {
                direction_x = 0;
                direction_y = 1;
            } else if (key == KEY_LEFT && direction_x == 0) {
                direction_x = -1;
                direction_y = 0;
            } else if (key == KEY_RIGHT && direction_x == 0) {
                direction_x = 1;
                direction_y = 0;
            }
        }

        if (timer_ticks() - last_move >= interval) {
            last_move = timer_ticks();
            step_snake();

            /* Speed up a little as the snake grows. */
            if (score > 5 && interval > 4)
                interval = (score > 15) ? 4 : 6;
        }

        cpu_wait_interrupt();
    }

    if (finished && !quit) {
        /* wait for a key press so the player can read the score */
        while (keyboard_getkey() < 0)
            cpu_wait_interrupt();
        while (keyboard_getkey() >= 0)
            ;
    }

    console_clear();
    console_cursor_hide(0);
}
