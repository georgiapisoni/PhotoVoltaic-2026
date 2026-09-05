// Interactive test for the CH1 load and short-circuit MOSFETs.
// This sketch does not initialize the INA219, LCD, RTC, or SD card.

const uint8_t SHORT_GATE_PIN = 5; // CH1 parallel short-circuit MOSFET
const uint8_t LOAD_GATE_PIN  = 9; // CH1 series load MOSFET

const unsigned long OPEN_CIRCUIT_TEST_MS  = 15000; // 15 seconds
const unsigned long SHORT_CIRCUIT_TEST_MS = 5000;  // 5 seconds maximum
const unsigned long MOSFET_DEAD_TIME_MS    = 10;    // break-before-make delay

enum TestState : uint8_t {
  LOADED,
  OPEN_CIRCUIT,
  SHORT_CIRCUIT
};

TestState state = LOADED;
unsigned long stateStartedAt = 0;
unsigned long lastCountdownAt = 0;

void printMenu()
{
  Serial.println(F("Commands:"));
  Serial.println(F("  o = CH1 open circuit for 15 seconds"));
  Serial.println(F("  s = CH1 short circuit for 5 seconds"));
  Serial.println(F("  l = return immediately to loaded state"));
  Serial.println(F("  ? = show this menu"));
}

void enterLoadedState()
{
  // Always remove the short before reconnecting the load.
  digitalWrite(SHORT_GATE_PIN, LOW);
  delay(MOSFET_DEAD_TIME_MS);
  digitalWrite(LOAD_GATE_PIN, HIGH);
  state = LOADED;
  Serial.println(F("STATE: LOADED (load ON, short OFF)"));
}

void enterOpenCircuitState()
{
  // The series MOSFET must be OFF to disconnect the load.
  digitalWrite(SHORT_GATE_PIN, LOW);
  digitalWrite(LOAD_GATE_PIN, LOW);
  state = OPEN_CIRCUIT;
  stateStartedAt = millis();
  lastCountdownAt = stateStartedAt;
  Serial.println(F("STATE: OPEN CIRCUIT (load OFF) for 15 seconds"));
}

void enterShortCircuitState()
{
  // Disconnect the load first so panel current can flow only through
  // the parallel short-circuit MOSFET during this test.
  digitalWrite(SHORT_GATE_PIN, LOW);
  digitalWrite(LOAD_GATE_PIN, LOW);
  delay(MOSFET_DEAD_TIME_MS);
  digitalWrite(SHORT_GATE_PIN, HIGH);
  state = SHORT_CIRCUIT;
  stateStartedAt = millis();
  lastCountdownAt = stateStartedAt;
  Serial.println(F("STATE: SHORT CIRCUIT (load OFF) for 5 seconds"));
}

void setup()
{
  Serial.begin(9600);

  // Establish safe output levels before changing the pin modes.
  digitalWrite(SHORT_GATE_PIN, LOW);
  pinMode(SHORT_GATE_PIN, OUTPUT);
  digitalWrite(LOAD_GATE_PIN, HIGH);
  pinMode(LOAD_GATE_PIN, OUTPUT);

  Serial.println(F("CH1 MOSFET test"));
  enterLoadedState();
  printMenu();
}

void loop()
{
  while (Serial.available() > 0) {
    char command = Serial.read();

    if (command == 'o' || command == 'O') {
      enterOpenCircuitState();
    } else if (command == 's' || command == 'S') {
      enterShortCircuitState();
    } else if (command == 'l' || command == 'L') {
      enterLoadedState();
    } else if (command == '?') {
      printMenu();
    }
  }

  if (state == OPEN_CIRCUIT &&
      millis() - stateStartedAt >= OPEN_CIRCUIT_TEST_MS) {
    Serial.println(F("Open-circuit test complete"));
    enterLoadedState();
  } else if (state == SHORT_CIRCUIT &&
             millis() - stateStartedAt >= SHORT_CIRCUIT_TEST_MS) {
    Serial.println(F("Short-circuit test complete"));
    enterLoadedState();
  }

  if (state != LOADED && millis() - lastCountdownAt >= 1000) {
    lastCountdownAt += 1000;
    Serial.print(F("Elapsed: "));
    Serial.print((millis() - stateStartedAt) / 1000);
    Serial.println(F(" s"));
  }
}
