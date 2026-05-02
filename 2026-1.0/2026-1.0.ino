//-----libraries-------
#include <Adafruit_ADS1X15.h>
#include <DS1307RTC.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>
#include <Wire.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

Adafruit_ADS1115 ads1;
Adafruit_ADS1115 ads2;
//ADC units addresses
const uint8_t ADS1_ADDR = 0x48;
const uint8_t ADS2_ADDR = 0x49;
//ADC1 -> 01; ADC2 -> 02
const uint8_t PAIR_01 = 0;        //ADC1
const uint8_t PAIR_23 = 1;        //ADC2

//ADC
const uint16_t R[4] = {220, 220, 220, 220};   //ADC resistance
const float multiplier = 0.125;               //ADS1115 conversion factor mV/bit for GAIN_ONE

int16_t raw[4] = {0, 0, 0, 0};                // ADC counts
float mV[4]  = {0, 0, 0, 0};                  // voltage (mV)
float mA[4]  = {0, 0, 0, 0};                // curent (mA)

//setting pins
const uint8_t chipSelect = 10;        //sd card reader -> CS[D10]
const uint8_t LDR_PIN = A0;           //light sensor -> A0=D14

//LDR
uint16_t ldrRaw = 0;
float ldrVolt = 0.0;
float ldrPct = 0.0;                     //percentage

int8_t   lastMin = -1;
const char* DATA_FILE = "medicao.csv";

//display
uint8_t screen = 0;
unsigned long lastScreenSwitch = 0;
const unsigned long screenInterval = 4000; // 4 seconds

bool displayDirty = true;

void setup(){
  tmElements_t time;
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ola Bender");
  lcd.setCursor(0, 1);
  lcd.print("ligando sistema");


  Serial.println("ADC Range: +/- 4.096V ---- 1 Bit = 0.125mV");

//-----error checks----
  if (!ads1.begin(ADS1_ADDR))
  {
    Serial.println("Failed to initialize ADC1.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro ADC1");
    while (1);
  }
    if (!ads2.begin(ADS2_ADDR))
  {
    Serial.println("Failed to initialize ADC2.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro ADC2");
    while (1);
  }

  ads1.setGain(GAIN_ONE);
  ads2.setGain(GAIN_ONE);

  if (!SD.begin(chipSelect))
  {
    pinMode(10, OUTPUT);
    Serial.println("Card failed or not present.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro SD");
    while (1);
  }
  if (RTC.read(time))
  {lastMin = time.Minute;}
  else
  {
    Serial.println("Failed to read RTC.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro RTC");
    while (1);
  }
  //SD FILE setup
  File dataFile = SD.open(DATA_FILE, FILE_WRITE);
  if (!dataFile)
  {
    Serial.println("Error opening file");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro .csv");
    return;
  }
  if (dataFile.size() == 0)               //file empty
  {dataFile.println("date,time,raw1,mV1,mA1,raw2,mV2,mA2,raw3,mV3,mA3,raw4,mV4,mA4,ldrRaw,ldrVolt,ldrPct");}
  dataFile.close();
}

void loop()
{
  tmElements_t time;
  displayTask();

  if (RTC.read(time))
  {
    if(time.Minute != lastMin)
    {
      lastMin = time.Minute;
      Leitura(time); 
    }
  }
  else
  {Serial.println("Failed to read RTC");}
}
void printFixed2_1(float value)
{
  if (value < 0) {
    value = 0;
  }

  if (value > 99.9) {
    lcd.print("99,9");
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
    lcd.print("9999");
    return;
  }

  uint16_t rounded = (uint16_t)(value + 0.5); // no decimals, rounded

  if (rounded < 1000) lcd.print('0');
  if (rounded < 100)  lcd.print('0');
  if (rounded < 10)   lcd.print('0');

  lcd.print(rounded);
}

