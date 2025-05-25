#include <Arduino.h>
#include <BluetoothSerial.h>
#include <map>

#include <TFT_eSPI.h> // Include the graphics library (this includes the sprite functions)
#include <SPI.h>

// #define TFT_WIDTH 128
// #define TFT_HEIGHT 160
#include "minesweeper.h"
#include "bt_commands.h"

TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h

unsigned long targetTime = 0;
byte red = 31;
byte green = 0;
byte blue = 0;
byte state = 0;
unsigned int colour = red << 11;

uint8_t shouldRedrawMap = 0;

// Check if Bluetooth Serial is properly supported
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` and enable Bluetooth Classic.
#endif

#define BAUD_RATE 9600

// Global variables
BluetoothSerial SerialBT;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Keep track of connected clients (in Classic BT we'll have just one active client)
uint8_t connectedAddress[6] = {0};
bool hasConnectedClient = false;

// Queue for handling messages
#define MAX_MESSAGES 10
#define MAX_MESSAGE_LENGTH 100
typedef struct
{
  uint16_t length;
  uint8_t data[MAX_MESSAGE_LENGTH];
} message_t;

message_t messageQueue[MAX_MESSAGES];
int messageQueueHead = 0;
int messageQueueTail = 0;
portMUX_TYPE messageQueueMux = portMUX_INITIALIZER_UNLOCKED;

Minesweeper game;

// Function to add message to queue to be handled by main loop
bool addMessageToQueue(uint8_t *data, size_t length)
{
  if (length > MAX_MESSAGE_LENGTH)
  {
    length = MAX_MESSAGE_LENGTH; // Truncate if too long
  }

  portENTER_CRITICAL(&messageQueueMux);
  int nextHead = (messageQueueHead + 1) % MAX_MESSAGES;

  if (nextHead == messageQueueTail)
  {
    // Queue is full
    portEXIT_CRITICAL(&messageQueueMux);
    return false;
  }

  messageQueue[messageQueueHead].length = length;
  memcpy(messageQueue[messageQueueHead].data, data, length);

  messageQueueHead = nextHead;
  portEXIT_CRITICAL(&messageQueueMux);
  return true;
}

// Function to get message from queue
bool getMessageFromQueue(message_t *message)
{
  portENTER_CRITICAL(&messageQueueMux);
  if (messageQueueHead == messageQueueTail)
  {
    // Queue is empty
    portEXIT_CRITICAL(&messageQueueMux);
    return false;
  }

  *message = messageQueue[messageQueueTail];
  messageQueueTail = (messageQueueTail + 1) % MAX_MESSAGES;
  portEXIT_CRITICAL(&messageQueueMux);
  return true;
}

// Bluetooth event callback
void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
  if (event == ESP_SPP_SRV_OPEN_EVT)
  {
    Serial.println("Client Connected");

    deviceConnected = true;
    hasConnectedClient = true;

    // Store connected device address
    memcpy(connectedAddress, param->srv_open.rem_bda, 6);

    char addrStr[18];
    sprintf(addrStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            connectedAddress[0], connectedAddress[1], connectedAddress[2],
            connectedAddress[3], connectedAddress[4], connectedAddress[5]);
    Serial.print("Client address: ");
    Serial.println(addrStr);
    SerialBT.print("Hi from the server!");
  }

  else if (event == ESP_SPP_CLOSE_EVT)
  {
    Serial.println("Client Disconnected");
    deviceConnected = false;
    hasConnectedClient = false;
  }

  else if (event == ESP_SPP_DATA_IND_EVT)
  {
    // Data received
    Serial.printf("Received %d bytes\n", param->data_ind.len);

    // Add to message queue
    addMessageToQueue(param->data_ind.data, param->data_ind.len);
  }
}

// ISR for pressing GPIO0 button
void IRAM_ATTR buttonISR_GPIO0()
{
  shouldRedrawMap = 1; // total reset the game
}

bool mark_as_bomb = false;

void IRAM_ATTR buttonISR_GPIO2()
{
  mark_as_bomb = true;
}

bool displayMenu = false;

int32_t lastButtonPress = 0;
void IRAM_ATTR buttonISR_GPIO35()
{
  displayMenu = !displayMenu;
}

// https://circuitdigest.com/microcontroller-projects/esp32-timers-and-timer-interrupts
// Setup one second timer
hw_timer_t *my_timer = NULL;
uint32_t timerCounter = 0;
void IRAM_ATTR timerISR()
{
  timerCounter++;
}

void draw_map()
{
  game.draw_map(tft);
}

void draw_menu()
{
  tft.fillScreen(TFT_CYAN);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Menu");
  tft.setTextSize(1);
  tft.setCursor(10, 50);
  tft.print("Press GPIO35 to resume");
}

#include "pitches.h" // Include the pitches header for note definitions

const int BUZZZER_PIN = GPIO_NUM_25;

// notes in the melody:
int melody[] = {
    NOTE_E5, NOTE_E5, NOTE_E5,
    NOTE_E5, NOTE_E5, NOTE_E5,
    NOTE_E5, NOTE_G5, NOTE_C5, NOTE_D5,
    NOTE_E5,
    NOTE_F5, NOTE_F5, NOTE_F5, NOTE_F5,
    NOTE_F5, NOTE_E5, NOTE_E5, NOTE_E5, NOTE_E5,
    NOTE_E5, NOTE_D5, NOTE_D5, NOTE_E5,
    NOTE_D5, NOTE_G5};

int noteDurations[] = {
    8, 8, 4,
    8, 8, 4,
    8, 8, 8, 8,
    2,
    8, 8, 8, 8,
    8, 8, 8, 16, 16,
    8, 8, 8, 8,
    4, 4};

void setup()
{
  Serial.begin(BAUD_RATE);
  Serial.println("Starting Bluetooth Classic Relay Server...");

  // Initialize Bluetooth Serial
  SerialBT.begin("HoriaESP32");
  // SerialBT.connect()
  SerialBT.register_callback(btCallback);

  Serial.println("Bluetooth Classic device started, ready to pair!");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_CYAN);
  tft.drawString("Bluetooth Relay", 30, 30, 1);
  Serial.println("TFT initialized with red background");
  Serial.printf("TFT width: %d, height: %d\n", tft.width(), tft.height());
  targetTime = millis() + 1000;

  draw_map();

  // Initialize button GPIO0
  pinMode(GPIO_NUM_0, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(GPIO_NUM_0), buttonISR_GPIO0, FALLING);

  pinMode(GPIO_NUM_2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(GPIO_NUM_2), buttonISR_GPIO2, FALLING);

  // pinMode(GPIO_NUM_35, INPUT_PULLUP);
  // attachInterrupt(digitalPinToInterrupt(GPIO_NUM_35), buttonISR_GPIO35, FALLING);

  // Initialize timer for one second
  my_timer = timerBegin(0, 80, true);              // Timer 0, prescaler 80 (1 tick = 1 us)
  timerAttachInterrupt(my_timer, &timerISR, true); // Attach the interrupt
  timerAlarmWrite(my_timer, 1000000 / 1000, true); // Set alarm for 1/1000 second
  timerAlarmEnable(my_timer);                      // Enable the alarm

  int size = sizeof(noteDurations) / sizeof(int);
  pinMode(BUZZZER_PIN, OUTPUT); // Set the buzzer pin as output

  for (int thisNote = 0; thisNote < size; thisNote++)
  {

    // to calculate the note duration, take one second divided by the note type.
    // e.g. quarter note = 1000 / 4, eighth note = 1000/8, etc.
    int noteDuration = 1000 / noteDurations[thisNote];
    tone(BUZZZER_PIN, melody[thisNote], noteDuration);

    // to distinguish the notes, set a minimum time between them.
    // the note's duration + 30% seems to work well:
    int pauseBetweenNotes = noteDuration * 1.30;
    delay(pauseBetweenNotes);
    // stop the tone playing:
    noTone(BUZZZER_PIN);
  }
}

bool play_song = false;
const int noteDuration_size = sizeof(noteDurations) / sizeof(int);
int currentNote = 0; // Track the current note being played
uint32_t lastTimerCheck = 0;
bool action_playing;

void loop()
{
  // Process messages from the queue
  if (mark_as_bomb)
  {
    mark_as_bomb = false;
    Serial.printf("Bomb marked in position %d %d\n",
                  (game.get_player_position() & 0x0F), // X position
                  (game.get_player_position() >> 4));  // Y position
    game.builtin_button_pressed();
    draw_map();
    Serial.printf("flag value %d\n",
                  game.is_marked_as_bomb(game.get_player_position()));

    play_song = true; // Play song when marking a bomb
    lastTimerCheck = timerCounter;
    action_playing = false;
  }

  if (play_song)
  {
    if (currentNote < noteDuration_size)
    {
      int noteDuration = 1000 / noteDurations[currentNote];
      if (!action_playing)
      {
        action_playing = true; // Set the action as playing
        tone(BUZZZER_PIN, melody[currentNote], noteDuration);
      }
      if (timerCounter - lastTimerCheck >= noteDuration + noteDuration / 4) // Check if enough time has passed
      {
        lastTimerCheck = timerCounter; // Update the last check time
        noTone(BUZZZER_PIN);
        currentNote++;
        Serial.printf("Current note: %d, Duration: %d ms\n",
                      currentNote, noteDuration);
        action_playing = false;
      }
    }
    else
    {
      play_song = false; // Stop playing when all notes are done
      currentNote = 0;   // Reset for next time
    }
  }

  if (false)
  {
    Serial.println("Displaying menu");
    draw_menu();
  }
  else
  {
    if (shouldRedrawMap)
    {
      tft.fillScreen(TFT_CYAN);

      game = Minesweeper(); // Reset the game
      draw_map();
      shouldRedrawMap = 0;
    }
    else if (game.is_game_over())
    {
      tft.fillScreen(TFT_RED);
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(2);
      tft.setCursor(10, 10);
      tft.print("Game Over");
      delay(200);
      // game = Minesweeper();
      // shouldRedrawMap = 1;
    }
    else if (game.won())
    {
      tft.fillScreen(TFT_GREEN);
      tft.setTextColor(TFT_BLACK);
      tft.setTextSize(2);
      tft.setCursor(10, 10);
      tft.print("You Won!");
      delay(200);
    }
    message_t message;
    if (getMessageFromQueue(&message))
    {
      Serial.printf("Processing message (%d bytes)\n", message.length);

      // In standard Bluetooth we typically have point-to-point connections
      // You can implement multi-device relaying by storing messages
      // and forwarding when other devices connect

      // If you want to echo back to the same device:
      if (deviceConnected)
      {
        SerialBT.write(message.data, message.length);
      }
      if (message.data[0] == 'L')
      {
        game.move_player(CMD_LEFT);
      }
      else if (message.data[0] == 'R')
      {
        game.move_player(CMD_RIGHT);
      }
      else if (message.data[0] == 'U')
      {
        game.move_player(CMD_UP);
      }
      else if (message.data[0] == 'D')
      {
        game.move_player(CMD_DOWN);
      }
      else if (message.data[0] == 'S')
      {
        game.move_player(CMD_SHOOT);
      }

      draw_map();

      // For debugging, print message content to Serial
      Serial.print("Message content: ");
      for (int i = 0; i < message.length; i++)
      {
        Serial.print((char)message.data[i]);
      }
      Serial.println();
    }
  }

  // Handle reconnection
  if (!deviceConnected && oldDeviceConnected)
  {
    delay(500); // Give the bluetooth stack time to get things ready
    oldDeviceConnected = deviceConnected;
  }

  // Handle new connection
  if (deviceConnected && !oldDeviceConnected)
  {
    oldDeviceConnected = deviceConnected;
  }

  delay(10); // Small delay for stability
}