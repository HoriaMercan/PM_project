#ifndef _MINESWEEPER_H_
#define _MINESWEEPER_H_

#include "esp_random.h"

#include "bt_commands.h"

#include <TFT_eSPI.h>

#define WIDTH 8
#define HEIGHT 16

#define NUM_BOMBS (WIDTH * HEIGHT / 8)

class Minesweeper
{
public:
    uint8_t bombs[NUM_BOMBS];
    uint8_t flag_is_revealed[(WIDTH * HEIGHT + 7) / 8]; // common for both players
    uint8_t player_position[2];
    uint8_t marked_as_bomb[2][(WIDTH * HEIGHT + 7) / 8]; // For marking positions as bombs

    int player_turn; // 0 or 1, which player is currently playing
    bool is_lost;
    void _reveal_until_neighbouring_bomb(uint8_t position);

// public:
    Minesweeper();
    static inline uint8_t get_x_pos(uint8_t position);
    static inline uint8_t get_y_pos(uint8_t position);
    uint8_t is_bomb(uint8_t position)
    {
        {
            for (int i = 0; i < NUM_BOMBS; i++)
            {
                if (bombs[i] == position)
                {
                    return 1;
                }
            }
            return 0;
        }
    }
    void move_player(command_t command);
    uint8_t get_player_position()
    {
        return player_position[player_turn];
    }
    bool is_revealed(uint8_t position);
    void set_revealed(uint8_t position);

    bool is_marked_as_bomb(uint8_t position);
    void set_marked_as_bomb(uint8_t position);

    void builtin_button_pressed();

    bool shoot();
    uint8_t how_many_neighbouring_bombs(uint8_t position);

    bool is_game_over()
    {
        return is_lost;
    }

    void draw_map(TFT_eSPI &tft);

    bool won();

};

#endif // _MINESWEEPER_H_