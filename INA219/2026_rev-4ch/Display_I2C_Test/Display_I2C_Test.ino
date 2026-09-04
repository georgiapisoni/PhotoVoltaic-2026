// Display and I2C diagnostic test
// Arduino Nano: SDA = A4, SCL = A5, LCD backpack address = 0x27.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

uint8_t addresses[32];
uint8_t addressCount = 0;
uint8_t displayIndex = 0;
unsigned long lastDisplay = 0;
const unsigned long DISPLAY_INTERVAL_MS = 2000;

void scanBus()
{
  addressCount = 0;
  Serial.println(F("Scanning I2C addresses..."));

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      if (addressCount < sizeof(addresses)) addresses[addressCount++] = address;
      Serial.print(F("I2C device found at 0x"));
      if (address < 0x10) Serial.print('0');
      Serial.println(address, HEX);
    }
  }

  Serial.print(F("I2C scan complete. Devices found: "));
  Serial.println(addressCount);
}

void showAddress()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("I2C devices: "));
  lcd.print(addressCount);

  lcd.setCursor(0, 1);
  if (addressCount == 0) {
    lcd.print(F("none found"));
    return;
  }

  lcd.print('#');
  lcd.print(displayIndex + 1);
  lcd.print(F(" 0x"));
  if (addresses[displayIndex] < 0x10) lcd.print('0');
  lcd.print(addresses[displayIndex], HEX);
}

void setup()
{
  Serial.begin(9600);
  Wire.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("I2C scanner"));
  lcd.setCursor(0, 1);
  lcd.print(F("Starting..."));
  delay(1000);

  scanBus();
  showAddress();
}

void loop()
{
  if (addressCount > 0 && millis() - lastDisplay >= DISPLAY_INTERVAL_MS) {
    lastDisplay = millis();
    displayIndex = (displayIndex + 1) % addressCount;
    showAddress();
  }
}
