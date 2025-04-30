#include <Arduino.h>
// #include <Serial.h>
#include <BluetoothSerial.h>
BluetoothSerial SerialBT;

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  SerialBT.begin("HoriaESP32"); //Bluetooth device name
  Serial.println("The device started, now you can pair it with bluetooth!");
  Serial.println("Bluetooth device name: HoriaESP32");}

void loop() {
  // put your main code here, to run repeatedly:
  // Serial.println("Hello, World!");
  if (SerialBT.available()) {
    Serial.write(SerialBT.read());
  }
  delay(50); // Wait for 1 second
}

int main() {
  setup();
  while (true) {
    loop();
  }
  return 0;
}