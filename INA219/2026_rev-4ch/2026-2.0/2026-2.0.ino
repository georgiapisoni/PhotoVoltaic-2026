//-----libraries-------
#define ENABLE_INA219 1  // INA219 measurement system enabled.
#define ENABLE_CH1 0     // Temporarily disabled due to a suspected hardware fault.

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

// I2C addresses: LCD 0x27, RTC 0x68, and INA219 channels 1-4.
#if ENABLE_INA219
const uint8_t INA219_CH1_ADDR = 0x40;
const uint8_t INA219_CH2_ADDR = 0x41;
const uint8_t INA219_CH3_ADDR = 0x44;
const uint8_t INA219_CH4_ADDR = 0x45;

#if ENABLE_CH1
Adafruit_INA219 ina219_ch1(INA219_CH1_ADDR);
#endif
Adafruit_INA219 ina219_ch2(INA219_CH2_ADDR);
Adafruit_INA219 ina219_ch3(INA219_CH3_ADDR);
Adafruit_INA219 ina219_ch4(INA219_CH4_ADDR);

float shuntVoltageMV[4] = {0, 0, 0, 0};
float busVoltageV[4] = {0, 0, 0, 0};
float currentMA[4] = {0, 0, 0, 0};
float iscShuntVoltageMV[4] = {0, 0, 0, 0};
float iscBusVoltageV[4] = {0, 0, 0, 0};
float iscCurrentMA[4] = {0, 0, 0, 0};

// The built-in 0.1 ohm shunt is paralleled with another 0.1 ohm shunt.
// Equivalent resistance is 0.05 ohm, so the library's default 0.1 ohm
// current result must be multiplied by 0.1 / 0.05 = 2.
const float CURRENT_SCALE = 2.0;
#endif

// setting pins
const uint8_t chipSelect = 10;        //sd card reader -> CS[D10]
const uint8_t LDR_PIN = A0;           //light sensor -> A0=D14
// Each IRLZ44N gate also requires a physical 10k resistor to its source.
// Array order is CH1, CH2, CH3, CH4.
const uint8_t MOSFET_GATE_PINS[4] = {5, 4, 3, 2};
const bool CHANNEL_ENABLED[4] = {ENABLE_CH1, true, true, true};
const unsigned long ISC_SETTLE_MS = 250;
const uint8_t ISC_SAMPLE_COUNT = 5;
const unsigned long ISC_SAMPLE_INTERVAL_MS = 20;

// LDR
uint16_t ldrRaw = 0;
float ldrVolt = 0.0;
float ldrPct = 0.0;                     //percentage

int8_t   lastMin = -1;
#if ENABLE_INA219
const char* DATA_FILE = "pv_isc.csv";
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
                 float &busV, float &currentMAValue, bool printResult = true);
void printINAValues(float shuntMV, float busV, float currentMAValue);
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

void setShortCircuit(bool enabled)
{
  uint8_t gateLevel = enabled ? HIGH : LOW;
  for (uint8_t i = 0; i < 4; i++) {
    // A disabled channel is always held LOW and can never be shorted.
    digitalWrite(MOSFET_GATE_PINS[i], CHANNEL_ENABLED[i] ? gateLevel : LOW);
  }

  Serial.println(enabled ? F("Short circuit ON") : F("Short circuit OFF"));
  Serial.flush();
}

void setup(){
  tmElements_t time;
  Serial.begin(9600);

  // LOW is the safe/normal loaded state for the parallel IRLZ44N switches.
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(MOSFET_GATE_PINS[i], LOW);
    pinMode(MOSFET_GATE_PINS[i], OUTPUT);
  }

  Wire.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Ola Bender"));
  lcd.setCursor(0, 1);
  lcd.print(F("ligando sistema"));


  Serial.println(F("4-channel INA219 + LDR/RTC/SD"));
  Serial.flush();

  scanI2CBus();

//-----error checks----
#if ENABLE_INA219
#if ENABLE_CH1
  showStartupStep(F("Testing INA219 CH1 at 0x40..."), F("Test INA CH1"));
  if (!ina219_ch1.begin())
  {
    Serial.println(F("Failed to initialize INA219 CH1 at 0x40."));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH1"));
    while (1);
  }
#else
  Serial.println(F("INA219 CH1 disabled; skipping 0x40."));
