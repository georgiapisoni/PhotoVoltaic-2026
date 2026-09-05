// Standalone timed MOSFET test.
// No Serial commands, I2C, RTC, LCD, INA219, or SD card are required.
// Each load and short MOSFET is activated one at a time.

const uint8_t SHORT_PINS[4] = {5, 4, 3, 2}; // CH1..CH4
const uint8_t LOAD_PINS[4]  = {9, 8, 7, 6}; // CH1..CH4
const uint8_t LED_PIN = 13;
const unsigned long HOLD_LOAD_MS  = 15000;
const unsigned long HOLD_SHORT_MS = 5000;
const unsigned long DEAD_TIME_MS  = 100;

void allOff()
{
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(SHORT_PINS[i], LOW);
    digitalWrite(LOAD_PINS[i], LOW);
  }
}

void testLoad(uint8_t channel)
{
  allOff();
  digitalWrite(LOAD_PINS[channel], HIGH);
  digitalWrite(LED_PIN, HIGH);
  delay(HOLD_LOAD_MS);
  digitalWrite(LOAD_PINS[channel], LOW);
  digitalWrite(LED_PIN, LOW);
  delay(DEAD_TIME_MS);
}

void testShort(uint8_t channel)
{
  allOff();
  delay(DEAD_TIME_MS);
  digitalWrite(SHORT_PINS[channel], HIGH);
  digitalWrite(LED_PIN, HIGH);
  delay(HOLD_SHORT_MS);
  digitalWrite(SHORT_PINS[channel], LOW);
  digitalWrite(LED_PIN, LOW);
  delay(DEAD_TIME_MS);
}

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(SHORT_PINS[i], OUTPUT);
    pinMode(LOAD_PINS[i], OUTPUT);
  }
  allOff();
}

void loop()
{
  for (uint8_t i = 0; i < 4; i++) testLoad(i);
  for (uint8_t i = 0; i < 4; i++) testShort(i);
  allOff();
  delay(5000);
}
