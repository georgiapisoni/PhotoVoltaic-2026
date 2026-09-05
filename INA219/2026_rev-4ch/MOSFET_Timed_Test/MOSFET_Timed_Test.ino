// Standalone timed MOSFET test.
// No Serial commands, I2C, RTC, LCD, INA219, or SD card are required.
// Each load and short MOSFET is activated one at a time.

const uint8_t SHORT_PINS[4] = {5, 4, 3, 2}; // CH1..CH4
const uint8_t LOAD_PINS[4]  = {9, 8, 7, 6}; // CH1..CH4
const uint8_t LED_PIN = 13;
const unsigned long HOLD_LOAD_MS  = 15000;
const unsigned long HOLD_SHORT_MS = 5000;
const unsigned long DEAD_TIME_MS  = 100;

void timedWait(unsigned long duration)
{
  unsigned long started = millis();
  unsigned long lastReport = started;
  while (millis() - started < duration) {
    if (millis() - lastReport >= 1000) {
      lastReport = millis();
      Serial.println(F("  test still running"));
    }
  }
}

void allOff()
{
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(SHORT_PINS[i], LOW);
    digitalWrite(LOAD_PINS[i], LOW);
  }
}

void testLoad(uint8_t channel)
{
  Serial.print(F("CH"));
  Serial.print(channel + 1);
  Serial.println(F(" LOAD ON (15 s)"));
  allOff();
  digitalWrite(LOAD_PINS[channel], HIGH);
  digitalWrite(LED_PIN, HIGH);
  timedWait(HOLD_LOAD_MS);
  digitalWrite(LOAD_PINS[channel], LOW);
  digitalWrite(LED_PIN, LOW);
  Serial.print(F("CH"));
  Serial.print(channel + 1);
  Serial.println(F(" LOAD OFF"));
  timedWait(DEAD_TIME_MS);
}

void testShort(uint8_t channel)
{
  Serial.print(F("CH"));
  Serial.print(channel + 1);
  Serial.println(F(" SHORT ON (5 s)"));
  allOff();
  timedWait(DEAD_TIME_MS);
  digitalWrite(SHORT_PINS[channel], HIGH);
  digitalWrite(LED_PIN, HIGH);
  timedWait(HOLD_SHORT_MS);
  digitalWrite(SHORT_PINS[channel], LOW);
  digitalWrite(LED_PIN, LOW);
  Serial.print(F("CH"));
  Serial.print(channel + 1);
  Serial.println(F(" SHORT OFF"));
  timedWait(DEAD_TIME_MS);
}

void setup()
{
  Serial.begin(9600);
  Serial.println(F("Timed MOSFET test starting"));
  Serial.println(F("Load stages: 15 s; short stages: 5 s"));
  pinMode(LED_PIN, OUTPUT);
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(SHORT_PINS[i], OUTPUT);
    pinMode(LOAD_PINS[i], OUTPUT);
  }
  allOff();
  Serial.println(F("All MOSFET outputs OFF"));
}

void loop()
{
  Serial.println(F("--- LOAD TESTS ---"));
  for (uint8_t i = 0; i < 4; i++) testLoad(i);
  Serial.println(F("--- SHORT TESTS ---"));
  for (uint8_t i = 0; i < 4; i++) testShort(i);
  allOff();
  Serial.println(F("Cycle complete; all MOSFET outputs OFF"));
  timedWait(5000);
}