void drawDisplay()
{
  lcd.clear();

  uint8_t chA = screen * 2;
  uint8_t chB = chA + 1;

  lcd.setCursor(0, 0);
  lcd.print("C");
  lcd.print(chA + 1);
  lcd.print(" ");
  printFixed4_0(mA[chA]);        
  lcd.print("mA ");
  printFixed2_1(mV[chA] / 1000.0); 
  lcd.print("V");

  lcd.setCursor(0, 1);
  lcd.print("C");
  lcd.print(chB + 1);
  lcd.print(" ");
  printFixed4_0(mA[chB]);     
  lcd.print("mA ");
  printFixed2_1(mV[chB] / 1000.0); 
  lcd.print("V");
}

void displayTask()
{
  if (millis() - lastScreenSwitch >= screenInterval) {
    lastScreenSwitch = millis();
    screen = (screen + 1) % 2;
    displayDirty = true;
  }

  if (displayDirty) {
    drawDisplay();
    displayDirty = false;
  }
}

void ADC_read(Adafruit_ADS1115 &adc, uint8_t pairIndex, uint16_t resistor,
                 int16_t &raw, float &mV, float &mA)
{
  switch (pairIndex) {
      case PAIR_01:
        raw = adc.readADC_Differential_0_1();
        break;

      case PAIR_23:
        raw = adc.readADC_Differential_2_3();
        break;

      default:
        raw = 0;
        mV = 0;
        mA = 0;
        return;
    }
    mV = raw * multiplier;                     
    mA = mV / resistor;

    Serial.print(raw);
    Serial.print(" (");
    Serial.print(mA, 3);
    Serial.print(" mA) | ");
    Serial.print(mV, 1);
    Serial.println(" mV");
}

void Leitura(const tmElements_t &time)
{
  Serial.println("Iniciando leitura");

  //ADC 1 - CH 1
  Serial.print("ADS 1");
  Serial.print("CH 1: ");
  ADC_read(ads1, PAIR_01, R[0], raw[0], mV[0], mA[0]);

  //ADC 1 - CH 2
  Serial.print("ADS 1");
  Serial.print("CH 2: ");
  ADC_read(ads1, PAIR_23, R[1], raw[1], mV[1], mA[1]);

  //ADC 2 - CH 1
  Serial.print("ADS 2");
  Serial.print("CH 1: ");
  ADC_read(ads2, PAIR_01, R[2], raw[2], mV[2], mA[2]);

  //ADC 1 - CH 2
  Serial.print("ADS 2");
  Serial.print("CH 2: ");
  ADC_read(ads2, PAIR_23, R[3], raw[3], mV[3], mA[3]);

  //LDR
  ldrRaw = analogRead(LDR_PIN);
  ldrVolt = (ldrRaw * 5.0) / 1023.0;
  ldrPct = (ldrRaw * 100.0) / 1023.0;
  Serial.print("LDR: ");
  Serial.print(ldrRaw);
  Serial.print(" | ");
  Serial.print(ldrVolt, 2);
  Serial.println(" V");
  Serial.print(" | ");
  Serial.print(ldrPct, 1);
  Serial.println("%");

  displayDirty = true;

  //SD FILE WRITE
  File dataFile = SD.open(DATA_FILE, FILE_WRITE);
  if (!dataFile)
  {
    Serial.println("Error opening file");
    return;
  }

  //DATE
  if (time.Day < 10) dataFile.write('0');
  dataFile.print(time.Day);
  dataFile.write('/');
  if (time.Month < 10) dataFile.write('0');
  dataFile.print(time.Month);
  dataFile.write('/');
  dataFile.print(tmYearToCalendar(time.Year));
  dataFile.write(',');

  //TIME
  if (time.Hour < 10) dataFile.write('0');
  dataFile.print(time.Hour);
  dataFile.write(':');
  if (time.Minute < 10) dataFile.write('0');
  dataFile.print(time.Minute);
  dataFile.write(':');
  if (time.Second < 10) dataFile.write('0');
  dataFile.print(time.Second);
  dataFile.write(',');

  //ADC
  for (uint8_t i = 0; i < 4; i++)
  {
    dataFile.print(raw[i]);
    dataFile.write(',');
    dataFile.print(mV[i], 3);
    dataFile.write(',');
    dataFile.print(mA[i], 3);
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
