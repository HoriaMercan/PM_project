#ifndef _MINESWEEPER_H_
#define _MINESWEEPER_H_

#include "esp_random.h"

#include "bt_commands.h"

#define WIDTH 8
#define HEIGHT 16

#define NUM_BOMBS (WIDTH * HEIGHT / 8)

class Minesweeper
{
private:
    uint8_t bombs[NUM_BOMBS];
    uint8_t flag_is_revealed[(WIDTH * HEIGHT + 7) / 8];
    uint8_t player_position = 0;

    bool is_lost = false;
    void _reveal_until_neighbouring_bomb(uint8_t position);

public:
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
        return player_position;
    }
    bool is_revealed(uint8_t position);
    void set_revealed(uint8_t position);

    bool shoot();
    uint8_t how_many_neighbouring_bombs(uint8_t position);

    bool is_game_over()
    {
        return is_lost;
    }
};

#endif // _MINESWEEPER_H_