#include "minesweeper.h"

Minesweeper::Minesweeper()
{
    for (int i = 0; i < (WIDTH * HEIGHT + 7) / 8; i++)
    {
        flag_is_revealed[i] = 0;
    }

    esp_fill_random(bombs, NUM_BOMBS);
    for (int i = 0; i < NUM_BOMBS; i++)
    {
        bombs[i] &= 0x7F; // Ensure the first bit is 0
        // for each bomb: 0xxxxyyy, where x is the line and y is the column
    }
}

inline uint8_t Minesweeper::get_x_pos(uint8_t position){
    return position >> 3;
} 
inline uint8_t Minesweeper::get_y_pos(uint8_t position){
    return position & 0x07;
}

void Minesweeper::move_player(command_t command)
{
    uint8_t x = get_x_pos(player_position);
    uint8_t y = get_y_pos(player_position);

    switch (command)
    {
    case CMD_UP:
        if (x > 0)
            x--;
        break;
    case CMD_DOWN:
        if (x < HEIGHT - 1)
            x++;
        break;
    case CMD_LEFT:
        if (y > 0)
            y--;
        break;
    case CMD_RIGHT:
        if (y < WIDTH - 1)
            y++;
        break;
    case CMD_SHOOT:
        // Handle shoot command
        break;
    default:
        // Invalid command
        break;
    }

    player_position = 8 * x + y;
}