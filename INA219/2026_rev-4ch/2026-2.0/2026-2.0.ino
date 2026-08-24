//-----libraries-------
#define ENABLE_INA219   1 // INA219 measurement system enabled.
#define ENABLE_I2C_SCAN 0 // Set to 1 temporarily when diagnosing the I2C bus.

#include <Adafruit_INA219.h>
#include <DS1307RTC.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>
#include <Wire.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// I2C addresses: LCD 0x27, RTC 0x68, and INA219 channels 1-4.
const uint8_t INA219_CH1_ADDR = 0x40;
const uint8_t INA219_CH2_ADDR = 0x41;
const uint8_t INA219_CH3_ADDR = 0x44;
const uint8_t INA219_CH4_ADDR = 0x45;

Adafruit_INA219 ina219_ch1(INA219_CH1_ADDR);
Adafruit_INA219 ina219_ch2(INA219_CH2_ADDR);
Adafruit_INA219 ina219_ch3(INA219_CH3_ADDR);
Adafruit_INA219 ina219_ch4(INA219_CH4_ADDR);

float shuntVoltageMV[4]    = {0, 0, 0, 0};
float busVoltageV[4]       = {0, 0, 0, 0};
float currentMA[4]         = {0, 0, 0, 0};
float ocShuntVoltageMV[4]  = {0, 0, 0, 0};
float ocBusVoltageV[4]     = {0, 0, 0, 0};
float ocCurrentMA[4]       = {0, 0, 0, 0};
float iscShuntVoltageMV[4] = {0, 0, 0, 0};
float iscBusVoltageV[4]    = {0, 0, 0, 0};
float iscCurrentMA[4]      = {0, 0, 0, 0};

// built-in 0.1Ω shunt is in parallel with external 0.1Ω shunt.
// Equivalent resistance is 0.05Ω
// current result must be multiplied by 0.1 / 0.05 = 2.
const float CURRENT_SCALE = 2.0;

// setting pins
const uint8_t chipSelect = 10; //sd card reader -> CS[D10]
const uint8_t LDR_PIN    = A0; //light sensor -> A0=D14

// Array order is CH1, CH2, CH3, CH4.
const uint8_t MOSFET_GATE_PINS[4] = {5, 4, 3, 2};

// Series load MOSFET gates: HIGH connects the load, LOW opens the circuit.
const uint8_t LOAD_GATE_PINS[4]         = {9, 8, 7, 6};
const bool ALL_CHANNELS[4]              = {true, true, true, true};
const bool OC_CHANNEL_ENABLED[4]        = {true, true, true, true};
const unsigned long OC_SETTLE_MS        = 250;
const unsigned long ISC_SETTLE_MS       = 250;
const uint8_t MEASUREMENT_SAMPLE_COUNT  = 5;
const unsigned long SAMPLE_INTERVAL_MS  = 20;
const unsigned long MOSFET_DEAD_TIME_MS = 10;

// LDR
uint16_t ldrRaw = 0;
float ldrVolt   = 0.0;
float ldrPct    = 0.0; //percentage

int8_t   lastMin = -1;
#if ENABLE_INA219
const char* DATA_FILE = "pv_iv.csv";
#else
const char* DATA_FILE = "test.csv";
#endif
tmElements_t displayTime;
bool displayTimeValid = false;

// display
uint8_t screen                     = 0;
unsigned long lastScreenSwitch     = 0;
const unsigned long screenInterval = 4000; // 4 seconds

bool displayDirty = true;

void Leitura(const tmElements_t &time);
#if ENABLE_INA219
void INA219_read(Adafruit_INA219 &sensor, float &shuntMV,
                 float &busV, float &currentMAValue);
void printINAValues(float shuntMV, float busV, float currentMAValue);
void printMeasurements(const __FlashStringHelper *label,
                       const float shuntMV[4], const float busV[4],
                       const float currentValueMA[4],
                       const bool enabledChannels[4]);
void averageINA219Readings(float shuntMV[4], float busV[4],
                           float currentValueMA[4],
                           const bool enabledChannels[4]);
void writeCsvHeader(File &dataFile);
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

#if ENABLE_I2C_SCAN
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
#endif

