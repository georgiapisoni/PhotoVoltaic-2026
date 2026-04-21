//-----libraries-------
#include <Adafruit_ADS1X15.h>
#include <DS1307RTC.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>
#include <Wire.h>

Adafruit_ADS1115 ads;
LiquidCrystal_I2C lcd(0x27, 16, 2);

//setting pins
const uint8_t chipSelect = 10;        //sd card reader -> CS[D10]
const uint8_t LDR_PIN = A0;           //light sensor -> A0=D14
//SD
//---SPI - MISO[D12], MOSI[D11], SCK[D13], CS[D10]
//ADS
//---CH1 [AIN0]&[AIN1]
//---CH2 [AIN2]&[AIN3]

uint16_t ldrRaw = 0;
float ldrVolt = 0.0;
float ldrPct = 0.0;                     //percentage

const uint8_t R1 = 220, R2 = 220;      //load resistance
const float multiplier = 0.125;        //ADS1115 conversion factor mV/bit for GAIN_ONE

int16_t raw1, raw2;
float mV1 = 0, mV2 = 0;               //channel voltage
float mA1 = 0, mA2 = 0;               //channel current

int8_t   lastMin = -1;
const char* DATA_FILE = "medicao.csv";

void setup()
{
  tmElements_t time;
  Serial.begin(9600);

  Serial.println("ADC Range: +/- 4.096V ---- 1 Bit = 0.125mV");
  ads.setGain(GAIN_ONE);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ola Bender");
  lcd.setCursor(0, 1);
  lcd.print("ligando sistema");

//-----error checks----
  if (!ads.begin())
  {
    Serial.println("Failed to initialize ADS.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro ADS");
    while (1);
  }
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
  {dataFile.println("date,time,raw1,mV1,mA1,raw2,mV2,mA2,ldrRaw,ldrVolt,ldrPct");}
  dataFile.close();
}

void loop()
{
  tmElements_t time;
  if (RTC.read(time))
  {
    //Serial.print("Ultimo minuto: ");
    //Serial.print(lastMin);
    //Serial.print(" | Minuto atual: ");
    //Serial.println(time.Minute);
    if(time.Minute != lastMin)
    {
      lastMin = time.Minute;
      Leitura(time); 
    }
    delay(500);
  }
  else
  {Serial.println("Failed to read RTC");}
}

void displayUpt()
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("1:");
  lcd.print(mA1, 1);
  lcd.print("mA ");
  lcd.print(mV1, 1);
  lcd.print("mV");

  lcd.setCursor(0, 1);
  lcd.print("2:");
  lcd.print(mA2, 1);
  lcd.print("mA ");
  lcd.print(mV2, 1);
  lcd.print("mV");
}

void Leitura(const tmElements_t &time)
{
  Serial.println("Iniciando leitura");

  //CH 1
  raw1 = ads.readADC_Differential_0_1();
  mV1 = raw1 * multiplier;
  mA1 = mV1 / R1;
  Serial.print("CH 1: ");
  Serial.print(raw1);
  Serial.print(" (");
  Serial.print(mA1, 3);
  Serial.print(" mA) | ");
  Serial.print(mV1, 1);
  Serial.println(" mV");

  //CH 2
  raw2 = ads.readADC_Differential_2_3();
  mV2 = raw2 * multiplier;
  mA2 = mV2 / R2;
  Serial.print("CH 2: ");
  Serial.print(raw2);
  Serial.print(" (");
  Serial.print(mA2, 3);
  Serial.print(" mA) | ");
  Serial.print(mV2, 1);
  Serial.println(" mV");

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

  displayUpt();

  //SD FILE WRITE
  File dataFile = SD.open(DATA_FILE, FILE_WRITE);
  if (!dataFile)
  {
    Serial.println("Error opening file");
    return;
  }

  if (time.Day < 10) dataFile.write('0');
  dataFile.print(time.Day);
  dataFile.write('/');
  if (time.Month < 10) dataFile.write('0');
  dataFile.print(time.Month);
  dataFile.write('/');
  dataFile.print(tmYearToCalendar(time.Year));
  dataFile.write(',');

  if (time.Hour < 10) dataFile.write('0');
  dataFile.print(time.Hour);
  dataFile.write(':');
  if (time.Minute < 10) dataFile.write('0');
  dataFile.print(time.Minute);
  dataFile.write(':');
  if (time.Second < 10) dataFile.write('0');
  dataFile.print(time.Second);
  dataFile.write(',');

  dataFile.print(raw1);
  dataFile.write(',');
  dataFile.print(mV1, 1);
  dataFile.write(',');
  dataFile.print(mA1, 3);
  dataFile.write(',');
  dataFile.print(raw2);
  dataFile.write(',');
  dataFile.print(mV2, 1);
  dataFile.write(',');
  dataFile.print(mA2, 3);
  dataFile.write(',');
  dataFile.print(ldrRaw);
  dataFile.write(',');
  dataFile.print(ldrVolt, 2);
  dataFile.write(',');
  dataFile.print(ldrPct, 1);
  dataFile.println();
  dataFile.close();
}