#endif

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
    dataFile.println(F("date,time,loadShuntmV1,loadBusV1,loadCurrentmA1,iscShuntmV1,iscBusV1,iscCurrentmA1,loadShuntmV2,loadBusV2,loadCurrentmA2,iscShuntmV2,iscBusV2,iscCurrentmA2,loadShuntmV3,loadBusV3,loadCurrentmA3,iscShuntmV3,iscBusV3,iscCurrentmA3,loadShuntmV4,loadBusV4,loadCurrentmA4,iscShuntmV4,iscBusV4,iscCurrentmA4,ldrRaw,ldrVolt,ldrPct"));
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
  lcd.print(ldrPct, 2);
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
  if (!CHANNEL_ENABLED[channelIndex]) {
    lcd.print(F("C"));
    lcd.print(channelNumber);
    lcd.print(F(" DESATIVADO"));
    return;
  }

  lcd.print(F("C"));
  lcd.print(channelNumber);
  lcd.print(' ');
  printFixed4_0(currentMA[channelIndex]);
  lcd.print(F("mA "));
  printFixed2_1(busVoltageV[channelIndex]);
  lcd.print(F("V"));
}

void drawIscRow(uint8_t row, uint8_t channelIndex, uint8_t channelNumber)
{
  lcd.setCursor(0, row);
  if (!CHANNEL_ENABLED[channelIndex]) {
    lcd.print(F("SC"));
    lcd.print(channelNumber);
    lcd.print(F(" DESATIVADO"));
    return;
  }

  lcd.print(F("SC"));
  lcd.print(channelNumber);
  lcd.print(' ');
  printFixed4_0(iscCurrentMA[channelIndex]);
  lcd.print(F("mA "));
  printFixed2_1(iscBusVoltageV[channelIndex]);
  lcd.print(F("V"));
}

void drawDisplay()
{
  lcd.clear();

  if (screen == 0) {
    drawChannelRow(0, 0, 1);
    drawChannelRow(1, 1, 2);
  } else if (screen == 1) {
    drawChannelRow(0, 2, 3);
    drawChannelRow(1, 3, 4);
  } else if (screen == 2) {
    drawIscRow(0, 0, 1);
    drawIscRow(1, 1, 2);
  } else if (screen == 3) {
    drawIscRow(0, 2, 3);
    drawIscRow(1, 3, 4);
  } else {
    drawLdrScreen();
  }
}

