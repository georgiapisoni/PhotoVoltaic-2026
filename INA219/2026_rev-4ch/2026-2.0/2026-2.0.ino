#include <Adafruit_INA219.h>
#include <Wire.h>

// Minimal INA219-only diagnostic. Disconnect the LCD, RTC, SD module,
// LDR, and every INA219 except the physical module being tested as CH1.
Adafruit_INA219 ina40(0x40);
Adafruit_INA219 ina41(0x41);
Adafruit_INA219 ina44(0x44);
Adafruit_INA219 ina45(0x45);

bool found40 = false;
bool found41 = false;
bool found44 = false;
bool found45 = false;

bool testAddress(Adafruit_INA219 &sensor, uint8_t address)
{
  Serial.print(F("Testing INA219 address 0x"));
  if (address < 0x10) Serial.print('0');
  Serial.print(address, HEX);
  Serial.print(F("... "));
  Serial.flush();

  if (!sensor.begin()) {
    Serial.println(F("not found"));
    return false;
  }

  Serial.println(F("FOUND"));
  return true;
}

void printReading(Adafruit_INA219 &sensor, uint8_t address)
{
  float shuntMV = sensor.getShuntVoltage_mV();
  float busV = sensor.getBusVoltage_V();
  float currentMA = sensor.getCurrent_mA();

  Serial.print(F("0x"));
  if (address < 0x10) Serial.print('0');
  Serial.print(address, HEX);
  Serial.print(F(" | bus: "));
  Serial.print(busV, 3);
  Serial.print(F(" V | shunt: "));
  Serial.print(shuntMV, 3);
  Serial.print(F(" mV | current: "));
  Serial.print(currentMA, 3);
  Serial.println(F(" mA"));
}

void setup()
{
  Serial.begin(9600);
  Wire.begin();

  Serial.println();
  Serial.println(F("INA219 CH1 isolated address test"));
  Serial.println(F("Probing 0x40, 0x41, 0x44 and 0x45"));

  found40 = testAddress(ina40, 0x40);
  found41 = testAddress(ina41, 0x41);
  found44 = testAddress(ina44, 0x44);
  found45 = testAddress(ina45, 0x45);

  uint8_t devicesFound = found40 + found41 + found44 + found45;
  Serial.print(F("INA219 candidate addresses found: "));
  Serial.println(devicesFound);

  if (devicesFound == 0) {
    Serial.println(F("No INA219 found. Check VCC, GND, SDA and SCL."));
  } else if (devicesFound > 1) {
    Serial.println(F("More than one address found. Disconnect other INA219 modules."));
  } else {
    Serial.println(F("One INA219 found. Continuous readings follow."));
  }
}

void loop()
{
  if (found40) printReading(ina40, 0x40);
  if (found41) printReading(ina41, 0x41);
  if (found44) printReading(ina44, 0x44);
  if (found45) printReading(ina45, 0x45);

  delay(2000);
}
