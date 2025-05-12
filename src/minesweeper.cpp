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

inline uint8_t Minesweeper::get_x_pos(uint8_t position)
{
    return position >> 3;
}
inline uint8_t Minesweeper::get_y_pos(uint8_t position)
{
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
        shoot();
        break;
    default:
        // Invalid command
        break;
    }

    player_position = 8 * x + y;
}

bool Minesweeper::is_revealed(uint8_t position)
{
    int32_t x = get_x_pos(position);
    int32_t y = get_y_pos(position);
    return (flag_is_revealed[x] & (1 << y)) != 0;
}

void Minesweeper::set_revealed(uint8_t position)
{
    int32_t x = get_x_pos(position);
    int32_t y = get_y_pos(position);
    flag_is_revealed[x] |= (1 << y);
}

bool Minesweeper::shoot()
{
    if (is_bomb(player_position))
    {
        set_revealed(player_position);
        return true; // Game over
    }
    else
    {
        set_revealed(player_position);
        _reveal_until_neighbouring_bomb(player_position);
        return false; // Continue playing
    }
}

uint8_t Minesweeper::how_many_neighbouring_bombs(uint8_t position)
{
    uint8_t count = 0;
    int32_t x = get_x_pos(position);
    int32_t y = get_y_pos(position);

    for (int32_t i = -1; i <= 1; i++)
    {
        for (int32_t j = -1; j <= 1; j++)
        {
            if (i == 0 && j == 0)
                continue; // Skip the current position
            int32_t nx = x + i;
            int32_t ny = y + j;
            if (nx >= 0 && nx < HEIGHT && ny >= 0 && ny < WIDTH)
            {
                if (is_bomb(nx * WIDTH + ny))
                {
                    count++;
                }
            }
        }
    }
    return count;
}

void Minesweeper::_reveal_until_neighbouring_bomb(uint8_t position)
{
    // BFS algorithm to reveal tiles until a neighbouring bomb is found

    uint8_t queue[WIDTH * HEIGHT];
    int32_t front = 0;
    int32_t rear = 0;
    queue[rear++] = position;
    set_revealed(position);
    while (front < rear)
    {
        uint8_t current = queue[front++];
        uint8_t count = how_many_neighbouring_bombs(current);
        if (count > 0)
        {
            continue; // Stop if a neighbouring bomb is found
        }
        for (int32_t i = -1; i <= 1; i++)
        {
            for (int32_t j = -1; j <= 1; j++)
            {
                if (i == 0 && j == 0)
                    continue; // Skip the current position
                int32_t nx = get_x_pos(current) + i;
                int32_t ny = get_y_pos(current) + j;
                if (nx >= 0 && nx < HEIGHT && ny >= 0 && ny < WIDTH)
                {
                    uint8_t neighbour = nx * WIDTH + ny;
                    if (!is_revealed(neighbour))
                    {
                        set_revealed(neighbour);
                        queue[rear++] = neighbour;
                    }
                }
            }
        }
    }
}