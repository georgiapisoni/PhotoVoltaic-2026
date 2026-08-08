//-----libraries-------
#define ENABLE_INA219 1  // INA219 channels 2, 3 and 4 enabled; CH1 disabled.

#if ENABLE_INA219
#include <Adafruit_INA219.h>
#endif
#include <DS1307RTC.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>
#include <Wire.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// I2C addresses: LCD 0x27, RTC 0x68, and INA219 channels 2-4.
#if ENABLE_INA219
const uint8_t INA219_CH2_ADDR = 0x41;
const uint8_t INA219_CH3_ADDR = 0x44;
const uint8_t INA219_CH4_ADDR = 0x45;

Adafruit_INA219 ina219_ch2(INA219_CH2_ADDR);
Adafruit_INA219 ina219_ch3(INA219_CH3_ADDR);
Adafruit_INA219 ina219_ch4(INA219_CH4_ADDR);

float shuntVoltageMV[3] = {0, 0, 0};
float busVoltageV[3] = {0, 0, 0};
float currentMA[3] = {0, 0, 0};
#endif

// setting pins
const uint8_t chipSelect = 10;        //sd card reader -> CS[D10]
const uint8_t LDR_PIN = A0;           //light sensor -> A0=D14

// LDR
uint16_t ldrRaw = 0;
float ldrVolt = 0.0;
float ldrPct = 0.0;                     //percentage

int8_t   lastMin = -1;
#if ENABLE_INA219
const char* DATA_FILE = "ina234.csv";
#else
const char* DATA_FILE = "test.csv";
#endif
tmElements_t displayTime;
bool displayTimeValid = false;

// display
uint8_t screen = 0;
unsigned long lastScreenSwitch = 0;
const unsigned long screenInterval = 4000; // 4 seconds

bool displayDirty = true;

void Leitura(const tmElements_t &time);
#if ENABLE_INA219
void INA219_read(Adafruit_INA219 &sensor, float &shuntMV,
                 float &busV, float &currentMAValue);
#endif

void showStartupStep(const __FlashStringHelper* serialMessage,
                     const __FlashStringHelper* lcdMessage)
{
  Serial.println(serialMessage);
  Serial.flush();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Inicializando"));
  lcd.setCursor(0, 1);
  lcd.print(lcdMessage);
}

void scanI2CBus()
{
  uint8_t devicesFound = 0;

  Serial.println(F("Scanning I2C addresses..."));
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.print(F("I2C device found at 0x"));
      if (address < 0x10) Serial.print('0');
      Serial.println(address, HEX);
      devicesFound++;
    } else if (error == 4) {
      Serial.print(F("Unknown I2C error at 0x"));
      if (address < 0x10) Serial.print('0');
      Serial.println(address, HEX);
    }
  }

  Serial.print(F("I2C scan complete. Devices found: "));
  Serial.println(devicesFound);
  Serial.flush();
}

