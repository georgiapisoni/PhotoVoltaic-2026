// Four-channel MOSFET serial test
// Commands at 9600 baud: S1 ON, S1 OFF, L1 ON, L1 OFF, ALL OFF, STATUS
// S = parallel short MOSFET; L = series load MOSFET.

const uint8_t SHORT_PINS[4] = {5, 4, 3, 2}; // CH1..CH4
const uint8_t LOAD_PINS[4]  = {9, 8, 7, 6}; // CH1..CH4
const unsigned long DEAD_TIME_MS = 10;
const bool MOSFET_OUTPUTS_ENABLED = false; // serial-only diagnostic mode

bool shortState[4] = {false, false, false, false};
bool loadState[4]  = {true, true, true, true};
String command;
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL_MS = 1000;

void printStatus()
{
  Serial.println(F("States (S=short, L=load):"));
  for (uint8_t i = 0; i < 4; i++) {
    Serial.print(F("CH"));
    Serial.print(i + 1);
    Serial.print(F(" S="));
    Serial.print(shortState[i] ? F("ON") : F("OFF"));
    Serial.print(F(" L="));
    Serial.println(loadState[i] ? F("ON") : F("OFF"));
  }
}

void setChannel(uint8_t channel, char type, bool enabled)
{
  if (channel >= 4) return;
  if (!MOSFET_OUTPUTS_ENABLED) {
    Serial.println(F("MOSFET outputs disabled; command not applied."));
    return;
  }

  if (type == 'S') {
    if (enabled) {
      digitalWrite(LOAD_PINS[channel], LOW);
      loadState[channel] = false;
      delay(DEAD_TIME_MS);
    }
    digitalWrite(SHORT_PINS[channel], enabled ? HIGH : LOW);
    shortState[channel] = enabled;
    if (!enabled) {
      delay(DEAD_TIME_MS);
      digitalWrite(LOAD_PINS[channel], HIGH);
      loadState[channel] = true;
    }
  } else if (type == 'L') {
    if (enabled) {
      digitalWrite(SHORT_PINS[channel], LOW);
      shortState[channel] = false;
      delay(DEAD_TIME_MS);
    }
    digitalWrite(LOAD_PINS[channel], enabled ? HIGH : LOW);
    loadState[channel] = enabled;
  }
}

void allOff()
{
  if (!MOSFET_OUTPUTS_ENABLED) {
    Serial.println(F("MOSFET outputs disabled; no pins changed."));
    return;
  }
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(SHORT_PINS[i], LOW);
    digitalWrite(LOAD_PINS[i], LOW);
    shortState[i] = false;
    loadState[i] = false;
  }
}

void printHelp()
{
  Serial.println(F("Commands: S1 ON/OFF, L1 ON/OFF (channels 1-4)"));
  Serial.println(F("          ALL OFF, ALL LOAD, STATUS, HELP"));
}

void processCommand(String text)
{
  text.trim();
  text.toUpperCase();
  if (text == F("STATUS")) { printStatus(); return; }
  if (text == F("HELP")) { printHelp(); return; }
  if (text == F("ALL OFF")) { allOff(); printStatus(); return; }
  if (text == F("ALL LOAD")) {
    for (uint8_t i = 0; i < 4; i++) setChannel(i, 'L', true);
    printStatus();
    return;
  }

  if (text.length() >= 4 && (text[0] == 'S' || text[0] == 'L') &&
      text[1] >= '1' && text[1] <= '4') {
    bool enabled = text.endsWith(F("ON"));
    bool disabled = text.endsWith(F("OFF"));
    if (enabled || disabled) {
      setChannel(text[1] - '1', text[0], enabled);
      printStatus();
      return;
    }
  }
  Serial.println(F("Unknown command."));
  printHelp();
}

void setup()
{
  Serial.begin(9600);
  if (MOSFET_OUTPUTS_ENABLED) {
    for (uint8_t i = 0; i < 4; i++) {
      pinMode(SHORT_PINS[i], OUTPUT);
      pinMode(LOAD_PINS[i], OUTPUT);
      digitalWrite(SHORT_PINS[i], LOW);
      digitalWrite(LOAD_PINS[i], HIGH);
    }
  }
  Serial.println(F("4-channel MOSFET serial test"));
  printHelp();
  printStatus();
}

void loop()
{
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeat = millis();
    Serial.println(F("alive"));
  }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (command.length() > 0) {
        processCommand(command);
        command = "";
      }
    } else if (command.length() < 24) {
      command += c;
    }
  }
}
