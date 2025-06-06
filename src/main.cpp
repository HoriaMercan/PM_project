#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

#include <TFT_eSPI.h>
#include <SPI.h>

// #define TFT_WIDTH 128
// #define TFT_HEIGHT 160
#include "minesweeper.h"
#include "bt_commands.h"

TFT_eSPI tft = TFT_eSPI();

volatile uint8_t shouldRedrawMap = 0;
bool formerDisplayMenu = false;

// Check if Bluetooth Serial is properly supported
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` and enable Bluetooth Classic.
#endif

#define BAUD_RATE 9600

// Global variables
bool deviceConnected = false;
bool oldDeviceConnected = false;

struct device_connected_t
{
  esp_bd_addr_t remote_bda; // Remote Bluetooth device address
  char name[10];
};

device_connected_t devices[2]; // Structure to hold connected device information
uint32_t devices_size = 0;

// Keep track of connected clients (in Classic BT we'll have just one active client)
uint8_t connectedAddress[6] = {0};
bool hasConnectedClient = false;

// Queue for handling messages
#define MAX_MESSAGES 10
#define MAX_MESSAGE_LENGTH 20
typedef struct
{
  uint8_t handle[6]; // Mac address of the device who sent the msg
  uint16_t length;
  uint8_t data[MAX_MESSAGE_LENGTH];
} message_t;

message_t messageQueue[MAX_MESSAGES];
int messageQueueHead = 0;
int messageQueueTail = 0;
portMUX_TYPE messageQueueMux = portMUX_INITIALIZER_UNLOCKED;

Minesweeper game;
int playerTurn = 0;
volatile bool gameStarted = false;
volatile bool displayMenu = true;

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

//--------------------------------------------START OF MESSAGE QUEUE CODE--------------------------------------------

// Function to add message to queue to be handled fby main loop
bool addMessageToQueue(uint8_t mac_addr[6], uint8_t *data, size_t length)
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

  // messageQueue[messageQueueHead].handle = handle;
  memcpy(messageQueue[messageQueueHead].handle, mac_addr, sizeof(messageQueue[messageQueueHead].handle));
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

//--------------------------------------------END OF MESSAGE QUEUE CODE--------------------------------------------

//--------------------------------------------START OF BLUETOOTH CONNECTION CODE--------------------------------------------

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param)
  {
    deviceConnected = true;
    BLEDevice::startAdvertising();
    // Get mac address of the connected device
    esp_bd_addr_t *addr = (esp_bd_addr_t *)param->connect.remote_bda;
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             (*addr)[0], (*addr)[1], (*addr)[2],
             (*addr)[3], (*addr)[4], (*addr)[5]);
    Serial.print("Connected to device with MAC: ");
    Serial.println(mac);

    for (int i = 0; i < devices_size; i++)
    {
      if (memcmp(devices[i].remote_bda, addr, sizeof(esp_bd_addr_t)) == 0)
      {
        // Device already exists in the list
        return;
      }
    }
    if (devices_size < sizeof(devices) / sizeof(devices[0]))
    {
      // Add new device to the list
      memcpy(devices[devices_size].remote_bda, addr, sizeof(esp_bd_addr_t));

      char _name[10];
      sprintf(_name, "Device %d", devices_size + 1);
      size_t _name_len = strnlen(_name, 9);
      strncpy(devices[devices_size].name, _name, _name_len);
      devices[devices_size].name[_name_len] = '\0'; // Ensure null termination
      devices_size++;
      formerDisplayMenu = false; // Reset display menu flag
    }
    else
    {
      Serial.println("Device list is full, cannot add new device");
    }
  };

  void onDisconnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param)
  {
    Serial.print("Device disconnected: ");
    esp_bd_addr_t *addr = (esp_bd_addr_t *)param->connect.remote_bda;
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             (*addr)[0], (*addr)[1], (*addr)[2],
             (*addr)[3], (*addr)[4], (*addr)[5]);
    Serial.println(mac);
    deviceConnected = false;

    // Remove device from the list
    for (int i = 0; i < devices_size; i++)
    {
      if (memcmp(devices[i].remote_bda, addr, sizeof(esp_bd_addr_t)) == 0)
      {
        // Shift remaining devices down
        for (int j = i; j < devices_size - 1; j++)
        {
          memcpy(&devices[j], &devices[j + 1], sizeof(device_connected_t));
        }
        devices_size--;
        // display the menu when someone disconnects
        displayMenu = true;
        formerDisplayMenu = false; // Reset display menu flag

        playerTurn = 0; // Reset player turn

        break;
      }
    }
  }

  void onWrite(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t *param)
  {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
      Serial.print("Received Value: ");
      Serial.print(value.c_str());
      Serial.printf(" From MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    param->write.bda[0], param->write.bda[1],
                    param->write.bda[2], param->write.bda[3],
                    param->write.bda[4], param->write.bda[5]);
      // Send back the received value
      pCharacteristic->setValue(value);
      pCharacteristic->notify();
      Serial.println();
      // Add to message queue
      if (!addMessageToQueue(param->write.bda, (uint8_t *)value.c_str(), value.length()))
      {
        Serial.println("Message queue is full, dropping message");
      }
    }
  }
};

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
class MyCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t *param)
  {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
      Serial.print("Received Value: ");
      for (int i = 0; i < value.length(); i++)
      {
        Serial.print(value[i]);
      }
      // Send back the received value
      Serial.printf(" From MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    param->write.bda[0], param->write.bda[1],
                    param->write.bda[2], param->write.bda[3],
                    param->write.bda[4], param->write.bda[5]);
      // Send back the received value
      // Add to message queue
      if (!addMessageToQueue(param->write.bda, (uint8_t *)value.c_str(), value.length()))
      {
        Serial.println("Message queue is full, dropping message");
      }
      pCharacteristic->setValue(value);
      pCharacteristic->notify();
      Serial.println();
    }
  }
};

//--------------------------------------------END OF BLUETOOTH CONNECTION CODE--------------------------------------------

//---------------------------------------------START OF ISRs CODE--------------------------------------------

// https://circuitdigest.com/microcontroller-projects/esp32-timers-and-timer-interrupts
// Setup one second timer
hw_timer_t *my_timer = NULL;
volatile uint32_t timerCounter = 0; // this increments every milisecond
void IRAM_ATTR timerISR()
{
  timerCounter++;
}

volatile uint32_t lastButtonPressTime = 0; // Last time a button was pressed

// ISR for pressing GPIO0 button
void IRAM_ATTR buttonISR_GPIO0()
{
  if (timerCounter - lastButtonPressTime < 300)
  {
    return; // Ignore button presses that are too close together
  }
  lastButtonPressTime = timerCounter; // Update last button press time
  shouldRedrawMap = 1; // total reset the game
  displayMenu = true;
  formerDisplayMenu = false; // Reset display menu flag
  playerTurn = 0;            // Reset player turn
}

// Marjk as bomb button on GPIO2
volatile bool mark_as_bomb = false;

void IRAM_ATTR buttonISR_GPIO2()
{
  if (timerCounter - lastButtonPressTime < 300)
  {
    return; // Ignore button presses that are too close together
  }
  lastButtonPressTime = timerCounter; // Update last button press time
  mark_as_bomb = true;
}

// Handke Menu: Start game and pause on GPIO32
void IRAM_ATTR buttonISR_GPIO32()
{
  if (timerCounter - lastButtonPressTime < 300)
  {
    return; // Ignore button presses that are too close together
  }
  lastButtonPressTime = timerCounter; // Update last button press time
  displayMenu = !displayMenu; // Toggle menu display
}

//---------------------------------------------END OF ISRs CODE--------------------------------------------

//---------------------------------------------START OF TFT DRAWING CODE--------------------------------------------

void draw_map(bool update_players_order = false)
{
  // Clear the buttom of the screen for displaying player turn properly
  if (update_players_order)
    tft.fillRect(0, 13 * 16, tft.width(), tft.height() - 13 * 16, TFT_CYAN);

  game.draw_map(tft);
  tft.setTextSize(1);

  tft.setCursor(2, 13 * 16);

  if (devices_size == 0)
  {
    tft.setTextColor(TFT_RED);
    tft.printf("No devices connected");
    tft.setTextColor(TFT_BLACK);
    return;
  }

  tft.setTextColor(TFT_RED);
  tft.printf("%s *", devices[playerTurn].name);
  tft.setTextColor(TFT_BLACK);
  if (devices_size == 1)
  {
    return;
  }

  tft.setCursor(2, 14 * 16);
  tft.printf("%s", devices[1 - playerTurn].name);
  // tft.setCursor(10, 13 * 16);
}

void draw_menu()
{
  tft.fillScreen(TFT_CYAN);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.print("Menu");
  tft.setTextSize(2);
  tft.setCursor(3, 50);
  tft.print("Press again to resume");

  tft.setTextSize(1);
  tft.setCursor(2, 90);
  tft.print("Connected devices:");
  for (int i = 0; i < devices_size; i++)
  {
    tft.setCursor(8, 110 + i * 20);
    tft.printf("%s (%02X:%02X:%02X:%02X:%02X:%02X)",
               devices[i].name,
               devices[i].remote_bda[0], devices[i].remote_bda[1],
               devices[i].remote_bda[2], devices[i].remote_bda[3],
               devices[i].remote_bda[4], devices[i].remote_bda[5]);
  }
}

// ---------------------------------------------END OF TFT DRAWING CODE--------------------------------------------

#include "pitches.h" // Include the pitches header for note definitions

const int BUZZZER_PIN = GPIO_NUM_25;

// notes in the melody:
int melodyWin[] = {
    NOTE_E5, NOTE_E5, NOTE_E5,
    NOTE_E5, NOTE_E5, NOTE_E5,
    NOTE_E5, NOTE_G5, NOTE_C5, NOTE_D5,
    NOTE_E5,
    NOTE_F5, NOTE_F5, NOTE_F5, NOTE_F5,
    NOTE_F5, NOTE_E5, NOTE_E5, NOTE_E5, NOTE_E5,
    NOTE_E5, NOTE_D5, NOTE_D5, NOTE_E5,
    NOTE_D5, NOTE_G5};

int noteDurationsWin[] = {
    8, 8, 4,
    8, 8, 4,
    8, 8, 8, 8,
    2,
    8, 8, 8, 8,
    8, 8, 8, 16, 16,
    8, 8, 8, 8,
    4, 4};

int gameOverMelody[] = {
    NOTE_C5, NOTE_B4, NOTE_AS4, NOTE_A4,
    NOTE_GS4, NOTE_G4, NOTE_FS4, NOTE_F4};

int gameOverDurations[] = {
    8, 8, 8, 8,
    8, 8, 8, 4};

int simpleMoveMelody[] = {
    NOTE_C6};
int simpleMoveDurations[] = {
    16};

int placeBombMelody[] = {
    NOTE_C4};

int placeBombDurations[] = {
  16};

void init_bt()
{
  // Create the BLE Device
  BLEDevice::init("HoriaESP32");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_INDICATE);

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCharacteristic->addDescriptor(new BLE2902());

  // Add the callback for characteristic writes
  pCharacteristic->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x0); // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
}

void setup()
{
  Serial.begin(BAUD_RATE);
  Serial.println("Starting Bluetooth Classic Relay Server...");

  init_bt();

  Serial.println("Bluetooth Classic device started, ready to pair!");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_CYAN);
  tft.drawString(" Horia BlueBomb ", 18, 30, 2);
  Serial.println("TFT initialized with red background");
  Serial.printf("TFT width: %d, height: %d\n", tft.width(), tft.height());

  // draw_map();

  // Initialize button GPIO0
  pinMode(GPIO_NUM_0, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(GPIO_NUM_0), buttonISR_GPIO0, FALLING);

  pinMode(GPIO_NUM_2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(GPIO_NUM_2), buttonISR_GPIO2, FALLING);

  pinMode(GPIO_NUM_32, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(GPIO_NUM_32), buttonISR_GPIO32, FALLING);

  // Initialize timer for one second
  my_timer = timerBegin(0, 80, true);              // Timer 0, prescaler 80 (1 tick = 1 us)
  timerAttachInterrupt(my_timer, &timerISR, true); // Attach the interrupt
  timerAlarmWrite(my_timer, 1000000 / 1000, true); // Set alarm for 1/1000 second
  timerAlarmEnable(my_timer);                      // Enable the alarm

  // Sing when the device starts
  int size = sizeof(noteDurationsWin) / sizeof(int);
  pinMode(BUZZZER_PIN, OUTPUT); // Set the buzzer pin as output

  for (int thisNote = 0; thisNote < size; thisNote++)
  {

    // to calculate the note duration, take one second divided by the note type.
    // e.g. quarter note = 1000 / 4, eighth note = 1000/8, etc.
    int noteDuration = 1000 / noteDurationsWin[thisNote];
    tone(BUZZZER_PIN, melodyWin[thisNote], noteDuration);

    // to distinguish the notes, set a minimum time between them.
    // the note's duration + 30% seems to work well:
    int pauseBetweenNotes = noteDuration * 1.30;
    delay(pauseBetweenNotes);
    // stop the tone playing:
    noTone(BUZZZER_PIN);
  }
}

bool play_song = false;

const int noteWinDuration_size = sizeof(noteDurationsWin) / sizeof(int);
const int noteGameOverDuration_size = sizeof(gameOverDurations) / sizeof(int);

int currentNote = 0; // Track the current note being played
uint32_t lastTimerCheck = 0;
bool action_playing;

const int displayFinalScreenTime = 2000; // 2 seconds in timer ticks (1000 ticks per second)
int startTimeDisplayFinalScreen = 0;     // Start time for displaying final screen
bool displayFinalScreen = false;         // Flag to indicate if final screen should be displayed

int note_size;
int *curr_notes;
int *curr_durations;

enum MusicType
{
  MUSIC_WIN,
  MUSIC_GAME_OVER,
  MUSIC_SIMPLE_MOVE,
  MUSIC_PLACE_BOMB
};

void startMusic(MusicType type)
{
  if (type == MUSIC_WIN)
  {
    curr_notes = melodyWin;
    curr_durations = noteDurationsWin;
    note_size = noteWinDuration_size;
  }
  else if (type == MUSIC_GAME_OVER)
  {
    curr_notes = gameOverMelody;
    curr_durations = gameOverDurations;
    note_size = noteGameOverDuration_size;
  }
  else if (type == MUSIC_SIMPLE_MOVE)
  {
    curr_notes = simpleMoveMelody;
    curr_durations = simpleMoveDurations;
    note_size = sizeof(simpleMoveDurations) / sizeof(int);
  }
  else if (type == MUSIC_PLACE_BOMB)
  {
    curr_notes = placeBombMelody;
    curr_durations = placeBombDurations;
    note_size = sizeof(placeBombDurations) / sizeof(int);
  }
  currentNote = 0;        // Reset the current note
  play_song = true;       // Set the flag to start playing music
  action_playing = false; // Reset action playing state

  // DDRAM(BUZZZER_PIN, OUTPUT); // Set the buzzer pin as output
}

void change_player_name(message_t &message)
{
  char new_name[10];
  size_t name_length = message.length - 1; // Exclude the command character
  if (name_length > 9)
  {
    name_length = 9; // Limit to 9 characters
  }
  memcpy(new_name, &message.data[1], name_length);
  new_name[name_length] = '\0'; // Null-terminate the string

  for (int i = 0; i < devices_size; i++)
  {
    Serial.printf("Checking device %d with MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  i,
                  devices[i].remote_bda[0], devices[i].remote_bda[1],
                  devices[i].remote_bda[2], devices[i].remote_bda[3],
                  devices[i].remote_bda[4], devices[i].remote_bda[5]);
    if (memcmp(devices[i].remote_bda, message.handle, sizeof(esp_bd_addr_t)) == 0)
    {
      // Found the device, update its name
      strncpy(devices[i].name, new_name, sizeof(devices[i].name) - 1);
      devices[i].name[sizeof(devices[i].name) - 1] = '\0'; // Ensure null termination
      Serial.printf("Updated device name to: %s\n", devices[i].name);
      break;
    }
  }
}

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

    // play_song = true; // Play song when marking a bomb
    lastTimerCheck = timerCounter;
    action_playing = false;
    startMusic(MUSIC_PLACE_BOMB); // Start playing the place bomb melody
  }

  if (play_song)
  {
    if (currentNote < note_size)
    {
      int noteDuration = 1000 / curr_durations[currentNote];
      if (!action_playing)
      {
        action_playing = true; // Set the action as playing
        tone(BUZZZER_PIN, curr_notes[currentNote], noteDuration);
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

  message_t message;

  if (displayMenu && !formerDisplayMenu)
  {
    Serial.println("Displaying menu");
    draw_menu();
    formerDisplayMenu = true; // Set the flag to indicate menu is displayed
    Serial.println("Menu button pressed, toggling displayMenu state");
    Serial.printf("displayMenu: %d\n\n", displayMenu);
  }
  else if (displayMenu)
  {

    if (getMessageFromQueue(&message))
    { // Loop over received messages while displaying the menu
      // Don't process commands, only change the name:
      if (message.length > 0 && message.data[0] == 'N')
      {
        Serial.printf("Changing name for device with MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      message.handle[0], message.handle[1],
                      message.handle[2], message.handle[3],
                      message.handle[4], message.handle[5]);
        change_player_name(message);
        formerDisplayMenu = false; // Reset the flag to redraw the menu
      }
    }
  }
  else if (!displayMenu)
  {
    if (formerDisplayMenu)
    {
      tft.fillScreen(TFT_CYAN);
      draw_map();                // Redraw the game map when exiting the menu
      formerDisplayMenu = false; // Reset the flag when exiting the menu
    }
    if (shouldRedrawMap)
    {
      tft.fillScreen(TFT_CYAN);

      game = Minesweeper(); // Reset the game
      draw_map();
      shouldRedrawMap = 0;
    }
    else if (game.is_game_over())
    {
      if (!displayFinalScreen && !game.displayed_final)
      {

        detachInterrupt(digitalPinToInterrupt(GPIO_NUM_0));  // Disable button interrupt
        detachInterrupt(digitalPinToInterrupt(GPIO_NUM_2));  // Disable bomb marking button interrupt
        detachInterrupt(digitalPinToInterrupt(GPIO_NUM_32)); // Disable menu button interrupt

        displayFinalScreen = true; // Set the flag to indicate final scr
        tft.fillScreen(TFT_RED);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(2);
        tft.setCursor(10, 10);
        tft.print("Game Over");

        tft.setCursor(10, 50);
        tft.printf("Lose: %s\n", devices[playerTurn].name);
        game.displayed_final = true;                // Set the flag to indicate final screen has been displayed
        startTimeDisplayFinalScreen = timerCounter; // Start the timer for displaying final screen

        startMusic(MUSIC_GAME_OVER); // Start playing the game over melody
      }
      if (displayFinalScreen && timerCounter - startTimeDisplayFinalScreen >= displayFinalScreenTime)
      {
        // If the final screen has been displayed for enough time, reset the game
        displayFinalScreen = false; // Reset the flag

        tft.setCursor(10, 90);
        tft.setTextSize(1);
        tft.print("You can exit the page now");

        attachInterrupt(digitalPinToInterrupt(GPIO_NUM_0), buttonISR_GPIO0, FALLING);   // Re-enable button interrupt
        attachInterrupt(digitalPinToInterrupt(GPIO_NUM_2), buttonISR_GPIO2, FALLING);   // Re-enable bomb marking button interrupt
        attachInterrupt(digitalPinToInterrupt(GPIO_NUM_32), buttonISR_GPIO32, FALLING); // Re-enable menu button interrupt
      }

      // game = Minesweeper();
      // shouldRedrawMap = 1;
    }
    else if (game.won())
    {
      if (!displayFinalScreen && !game.displayed_final)
      {

        detachInterrupt(digitalPinToInterrupt(GPIO_NUM_0));  // Disable button interrupt
        detachInterrupt(digitalPinToInterrupt(GPIO_NUM_2));  // Disable bomb marking button interrupt
        detachInterrupt(digitalPinToInterrupt(GPIO_NUM_32)); // Disable menu button interrupt

        displayFinalScreen = true; // Set the flag to indicate final screen should be displayed
        tft.fillScreen(TFT_GREEN);
        tft.setTextColor(TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(10, 10);
        tft.print("You Won!");
        tft.setCursor(10, 50);
        tft.printf("Congrats %s\n", devices[playerTurn].name);
        game.displayed_final = true;                // Set the flag to indicate final screen has been displayed
        startTimeDisplayFinalScreen = timerCounter; // Start the timer for displaying final screen
        startMusic(MUSIC_WIN);                      // Start playing the win melody
      }
      if (displayFinalScreen && timerCounter - startTimeDisplayFinalScreen >= displayFinalScreenTime)
      {
        // If the final screen has been displayed for enough time, reset the game
        displayFinalScreen = false; // Reset the flag

        tft.setCursor(10, 90);
        tft.setTextSize(1);
        tft.print("You can exit the page now");

        attachInterrupt(digitalPinToInterrupt(GPIO_NUM_0), buttonISR_GPIO0, FALLING);   // Re-enable button interrupt
        attachInterrupt(digitalPinToInterrupt(GPIO_NUM_2), buttonISR_GPIO2, FALLING);   // Re-enable bomb marking button interrupt
        attachInterrupt(digitalPinToInterrupt(GPIO_NUM_32), buttonISR_GPIO32, FALLING); // Re-enable menu button interrupt
      }
    }
    if (getMessageFromQueue(&message)) // Going through the message queue
    {
      Serial.printf("Processing message (%d bytes) from %02X:%02X:%02X:%02X:%02X:%02X\n", message.length,
                    message.handle[0], message.handle[1],
                    message.handle[2], message.handle[3],
                    message.handle[4], message.handle[5]);
      if (memcmp(message.handle, devices[playerTurn].remote_bda, sizeof(esp_bd_addr_t)) != 0)
      {
        // If the message is not from the current player, ignore it
        Serial.println("Message not from current player, ignoring");
      }
      else
      {
        // If you want to echo back to the same device:
        if (message.data[0] == 'L')
        {
          game.move_player(CMD_LEFT);
          startMusic(MUSIC_SIMPLE_MOVE); // Play simple move melody
        }
        else if (message.data[0] == 'R')
        {
          game.move_player(CMD_RIGHT);
          startMusic(MUSIC_SIMPLE_MOVE); // Play simple move melody
        }
        else if (message.data[0] == 'U')
        {
          game.move_player(CMD_UP);
          startMusic(MUSIC_SIMPLE_MOVE); // Play simple move melody
        }
        else if (message.data[0] == 'D')
        {
          game.move_player(CMD_DOWN);
          startMusic(MUSIC_SIMPLE_MOVE); // Play simple move melody
        }
        else if (message.data[0] == 'S')
        {
          game.move_player(CMD_SHOOT);
          playerTurn = (playerTurn + 1) % devices_size; // Switch to the next player
          game.set_player_turn(playerTurn);
        }
        else if (message.data[0] == 'N')
        { // Change name for MAC address
          change_player_name(message);
        }

        draw_map(message.data[0] == 'S');
      }

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

int main()
{
  setup();
  while (true)
  {
    loop();
  }
  return 0;
}