void setShortCircuit(bool enabled)
{
  if (enabled) {
    // Disconnect every load before enabling its parallel shorting MOSFET.
    for (uint8_t i = 0; i < 4; i++) {
      digitalWrite(MOSFET_GATE_PINS[i], LOW);
      digitalWrite(LOAD_GATE_PINS[i], LOW);
    }
    delay(MOSFET_DEAD_TIME_MS);
    for (uint8_t i = 0; i < 4; i++) {
      digitalWrite(MOSFET_GATE_PINS[i], HIGH);
    }
  } else {
    // Remove every short before reconnecting the corresponding load.
    for (uint8_t i = 0; i < 4; i++) {
      digitalWrite(MOSFET_GATE_PINS[i], LOW);
    }
    delay(MOSFET_DEAD_TIME_MS);
    for (uint8_t i = 0; i < 4; i++) {
      digitalWrite(LOAD_GATE_PINS[i], HIGH);
    }
  }

  Serial.println(enabled ? F("Short circuit ON; loads OFF")
                         : F("Short circuit OFF; loads ON"));
  Serial.flush();
}

void setLoadsConnected(bool connected)
{
  for (uint8_t i = 0; i < 4; i++) {
    bool loadConnected = OC_CHANNEL_ENABLED[i] ? connected : true;
    digitalWrite(LOAD_GATE_PINS[i], loadConnected ? HIGH : LOW);
  }

  Serial.println(connected ? F("CH1 load connected")
                           : F("CH1 load disconnected (open circuit)"));
  Serial.flush();
}

void setup(){
  tmElements_t time;
  Serial.begin(9600);

  // LOW is the safe/normal loaded state for the parallel IRLZ44N switches.
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(MOSFET_GATE_PINS[i], LOW);
    pinMode(MOSFET_GATE_PINS[i], OUTPUT);

    // HIGH is the normal state for the series load switches.
    digitalWrite(LOAD_GATE_PINS[i], HIGH);
    pinMode(LOAD_GATE_PINS[i], OUTPUT);
  }

  Wire.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Ola Bender"));
  lcd.setCursor(0, 1);
  lcd.print(F("ligando sistema"));


  Serial.println(F("PV monitor"));
  Serial.flush();

#if ENABLE_I2C_SCAN
  scanI2CBus();
#endif

