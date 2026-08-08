//-----libraries-------
#include <Adafruit_INA219.h>
#include <DS1307RTC.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>
#include <Wire.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// I2C addresses: LCD 0x27, RTC 0x68, and one INA219 per channel.
const uint8_t INA219_CH1_ADDR = 0x40;
const uint8_t INA219_CH2_ADDR = 0x41;
const uint8_t INA219_CH3_ADDR = 0x44;
const uint8_t INA219_CH4_ADDR = 0x45;

Adafruit_INA219 ina219_ch1(INA219_CH1_ADDR);
Adafruit_INA219 ina219_ch2(INA219_CH2_ADDR);
Adafruit_INA219 ina219_ch3(INA219_CH3_ADDR);
Adafruit_INA219 ina219_ch4(INA219_CH4_ADDR);

float shuntVoltageMV[4] = {0, 0, 0, 0};       // voltage across INA219 shunt (mV)
float busVoltageV[4] = {0, 0, 0, 0};          // bus voltage relative to GND (V)
float currentMA[4] = {0, 0, 0, 0};            // measured current (mA)

// setting pins
const uint8_t chipSelect = 10;        //sd card reader -> CS[D10]
const uint8_t LDR_PIN = A0;           //light sensor -> A0=D14

// LDR
uint16_t ldrRaw = 0;
float ldrVolt = 0.0;
float ldrPct = 0.0;                     //percentage

int8_t   lastMin = -1;
const char* DATA_FILE = "ina219.csv";
tmElements_t displayTime;
bool displayTimeValid = false;

// display
uint8_t screen = 0;
unsigned long lastScreenSwitch = 0;
const unsigned long screenInterval = 4000; // 4 seconds

bool displayDirty = true;

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

void setup(){
  tmElements_t time;
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Ola Bender"));
  lcd.setCursor(0, 1);
  lcd.print(F("ligando sistema"));


  Serial.println(F("4-channel INA219 monitor"));
  Serial.flush();

//-----error checks----
  showStartupStep(F("Testing INA219 CH1 at 0x40..."), F("Test INA CH1"));
  if (!ina219_ch1.begin())
  {
    Serial.println(F("Failed to initialize INA219 CH1 at 0x40."));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH1"));
    while (1);
  }
  showStartupStep(F("INA219 CH1 OK. Testing CH2 at 0x41..."), F("Test INA CH2"));
  if (!ina219_ch2.begin())
  {
    Serial.println(F("Failed to initialize INA219 CH2 at 0x41."));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH2"));
    while (1);
  }
  showStartupStep(F("INA219 CH2 OK. Testing CH3 at 0x44..."), F("Test INA CH3"));
  if (!ina219_ch3.begin())
  {
    Serial.println(F("Failed to initialize INA219 CH3 at 0x44."));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH3"));
    while (1);
  }
  showStartupStep(F("INA219 CH3 OK. Testing CH4 at 0x45..."), F("Test INA CH4"));
  if (!ina219_ch4.begin())
  {
    Serial.println(F("Failed to initialize INA219 CH4 at 0x45."));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH4"));
    while (1);
  }

  showStartupStep(F("All INA219 sensors OK. Testing SD..."), F("Testando SD"));
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
  {dataFile.println(F("date,time,shuntmV1,busV1,currentmA1,shuntmV2,busV2,currentmA2,shuntmV3,busV3,currentmA3,shuntmV4,busV4,currentmA4,ldrRaw,ldrVolt,ldrPct"));}
  dataFile.close();
  Serial.println(F("Setup complete."));
  Serial.flush();
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

void drawDisplay()
{
  lcd.clear();

  if (screen == 2) {
    drawLdrScreen();
    return;
  }

  uint8_t chA = screen * 2;
  uint8_t chB = chA + 1;

  lcd.setCursor(0, 0);
  lcd.print(F("C"));
  lcd.print(chA + 1);
  lcd.print(F(" "));
  printFixed4_0(currentMA[chA]);
  lcd.print(F("mA "));
  printFixed2_1(busVoltageV[chA]);
  lcd.print(F("V"));

  lcd.setCursor(0, 1);
  lcd.print(F("C"));
  lcd.print(chB + 1);
  lcd.print(F(" "));
  printFixed4_0(currentMA[chB]);
  lcd.print(F("mA "));
  printFixed2_1(busVoltageV[chB]);
  lcd.print(F("V"));
}

void displayTask()
{
  if (millis() - lastScreenSwitch >= screenInterval) {
    lastScreenSwitch = millis();
    screen = (screen + 1) % 3;
    displayDirty = true;
  }

  if (displayDirty) {
    drawDisplay();
    displayDirty = false;
  }
}

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

void Leitura(const tmElements_t &time)
{
  Serial.println(F("Iniciando leitura"));

  Serial.print(F("INA219 CH1 (0x40): "));
  INA219_read(ina219_ch1, shuntVoltageMV[0], busVoltageV[0], currentMA[0]);

  Serial.print(F("INA219 CH2 (0x41): "));
  INA219_read(ina219_ch2, shuntVoltageMV[1], busVoltageV[1], currentMA[1]);

  Serial.print(F("INA219 CH3 (0x44): "));
  INA219_read(ina219_ch3, shuntVoltageMV[2], busVoltageV[2], currentMA[2]);

  Serial.print(F("INA219 CH4 (0x45): "));
  INA219_read(ina219_ch4, shuntVoltageMV[3], busVoltageV[3], currentMA[3]);

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

  // ADC - INA219 channels
  for (uint8_t i = 0; i < 4; i++)
  {
    dataFile.print(shuntVoltageMV[i], 3);
    dataFile.write(',');
    dataFile.print(busVoltageV[i], 3);
    dataFile.write(',');
    dataFile.print(currentMA[i], 3);
    dataFile.write(',');
  }
  //LDR
  dataFile.print(ldrRaw);
  dataFile.write(',');
  dataFile.print(ldrVolt, 2);
  dataFile.write(',');
  dataFile.print(ldrPct, 1);
  dataFile.println();
  dataFile.close();
}
