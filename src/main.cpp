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
  game = Minesweeper(); // Reset the game
  draw_map();
}

// https://circuitdigest.com/microcontroller-projects/esp32-timers-and-timer-interrupts
// Setup one second timer
hw_timer_t *timer = NULL;
void IRAM_ATTR timerISR()
{
  Serial.println("Timer interrupt triggered");
}

void draw_map()
{
  const int pixel_size = 13;
  for (int i = 0; i < WIDTH; i++)
  {
    for (int j = 0; j < HEIGHT; j++)
    {
      if (j * 8 + i == game.get_player_position())
      {
        // Write 0 at the first position
        tft.fillRect(i * pixel_size, j * pixel_size, pixel_size, pixel_size, TFT_ORANGE);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        tft.setCursor(i * pixel_size + 2, j * pixel_size + 2);
        tft.print("0");
      }
      else if (!game.is_revealed(j * 8 + i))
      {
        tft.drawRect(i * pixel_size, j * pixel_size, pixel_size, pixel_size, TFT_BLACK);
        tft.fillRect(i * pixel_size + 1, j * pixel_size + 1, pixel_size - 2, pixel_size - 2, TFT_LIGHTGREY);
      }
      else if (game.is_bomb(j * 8 + i))
      {
        tft.drawRect(i * pixel_size, j * pixel_size, pixel_size, pixel_size, TFT_BLACK);
        tft.fillRect(i * pixel_size + 1, j * pixel_size + 1, pixel_size - 2, pixel_size - 2, TFT_RED);
      }
      else
      {
        tft.drawRect(i * pixel_size, j * pixel_size, pixel_size, pixel_size, TFT_BLACK);
        tft.fillRect(i * pixel_size + 1, j * pixel_size + 1, pixel_size - 2, pixel_size - 2, TFT_GREEN);
        tft.setCursor(i * pixel_size + 2, j * pixel_size + 2);

        char text[2];
        int num_bombs = game.how_many_neighbouring_bombs(j * 8 + i);
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

  // Initialize timer for one second
  // timer = timerBegin(0, 80, true); // Timer 0, prescaler 80 (1 tick = 1 us)
  // timerAttachInterrupt(timer, &timerISR, true); // Attach the interrupt
  // timerAlarmWrite(timer, 1000000, true); // Set alarm for 1 second
  // timerAlarmEnable(timer); // Enable the alarm
}

void loop()
{
  // Process messages from the queue
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