//-----error checks----
#if ENABLE_INA219
  showStartupStep(F("INA CH1 test"), F("Test INA CH1"));
  if (!ina219_ch1.begin())
  {
    Serial.println(F("INA CH1 failed"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH1"));
    while (1);
  }

  showStartupStep(F("INA CH2 test"), F("Test INA CH2"));
  if (!ina219_ch2.begin())
  {
    Serial.println(F("INA CH2 failed"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH2"));
    while (1);
  }

  showStartupStep(F("INA CH3 test"), F("Test INA CH3"));
  if (!ina219_ch3.begin())
  {
    Serial.println(F("INA CH3 failed"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH3"));
    while (1);
  }

  showStartupStep(F("INA CH4 test"), F("Test INA CH4"));
  if (!ina219_ch4.begin())
  {
    Serial.println(F("INA CH4 failed"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro INA CH4"));
    while (1);
  }
#endif

  showStartupStep(F("SD test"), F("Testando SD"));
  if (!SD.begin(chipSelect))
  {
    pinMode(10, OUTPUT);
    Serial.println(F("SD failed"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro SD"));
    while (1);
  }
  showStartupStep(F("RTC test"), F("Testando RTC"));
  if (RTC.read(time))
  {
    lastMin = time.Minute;
    displayTime = time;
    displayTimeValid = true;
  }
  else
  {
    Serial.println(F("RTC failed"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Erro RTC"));
    while (1);
  }
  Serial.println(F("Opening CSV"));
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
    writeCsvHeader(dataFile);
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

void drawMeasurementRow(uint8_t row, uint8_t channelIndex, uint8_t channelNumber,
                        const __FlashStringHelper *label,
                        const float currentValues[4], const float busValues[4],
                        bool measurementEnabled = true)
{
  lcd.setCursor(0, row);
  lcd.print(label);
  lcd.print(channelNumber);
  if (!measurementEnabled) {
    lcd.print(F(" DESATIVADO"));
    return;
  }
  lcd.print(' ');
  printFixed4_0(currentValues[channelIndex]);
  lcd.print(F("mA "));
  printFixed2_1(busValues[channelIndex]);
  lcd.print(F("V"));
}

void drawDisplay()
{
  lcd.clear();

  if (screen == 0) {
    drawMeasurementRow(0, 0, 1, F("LC"), currentMA, busVoltageV);
    drawMeasurementRow(1, 1, 2, F("LC"), currentMA, busVoltageV);
  } else if (screen == 1) {
    drawMeasurementRow(0, 2, 3, F("LC"), currentMA, busVoltageV);
    drawMeasurementRow(1, 3, 4, F("LC"), currentMA, busVoltageV);
  } else if (screen == 2) {
    // Combined result: short-circuit current and open-circuit voltage.
    drawMeasurementRow(0, 0, 1, F("C"), iscCurrentMA, ocBusVoltageV,
                       OC_CHANNEL_ENABLED[0]);
    drawMeasurementRow(1, 1, 2, F("C"), iscCurrentMA, ocBusVoltageV,
                       OC_CHANNEL_ENABLED[1]);
  } else if (screen == 3) {
    drawMeasurementRow(0, 2, 3, F("C"), iscCurrentMA, ocBusVoltageV,
                       OC_CHANNEL_ENABLED[2]);
    drawMeasurementRow(1, 3, 4, F("C"), iscCurrentMA, ocBusVoltageV,
                       OC_CHANNEL_ENABLED[3]);
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
                 float &busV, float &currentMAValue)
{
  shuntMV = sensor.getShuntVoltage_mV();
  busV = sensor.getBusVoltage_V();
  currentMAValue = sensor.getCurrent_mA() * CURRENT_SCALE;
}

void printMeasurements(const __FlashStringHelper *label,
                       const float shuntMV[4], const float busV[4],
                       const float currentValueMA[4],
                       const bool enabledChannels[4])
{
  for (uint8_t channel = 0; channel < 4; channel++) {
    if (!enabledChannels[channel]) continue;
    Serial.print(label);
    Serial.print(F(" CH"));
    Serial.print(channel + 1);
    Serial.print(F(": "));
    printINAValues(shuntMV[channel], busV[channel], currentValueMA[channel]);
  }
}

void averageINA219Readings(float shuntMV[4], float busV[4],
                           float currentValueMA[4],
                           const bool enabledChannels[4])
{
  for (uint8_t channel = 0; channel < 4; channel++) {
    shuntMV[channel] = 0;
    busV[channel] = 0;
    currentValueMA[channel] = 0;
  }

  for (uint8_t sample = 0; sample < MEASUREMENT_SAMPLE_COUNT; sample++) {
    float sampleShuntMV;
    float sampleBusV;
    float sampleCurrentMA;

    if (enabledChannels[0]) {
      INA219_read(ina219_ch1, sampleShuntMV, sampleBusV, sampleCurrentMA);
      shuntMV[0] += sampleShuntMV;
      busV[0] += sampleBusV;
      currentValueMA[0] += sampleCurrentMA;
    }

    if (enabledChannels[1]) {
      INA219_read(ina219_ch2, sampleShuntMV, sampleBusV, sampleCurrentMA);
      shuntMV[1] += sampleShuntMV;
      busV[1] += sampleBusV;
      currentValueMA[1] += sampleCurrentMA;
    }

    if (enabledChannels[2]) {
      INA219_read(ina219_ch3, sampleShuntMV, sampleBusV, sampleCurrentMA);
      shuntMV[2] += sampleShuntMV;
      busV[2] += sampleBusV;
      currentValueMA[2] += sampleCurrentMA;
    }

    if (enabledChannels[3]) {
      INA219_read(ina219_ch4, sampleShuntMV, sampleBusV, sampleCurrentMA);
      shuntMV[3] += sampleShuntMV;
      busV[3] += sampleBusV;
      currentValueMA[3] += sampleCurrentMA;
    }

    if (sample + 1 < MEASUREMENT_SAMPLE_COUNT) delay(SAMPLE_INTERVAL_MS);
  }

  for (uint8_t channel = 0; channel < 4; channel++) {
    if (!enabledChannels[channel]) continue;
    shuntMV[channel] /= MEASUREMENT_SAMPLE_COUNT;
    busV[channel] /= MEASUREMENT_SAMPLE_COUNT;
    currentValueMA[channel] /= MEASUREMENT_SAMPLE_COUNT;
  }
}

void writeHeaderGroup(File &dataFile, const __FlashStringHelper *prefix, uint8_t channel)
{
  dataFile.print(prefix);
  dataFile.print(F("ShuntmV"));
  dataFile.print(channel);
  dataFile.write(',');
  dataFile.print(prefix);
  dataFile.print(F("BusV"));
  dataFile.print(channel);
  dataFile.write(',');
  dataFile.print(prefix);
  dataFile.print(F("CurrentmA"));
  dataFile.print(channel);
  dataFile.write(',');
}

void writeCsvHeader(File &dataFile)
{
  dataFile.print(F("date,time,"));
  for (uint8_t channel = 1; channel <= 4; channel++) {
    writeHeaderGroup(dataFile, F("load"), channel);
    writeHeaderGroup(dataFile, F("oc"), channel);
    writeHeaderGroup(dataFile, F("isc"), channel);
  }
  dataFile.println(F("ldrRaw,ldrVolt,ldrPct"));
}
#endif

void Leitura(const tmElements_t &time)
{
  Serial.println(F("Starting loaded measurement"));

#if ENABLE_INA219
  INA219_read(ina219_ch1, shuntVoltageMV[0], busVoltageV[0], currentMA[0]);
  INA219_read(ina219_ch2, shuntVoltageMV[1], busVoltageV[1], currentMA[1]);
  INA219_read(ina219_ch3, shuntVoltageMV[2], busVoltageV[2], currentMA[2]);
  INA219_read(ina219_ch4, shuntVoltageMV[3], busVoltageV[3], currentMA[3]);
  printMeasurements(F("LOAD"), shuntVoltageMV, busVoltageV, currentMA,
                    ALL_CHANNELS);
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
  // Disconnect only the CH1 load, allow Voc to settle, average five
  // CH1 readings, and reconnect it before any Serial output.
  setLoadsConnected(false);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Medindo Voc"));
  lcd.setCursor(0, 1);
  lcd.print(F("5 amostras"));
  delay(OC_SETTLE_MS);

  averageINA219Readings(ocShuntVoltageMV, ocBusVoltageV, ocCurrentMA,
                        OC_CHANNEL_ENABLED);
  setLoadsConnected(true);

  Serial.println(F("Averaged Voc (5 samples)"));
  printMeasurements(F("VOC"), ocShuntVoltageMV, ocBusVoltageV, ocCurrentMA,
                    OC_CHANNEL_ENABLED);

  // Disconnect all loads, then enable their parallel shorting MOSFETs.
  // After settling, average several quiet samples, remove every short,
  // and reconnect all loads before printing.
  setShortCircuit(true);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Medindo Isc"));
  lcd.setCursor(0, 1);
  lcd.print(F("5 amostras"));
  delay(ISC_SETTLE_MS);

  averageINA219Readings(iscShuntVoltageMV, iscBusVoltageV, iscCurrentMA,
                        ALL_CHANNELS);
  setShortCircuit(false);

  Serial.println(F("Averaged Isc (5 samples)"));
  printMeasurements(F("ISC"), iscShuntVoltageMV, iscBusVoltageV, iscCurrentMA,
                    ALL_CHANNELS);
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

  // Loaded, open-circuit, and short-circuit values for channels 1-4.
  for (uint8_t i = 0; i < 4; i++)
  {
    dataFile.print(shuntVoltageMV[i], 3);
    dataFile.write(',');
    dataFile.print(busVoltageV[i], 3);
    dataFile.write(',');
    dataFile.print(currentMA[i], 3);
    dataFile.write(',');
    if (OC_CHANNEL_ENABLED[i]) {
      dataFile.print(ocShuntVoltageMV[i], 3);
      dataFile.write(',');
      dataFile.print(ocBusVoltageV[i], 3);
      dataFile.write(',');
      dataFile.print(ocCurrentMA[i], 3);
      dataFile.write(',');
    } else {
      // Preserve the three OC columns as empty fields while disabled.
      dataFile.write(',');
      dataFile.write(',');
      dataFile.write(',');
    }
    dataFile.print(iscShuntVoltageMV[i], 3);
    dataFile.write(',');
    dataFile.print(iscBusVoltageV[i], 3);
    dataFile.write(',');
    dataFile.print(iscCurrentMA[i], 3);
    dataFile.write(',');
  }

  //LDR
  dataFile.print(ldrRaw);
  dataFile.write(',');
  dataFile.print(ldrVolt, 2);
  dataFile.write(',');
  dataFile.print(ldrPct, 2);
  dataFile.println();
  dataFile.close();
}