void displayTask()
{
  if (millis() - lastScreenSwitch >= screenInterval) {
    lastScreenSwitch = millis();
#if ENABLE_INA219
    screen = (screen + 1) % 5;
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
void printINAValues(float shuntMV, float busV, float currentMAValue)
{
  Serial.print(F("shunt: "));
  Serial.print(shuntMV, 3);
  Serial.print(F(" mV | bus: "));
  Serial.print(busV, 3);
  Serial.print(F(" V | current: "));
  Serial.print(currentMAValue, 3);
  Serial.println(F(" mA"));
}

void INA219_read(Adafruit_INA219 &sensor, float &shuntMV,
                 float &busV, float &currentMAValue, bool printResult)
{
  shuntMV = sensor.getShuntVoltage_mV();
  busV = sensor.getBusVoltage_V();
  currentMAValue = sensor.getCurrent_mA() * CURRENT_SCALE;

  if (printResult) printINAValues(shuntMV, busV, currentMAValue);
}
#endif

void Leitura(const tmElements_t &time)
{
  Serial.println(F("Starting loaded measurement"));

#if ENABLE_INA219
#if ENABLE_CH1
  Serial.print(F("INA219 CH1 (0x40): "));
  INA219_read(ina219_ch1, shuntVoltageMV[0], busVoltageV[0], currentMA[0]);
#endif

  Serial.print(F("INA219 CH2 (0x41): "));
  INA219_read(ina219_ch2, shuntVoltageMV[1], busVoltageV[1], currentMA[1]);

  Serial.print(F("INA219 CH3 (0x44): "));
  INA219_read(ina219_ch3, shuntVoltageMV[2], busVoltageV[2], currentMA[2]);

  Serial.print(F("INA219 CH4 (0x45): "));
  INA219_read(ina219_ch4, shuntVoltageMV[3], busVoltageV[3], currentMA[3]);
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
  Serial.print(ldrPct, 2);
  Serial.println(F("%"));

#if ENABLE_INA219
  // Briefly bypass each load with its parallel MOSFET. After settling,
  // average several quiet samples and remove the shorts before printing.
  setShortCircuit(true);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Medindo Isc"));
  lcd.setCursor(0, 1);
  lcd.print(F("5 amostras"));
  delay(ISC_SETTLE_MS);

  for (uint8_t channel = 0; channel < 4; channel++) {
    iscShuntVoltageMV[channel] = 0;
    iscBusVoltageV[channel] = 0;
    iscCurrentMA[channel] = 0;
  }

  for (uint8_t sample = 0; sample < ISC_SAMPLE_COUNT; sample++) {
    float sampleShuntMV;
    float sampleBusV;
    float sampleCurrentMA;

#if ENABLE_CH1
    INA219_read(ina219_ch1, sampleShuntMV, sampleBusV, sampleCurrentMA, false);
    iscShuntVoltageMV[0] += sampleShuntMV;
    iscBusVoltageV[0] += sampleBusV;
    iscCurrentMA[0] += sampleCurrentMA;
#endif

    INA219_read(ina219_ch2, sampleShuntMV, sampleBusV, sampleCurrentMA, false);
    iscShuntVoltageMV[1] += sampleShuntMV;
    iscBusVoltageV[1] += sampleBusV;
    iscCurrentMA[1] += sampleCurrentMA;

    INA219_read(ina219_ch3, sampleShuntMV, sampleBusV, sampleCurrentMA, false);
    iscShuntVoltageMV[2] += sampleShuntMV;
    iscBusVoltageV[2] += sampleBusV;
    iscCurrentMA[2] += sampleCurrentMA;

    INA219_read(ina219_ch4, sampleShuntMV, sampleBusV, sampleCurrentMA, false);
    iscShuntVoltageMV[3] += sampleShuntMV;
    iscBusVoltageV[3] += sampleBusV;
    iscCurrentMA[3] += sampleCurrentMA;

    if (sample + 1 < ISC_SAMPLE_COUNT) delay(ISC_SAMPLE_INTERVAL_MS);
  }

  for (uint8_t channel = 0; channel < 4; channel++) {
    if (!CHANNEL_ENABLED[channel]) continue;
    iscShuntVoltageMV[channel] /= ISC_SAMPLE_COUNT;
    iscBusVoltageV[channel] /= ISC_SAMPLE_COUNT;
    iscCurrentMA[channel] /= ISC_SAMPLE_COUNT;
  }

  setShortCircuit(false);

  Serial.println(F("Averaged short-circuit measurement (5 samples)"));
#if ENABLE_CH1
  Serial.print(F("INA219 CH1 Isc (0x40): "));
  printINAValues(iscShuntVoltageMV[0], iscBusVoltageV[0], iscCurrentMA[0]);
#endif
  Serial.print(F("INA219 CH2 Isc (0x41): "));
  printINAValues(iscShuntVoltageMV[1], iscBusVoltageV[1], iscCurrentMA[1]);
  Serial.print(F("INA219 CH3 Isc (0x44): "));
  printINAValues(iscShuntVoltageMV[2], iscBusVoltageV[2], iscCurrentMA[2]);
  Serial.print(F("INA219 CH4 Isc (0x45): "));
  printINAValues(iscShuntVoltageMV[3], iscBusVoltageV[3], iscCurrentMA[3]);
#endif
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
  // Loaded and short-circuit values for INA219 channels 1-4.
  for (uint8_t i = 0; i < 4; i++)
  {
    if (!CHANNEL_ENABLED[i]) {
      // Preserve the six CH1 CSV columns as empty fields while disabled.
      for (uint8_t field = 0; field < 6; field++) dataFile.write(',');
      continue;
    }

    dataFile.print(shuntVoltageMV[i], 3);
    dataFile.write(',');
    dataFile.print(busVoltageV[i], 3);
    dataFile.write(',');
    dataFile.print(currentMA[i], 3);
    dataFile.write(',');
    dataFile.print(iscShuntVoltageMV[i], 3);
    dataFile.write(',');
    dataFile.print(iscBusVoltageV[i], 3);
    dataFile.write(',');
    dataFile.print(iscCurrentMA[i], 3);
    dataFile.write(',');
  }
#endif
  //LDR
  dataFile.print(ldrRaw);
  dataFile.write(',');
  dataFile.print(ldrVolt, 2);
  dataFile.write(',');
  dataFile.print(ldrPct, 2);
  dataFile.println();
  dataFile.close();
}
