#include "minesweeper.h"

Minesweeper::Minesweeper()
{
    player_turn = 0; // Start with player 0
    for (int i = 0; i < (WIDTH * HEIGHT + 7) / 8; i++)
    {
        flag_is_revealed[i] = 0;
        marked_as_bomb[0][i] = marked_as_bomb[1][i] = 0; // Initialize marked positions as not bombs
    }

    is_lost = false;
    player_position[0] = player_position[1] = 0; // Start at the top-left corner
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
    uint8_t x = get_x_pos(player_position[player_turn]);
    uint8_t y = get_y_pos(player_position[player_turn]);

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

    player_position[player_turn] = 8 * x + y;
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
    marked_as_bomb[player_turn][x] &= ~(1 << y); // Unmark as bomb when revealed
}

bool Minesweeper::is_marked_as_bomb(uint8_t position)
{
    int32_t x = get_x_pos(position);
    int32_t y = get_y_pos(position);
    return (marked_as_bomb[player_turn][x] & (1 << y)) != 0;
}
void Minesweeper::set_marked_as_bomb(uint8_t position)
{
    if (is_revealed(position))
    {
        return; // Cannot mark a revealed position as a bomb
    }
    int32_t x = get_x_pos(position);
    int32_t y = get_y_pos(position);
    marked_as_bomb[player_turn][x] ^= (1 << y); // change state
}

bool Minesweeper::shoot()
{
    if (is_bomb(player_position[player_turn]))
    {
        set_revealed(player_position[player_turn]);
        is_lost = true;
        return true; // Game over
    }
    else
    {
        set_revealed(player_position[player_turn]);
        _reveal_until_neighbouring_bomb(player_position[player_turn]);
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

void Minesweeper::draw_map(TFT_eSPI &tft)
{
    const int pixel_size = 13;
    tft.setTextSize(1);
    for (int i = 0; i < WIDTH; i++)
    {
        for (int j = 0; j < HEIGHT; j++)
        {
            if (j * 8 + i == this->get_player_position())
            {
                // Write 0 at the first position
                tft.fillRect(i * pixel_size, j * pixel_size, pixel_size, pixel_size, TFT_ORANGE);
                tft.setTextColor(TFT_WHITE);
                tft.setTextSize(1);
                if (this->is_revealed(j * 8 + i))
                {
                    if (this->is_bomb(j * 8 + i))
                    {
                        tft.setCursor(i * pixel_size + 2, j * pixel_size + 2);
                        tft.print("L");
                    }
                    else // standard revealed tile
                    {
                        // tft.setTextColor(TFT_BLACK);
                        char text[2];
                        int num_bombs = this->how_many_neighbouring_bombs(j * 8 + i);
                        sprintf(text, "%d", num_bombs);
                        tft.setCursor(i * pixel_size + 2, j * pixel_size + 2);
                        tft.print(text);
                    }
                }
                else if (this->is_marked_as_bomb(j * 8 + i))
                {
                    tft.setCursor(i * pixel_size + 2, j * pixel_size + 2);
                    tft.setTextColor(TFT_BLACK);
                    tft.print("B");
                }
            }
            else if (!this->is_revealed(j * 8 + i) && !this->is_marked_as_bomb(j * 8 + i))
            {
                tft.drawRect(i * pixel_size, j * pixel_size, pixel_size, pixel_size, TFT_BLACK);
                tft.fillRect(i * pixel_size + 1, j * pixel_size + 1, pixel_size - 2, pixel_size - 2, TFT_LIGHTGREY);
            }
            else if (this->is_revealed(j * 8 + i) && this->is_bomb(j * 8 + i))
            {
                tft.drawRect(i * pixel_size, j * pixel_size, pixel_size, pixel_size, TFT_BLACK);
                tft.fillRect(i * pixel_size + 1, j * pixel_size + 1, pixel_size - 2, pixel_size - 2, TFT_RED);
            }
            else if (!this->is_revealed(j * 8 + i) && this->is_marked_as_bomb(j * 8 + i))
            {
                tft.drawRect(i * pixel_size, j * pixel_size, pixel_size, pixel_size, TFT_BLACK);
                tft.fillRect(i * pixel_size + 1, j * pixel_size + 1, pixel_size - 2, pixel_size - 2, TFT_YELLOW);
                tft.setCursor(i * pixel_size + 2, j * pixel_size + 2);
                tft.setTextColor(TFT_BLACK);
                tft.print("B");
            }
            else
            {
                // revealed but not a bomb
                tft.drawRect(i * pixel_size, j * pixel_size, pixel_size, pixel_size, TFT_BLACK);
                tft.fillRect(i * pixel_size + 1, j * pixel_size + 1, pixel_size - 2, pixel_size - 2, TFT_GREEN);
                tft.setCursor(i * pixel_size + 2, j * pixel_size + 2);

                char text[2];
                int num_bombs = this->how_many_neighbouring_bombs(j * 8 + i);
                if (num_bombs > 0)
                {
                    sprintf(text, "%d", num_bombs);
                    tft.setTextColor(TFT_BLACK);
                    tft.print(text);
                }
            }
        }
    }
}

void Minesweeper::builtin_button_pressed()
{
    set_marked_as_bomb(player_position[player_turn]);
}

bool Minesweeper::won()
{
    // Check if all non-bomb positions are revealed
    for (int i = 0; i < WIDTH * HEIGHT; i++)
    {
        if (!is_bomb(i) && !is_revealed(i))
        {
            return false; // Not all non-bomb positions are revealed
        }

        if (is_bomb(i) && !is_marked_as_bomb(i))
        {
            return false; // Not all bomb positions are marked as bombs
        }
    }
    return true; // All non-bomb positions are revealed and all bombs are marked
}