void setup(){
  tmElements_t time;
  Serial.begin(9600);
  Wire.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Ola Bender"));
  lcd.setCursor(0, 1);
  lcd.print(F("ligando sistema"));


  Serial.println(F("INA219 CH2-CH4 + LDR/RTC/SD"));
  Serial.flush();

  scanI2CBus();

//-----error checks----
#if ENABLE_INA219
  showStartupStep(F("Testing INA219 CH2 at 0x41..."), F("Test INA CH2"));
  if (!ina219_ch2.begin())
  {
    Serial.println(F("Failed to initialize INA219 CH2 at 0x41."));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH2"));
    while (1);
  }

  showStartupStep(F("Testing INA219 CH3 at 0x44..."), F("Test INA CH3"));
  if (!ina219_ch3.begin())
  {
    Serial.println(F("Failed to initialize INA219 CH3 at 0x44."));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH3"));
    while (1);
  }

  showStartupStep(F("Testing INA219 CH4 at 0x45..."), F("Test INA CH4"));
  if (!ina219_ch4.begin())
  {
    Serial.println(F("Failed to initialize INA219 CH4 at 0x45."));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH4"));
    while (1);
  }
#endif

  showStartupStep(F("Testing SD card..."), F("Testando SD"));
  if (!SD.begin(chipSelect))
  {
    pinMode(10, OUTPUT);
    Serial.println(F("Card failed or not present."));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro SD"));
    while (1);
  }
  showStartupStep(F("SD OK. Testing RTC at 0x68..."), F("Testando RTC"));
  if (RTC.read(time))
  {
    lastMin = time.Minute;
    displayTime = time;
    displayTimeValid = true;
  }
  else
  {
    Serial.println(F("Failed to read RTC."));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro RTC"));
    while (1);
  }
  Serial.println(F("RTC OK. Opening CSV file..."));
  Serial.flush();
  //SD FILE setup
  File dataFile = SD.open(DATA_FILE, FILE_WRITE);
  if (!dataFile)
  {
    Serial.println(F("Error opening file"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro .csv"));
    return;
  }
  if (dataFile.size() == 0)               //file empty
  {
#if ENABLE_INA219
    dataFile.println(F("date,time,shuntmV2,busV2,currentmA2,shuntmV3,busV3,currentmA3,shuntmV4,busV4,currentmA4,ldrRaw,ldrVolt,ldrPct"));
#else
    dataFile.println(F("date,time,ldrRaw,ldrVolt,ldrPct"));
#endif
  }
  dataFile.close();
  Serial.println(F("Setup complete."));
  Serial.flush();

  // Take one immediate reading so the test screen is not left at zero.
  Leitura(time);
}

void loop()
{
  tmElements_t time;
  displayTask();

  if (RTC.read(time))
  {
    displayTime = time;
    displayTimeValid = true;

    if(time.Minute != lastMin)
    {
      lastMin = time.Minute;
      Leitura(time);
    }
  }
  else
  {Serial.println(F("Failed to read RTC"));}
}
void printFixed2_1(float value)
{
  if (value < 0) {
    value = 0;
  }

  if (value > 99.9) {
    lcd.print(F("99,9"));
    return;
  }

  uint16_t scaled = (uint16_t)(value * 10.0 + 0.5); // rounded value * 10

  uint8_t integerPart = scaled / 10;
  uint8_t decimalPart = scaled % 10;

  if (integerPart < 10) {
    lcd.print('0');
  }

  lcd.print(integerPart);
  lcd.print(',');
  lcd.print(decimalPart);
}
void printFixed4_0(float value)
{
  if (value < 0) {
    value = 0;
  }

  if (value > 9999) {
    lcd.print(F("9999"));
    return;
  }

  uint16_t rounded = (uint16_t)(value + 0.5); // no decimals, rounded

  if (rounded < 1000) lcd.print('0');
  if (rounded < 100)  lcd.print('0');
  if (rounded < 10)   lcd.print('0');

  lcd.print(rounded);
}

void printTwoDigits(uint8_t value)
{
  if (value < 10) lcd.print('0');
  lcd.print(value);
}

void drawLdrScreen()
{
  // Row 1 example: "LDR:1023 100.0%"
  lcd.setCursor(0, 0);
  lcd.print(F("LDR:"));
  lcd.print(ldrRaw);
  lcd.print(' ');
  lcd.print(ldrPct, 1);
  lcd.print('%');

  // Row 2 uses exactly 16 characters: "DD/MM/YYYY HH:MM".
  lcd.setCursor(0, 1);
  if (!displayTimeValid) {
    lcd.print(F("RTC indisponivel"));
    return;
  }

  printTwoDigits(displayTime.Day);
  lcd.print('/');
  printTwoDigits(displayTime.Month);
  lcd.print('/');
  lcd.print(tmYearToCalendar(displayTime.Year));
  lcd.print(' ');
  printTwoDigits(displayTime.Hour);
  lcd.print(':');
  printTwoDigits(displayTime.Minute);
}

void drawChannelRow(uint8_t row, uint8_t channelIndex, uint8_t channelNumber)
{
  lcd.setCursor(0, row);
  lcd.print(F("C"));
  lcd.print(channelNumber);
  lcd.print(' ');
  printFixed4_0(currentMA[channelIndex]);
  lcd.print(F("mA "));
  printFixed2_1(busVoltageV[channelIndex]);
  lcd.print(F("V"));
}

void drawDisplay()
{
  lcd.clear();

  if (screen == 0) {
    drawChannelRow(0, 0, 2);
    drawChannelRow(1, 1, 3);
  } else if (screen == 1) {
    drawChannelRow(0, 2, 4);
    lcd.setCursor(0, 1);
    lcd.print(F("Shunt "));
    lcd.print(shuntVoltageMV[2], 3);
    lcd.print(F("mV"));
  } else {
    drawLdrScreen();
  }
}

void displayTask()
{
  if (millis() - lastScreenSwitch >= screenInterval) {
    lastScreenSwitch = millis();
#if ENABLE_INA219
    screen = (screen + 1) % 3;
#else
    screen = 0;
#endif
    displayDirty = true;
  }

  if (displayDirty) {
    drawDisplay();
    displayDirty = false;
  }
}

#if ENABLE_INA219
void INA219_read(Adafruit_INA219 &sensor, float &shuntMV,
                 float &busV, float &currentMAValue)
{
  shuntMV = sensor.getShuntVoltage_mV();
  busV = sensor.getBusVoltage_V();
  currentMAValue = sensor.getCurrent_mA();

  Serial.print(F("shunt: "));
  Serial.print(shuntMV, 3);
  Serial.print(F(" mV | bus: "));
  Serial.print(busV, 3);
  Serial.print(F(" V | current: "));
  Serial.print(currentMAValue, 3);
  Serial.println(F(" mA"));
}
#endif

void Leitura(const tmElements_t &time)
{
  Serial.println(F("Iniciando leitura"));

#if ENABLE_INA219
  Serial.print(F("INA219 CH2 (0x41): "));
  INA219_read(ina219_ch2, shuntVoltageMV[0], busVoltageV[0], currentMA[0]);

  Serial.print(F("INA219 CH3 (0x44): "));
  INA219_read(ina219_ch3, shuntVoltageMV[1], busVoltageV[1], currentMA[1]);

  Serial.print(F("INA219 CH4 (0x45): "));
  INA219_read(ina219_ch4, shuntVoltageMV[2], busVoltageV[2], currentMA[2]);
#endif

  // LDR
  ldrRaw = analogRead(LDR_PIN);
  ldrVolt = (ldrRaw * 3.3) / 1023.0;        //voltage of arduino connection
  ldrPct = (ldrRaw * 100.0) / 1023.0;       //percentage
  Serial.print(F("LDR: "));
  Serial.print(ldrRaw);
  Serial.print(F(" | "));
  Serial.print(ldrVolt, 2);
  Serial.println(F(" V"));
  Serial.print(F(" | "));
  Serial.print(ldrPct, 1);
  Serial.println(F("%"));

  displayDirty = true;

  // SD FILE WRITE
  File dataFile = SD.open(DATA_FILE, FILE_WRITE);
  if (!dataFile)
  {
    Serial.println(F("Error opening file"));
    return;
  }

  // DATE
  if (time.Day < 10) dataFile.write('0');
  dataFile.print(time.Day);
  dataFile.write('/');
  if (time.Month < 10) dataFile.write('0');
  dataFile.print(time.Month);
  dataFile.write('/');
  dataFile.print(tmYearToCalendar(time.Year));
  dataFile.write(',');

  // TIME
  if (time.Hour < 10) dataFile.write('0');
  dataFile.print(time.Hour);
  dataFile.write(':');
  if (time.Minute < 10) dataFile.write('0');
  dataFile.print(time.Minute);
  dataFile.write(':');
  if (time.Second < 10) dataFile.write('0');
  dataFile.print(time.Second);
  dataFile.write(',');

#if ENABLE_INA219
  // INA219 channels 2, 3 and 4
  for (uint8_t i = 0; i < 3; i++)
  {
    dataFile.print(shuntVoltageMV[i], 3);
    dataFile.write(',');
    dataFile.print(busVoltageV[i], 3);
    dataFile.write(',');
    dataFile.print(currentMA[i], 3);
    dataFile.write(',');
  }
#endif
  //LDR
  dataFile.print(ldrRaw);
  dataFile.write(',');
  dataFile.print(ldrVolt, 2);
  dataFile.write(',');
  dataFile.print(ldrPct, 1);
  dataFile.println();
  dataFile.close();
}
