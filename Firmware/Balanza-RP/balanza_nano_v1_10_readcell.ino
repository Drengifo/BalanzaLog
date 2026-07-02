/*
  ============================================================
  BALANZA-LOG - VERSION 1.10
  Arduino Nano (ATmega328P)
  ============================================================

  PINOUT DEFINIDO:
  D5  -> HX711 DT
  D6  -> HX711 SCK
  D7  -> Driver actuador IN1
  D8  -> Driver actuador IN2
  D9  -> Boton (INPUT_PULLUP)
  D10 -> LED
  A4  -> OLED SDA
  A5  -> OLED SCL

  CAMBIOS V1.10:
  - no guarda muestras manuales
  - al activar modo automatico reinicia almacenamiento e IDX vuelve a 0
  - maximo 100 muestras guardadas
  - se mantiene toda la logica anterior
  - nuevo comando READCELL para leer la celda sin mover el actuador
*/

#include <HX711.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// ===================== PINOUT =====================
const uint8_t HX_DOUT = 5;
const uint8_t HX_SCK  = 6;
const uint8_t PIN_IN1 = 7;
const uint8_t PIN_IN2 = 8;
const uint8_t PIN_BTN = 9;
const uint8_t PIN_LED = 10;

// ===================== OLED =====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledOk = false;

// ===================== HX711 =====================
HX711 scale;

// AJUSTAR CON TU CALIBRACION REAL
float calibration_factor = 1677.0;

// ===================== TIEMPOS =====================
const unsigned long DISPLAY_TIMEOUT_MS      = 60000UL;
const unsigned long LONG_PRESS_MS           = 3000UL;
const unsigned long EDIT_TIMEOUT_MS         = 30000UL;
const unsigned long MULTICLICK_WINDOW_MS    = 700UL;
const unsigned long BLINK_INTERVAL_MS       = 400UL;

const unsigned long ACTUATOR_UP_TIME_MS     = 6000UL;
const unsigned long ACTUATOR_DOWN_TIME_MS   = 6000UL;

const unsigned long WAIT_BEFORE_TARE_MS     = 1000UL;
const unsigned long WAIT_AFTER_TARE_MS      = 1000UL;
const unsigned long WAIT_AFTER_UP_MS        = 1000UL;
const unsigned long MEASURE_WINDOW_MS       = 2000UL;

// ===================== ESTADOS =====================
enum MenuState {
  MENU_1 = 1,
  MENU_2 = 2,
  MENU_3 = 3
};

enum WeighStatus {
  STATUS_NONE = 0,
  STATUS_BAJANDO,
  STATUS_TARE,
  STATUS_SUBIENDO,
  STATUS_PESANDO
};

MenuState currentMenu = MENU_1;

bool autoMode = false;
bool displayOn = true;
bool actuatorIsDown = true;
bool isEditingInterval = false;
bool isWeighing = false;

float lastWeight = 0.0;
float plateWeight = 50.0;
uint16_t autoSampleCount = 0;
uint16_t intervalMinutes = 10;
uint16_t editIntervalMinutes = 10;

unsigned long nextAutoWeighMs = 0;
unsigned long lastInteractionMs = 0;
unsigned long lastEditActionMs = 0;

WeighStatus weighStatus = STATUS_NONE;

// ===================== BOTON =====================
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long lastDebounceMs = 0;
const unsigned long debounceDelayMs = 30;

unsigned long buttonPressStartMs = 0;
bool longPressHandled = false;

uint8_t menu1ClickCount = 0;
unsigned long menu1LastClickMs = 0;

// ===================== SERIAL =====================
char serialCmd[48];
uint8_t serialCmdPos = 0;

// ===================== EEPROM =====================
struct EepromHeader {
  uint16_t nextIndex;
  uint16_t storedCount;
};

struct SampleRecord {
  uint32_t timeSec;
  float weight;
  uint8_t automaticMode;
};

struct PlateCalibrationData {
  uint16_t signature;
  float plateWeight;
};

const uint16_t PLATE_SIGNATURE = 0xB10A;
const uint16_t MAX_SAVED_SAMPLES = 100;

EepromHeader eepromHeader;
int maxRecords = 0;
int plateDataAddr = 0;

// ============================================================
// ===================== FUNCIONES BASICAS =====================
// ============================================================

void ledPulseShort() {
  digitalWrite(PIN_LED, HIGH);
  delay(10);
  digitalWrite(PIN_LED, LOW);
}

void actuatorStop() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
}

// OJO: mantener esta logica tal como quedo buena
void actuatorDownStart() {
  digitalWrite(PIN_IN1, HIGH);
  digitalWrite(PIN_IN2, LOW);
}

void actuatorUpStart() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, HIGH);
}

void actuatorMoveDownFixed() {
  actuatorDownStart();
  delay(ACTUATOR_DOWN_TIME_MS);
  actuatorStop();
  actuatorIsDown = true;
}

void actuatorMoveUpFixed() {
  actuatorUpStart();
  delay(ACTUATOR_UP_TIME_MS);
  actuatorStop();
  actuatorIsDown = false;
}

void registerInteraction() {
  lastInteractionMs = millis();
  if (!displayOn) {
    displayOn = true;
    if (oledOk) {
      display.ssd1306_command(SSD1306_DISPLAYON);
    }
  }
}

void turnDisplayOff() {
  if (displayOn) {
    displayOn = false;
    if (oledOk) {
      display.ssd1306_command(SSD1306_DISPLAYOFF);
    }
  }
}

bool blinkVisible() {
  return ((millis() / BLINK_INTERVAL_MS) % 2) == 0;
}

void centerText(const char* txt, int y, uint8_t size) {
  if (!oledOk) return;

  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - (int)w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(txt);
}

const __FlashStringHelper* getStatusLabelF() {
  switch (weighStatus) {
    case STATUS_BAJANDO:  return F("Bajando");
    case STATUS_TARE:     return F("Tare");
    case STATUS_SUBIENDO: return F("Subiendo");
    case STATUS_PESANDO:  return F("Pesando");
    default:              return F("");
  }
}

void setWeighStatus(WeighStatus s) {
  weighStatus = s;
}

// Limpieza quirurgica de esquina inferior derecha
void clearBottomRightArtifacts() {
  if (!oledOk) return;
  display.fillRect(108, 54, 20, 10, SSD1306_BLACK);
}

// Limpieza completa de la franja inferior
void clearBottomBand() {
  if (!oledOk) return;
  display.fillRect(0, 54, 128, 10, SSD1306_BLACK);
}

// ============================================================
// ===================== EEPROM =====================
// ============================================================

void resetStoredSamples() {
  eepromHeader.nextIndex = 0;
  eepromHeader.storedCount = 0;
  EEPROM.put(0, eepromHeader);
}

void loadPlateWeightFromEEPROM() {
  PlateCalibrationData plateData;
  EEPROM.get(plateDataAddr, plateData);

  if (plateData.signature == PLATE_SIGNATURE &&
      !isnan(plateData.plateWeight) &&
      plateData.plateWeight >= 0.0 &&
      plateData.plateWeight < 5000.0) {
    plateWeight = plateData.plateWeight;
  } else {
    plateWeight = 50.0;
    plateData.signature = PLATE_SIGNATURE;
    plateData.plateWeight = plateWeight;
    EEPROM.put(plateDataAddr, plateData);
  }
}

void savePlateWeightToEEPROM(float value) {
  PlateCalibrationData plateData;
  plateData.signature = PLATE_SIGNATURE;
  plateData.plateWeight = value;
  EEPROM.put(plateDataAddr, plateData);
  plateWeight = value;
}

void initEepromStorage() {
  plateDataAddr = EEPROM.length() - (int)sizeof(PlateCalibrationData);
  maxRecords = (plateDataAddr - (int)sizeof(EepromHeader)) / (int)sizeof(SampleRecord);

  if (maxRecords > MAX_SAVED_SAMPLES) {
    maxRecords = MAX_SAVED_SAMPLES;
  }

  EEPROM.get(0, eepromHeader);

  if (eepromHeader.nextIndex >= maxRecords || eepromHeader.storedCount > maxRecords) {
    eepromHeader.nextIndex = 0;
    eepromHeader.storedCount = 0;
    EEPROM.put(0, eepromHeader);
  }

  loadPlateWeightFromEEPROM();
}

void saveSampleToEEPROM(float weight, bool automaticModeValue) {
  if (maxRecords <= 0) return;
  if (!automaticModeValue) return;

  SampleRecord rec;
  rec.timeSec = millis() / 1000UL;
  rec.weight = weight;
  rec.automaticMode = 1;

  int addr = sizeof(EepromHeader) + eepromHeader.nextIndex * (int)sizeof(SampleRecord);
  EEPROM.put(addr, rec);

  eepromHeader.nextIndex++;
  if (eepromHeader.nextIndex >= maxRecords) {
    eepromHeader.nextIndex = 0;
  }

  if (eepromHeader.storedCount < maxRecords) {
    eepromHeader.storedCount++;
  }

  EEPROM.put(0, eepromHeader);
}

void dumpSamplesFromEEPROM() {
  Serial.println(F("DUMP_BEGIN"));

  if (eepromHeader.storedCount == 0) {
    Serial.println(F("DUMP_EMPTY"));
    Serial.println(F("DUMP_END"));
    return;
  }

  for (uint16_t i = 0; i < eepromHeader.storedCount; i++) {
    int addr = sizeof(EepromHeader) + i * (int)sizeof(SampleRecord);

    SampleRecord rec;
    EEPROM.get(addr, rec);

    Serial.print(F("IDX="));
    Serial.print(i);
    Serial.print(F(",T_SEC="));
    Serial.print(rec.timeSec);
    Serial.print(F(",W_G="));
    Serial.print(rec.weight, 3);
    Serial.print(F(",AUTO="));
    Serial.println(rec.automaticMode);
  }

  Serial.println(F("DUMP_END"));
}

// ============================================================
// ===================== HX711 =====================
// ============================================================

void doTare() {
  if (!scale.is_ready()) {
    Serial.println(F("ERROR: HX711 NO DISPONIBLE"));
    return;
  }

  Serial.println(F("TARANDO..."));
  scale.tare(20);
  Serial.println(F("OK: TARA REALIZADA"));
}

float measureAverageFor2Seconds() {
  if (!scale.is_ready()) {
    Serial.println(F("ERROR: HX711 NO DISPONIBLE"));
    return NAN;
  }

  unsigned long t0 = millis();
  double sum = 0.0;
  uint16_t count = 0;

  while (millis() - t0 < MEASURE_WINDOW_MS) {
    float w = scale.get_units(1);
    sum += w;
    count++;
    ledPulseShort();
    delay(40);
  }

  if (count == 0) return NAN;
  return (float)(sum / count);
}

float readCellWithoutMoving() {
  float value = measureAverageFor2Seconds();

  if (isnan(value)) {
    Serial.println(F("ERROR: READCELL SIN DATOS"));
    return NAN;
  }

  lastWeight = value;
  Serial.print(F("CELL_G,"));
  Serial.println(value, 3);
  return value;
}

// ============================================================
// ===================== DISPLAY =====================
// ============================================================

void drawMenu1() {
  if (!displayOn || !oledOk) return;

  char weightBuf[16];

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  display.setTextSize(1);
  display.setCursor(88, 0);
  display.print(F("N="));
  display.print(autoSampleCount);

  dtostrf(lastWeight, 0, 3, weightBuf);

  display.setTextSize(2);
  display.setCursor(0, 22);
  display.print(weightBuf);
  display.print(F(" gr"));

  clearBottomBand();

  display.setTextSize(1);
  display.setCursor(0, 56);

  if (isWeighing) {
    display.print(getStatusLabelF());
  } else {
    display.print(F("auto = "));
    display.print(autoMode ? F("ON") : F("OFF"));
  }

  clearBottomRightArtifacts();
  display.display();
}

void drawMenu2() {
  if (!displayOn || !oledOk) return;

  char txt[16];
  char rem[18];

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  centerText("Tiempo pesaje auto", 2, 1);

  if (!(isEditingInterval && !blinkVisible())) {
    uint16_t shownValue = isEditingInterval ? editIntervalMinutes : intervalMinutes;
    itoa(shownValue, txt, 10);
    strcat(txt, " min");
    centerText(txt, 22, 2);
  }

  clearBottomBand();

  if (isEditingInterval) {
    centerText("Editando", 50, 1);
  } else {
    if (autoMode) {
      unsigned long remainingSec = 0;
      if (millis() < nextAutoWeighMs) {
        remainingSec = (nextAutoWeighMs - millis()) / 1000UL;
      }
      ltoa(remainingSec, rem, 10);
      strcat(rem, " s");
      centerText(rem, 50, 1);
    } else {
      centerText("Auto OFF", 50, 1);
    }
  }

  clearBottomRightArtifacts();
  display.display();
}

void drawMenu3() {
  if (!displayOn || !oledOk) return;

  char plateBuf[16];

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  centerText("Peso plato", 2, 1);

  dtostrf(plateWeight, 0, 3, plateBuf);

  display.setTextSize(2);
  display.setCursor(0, 22);
  display.print(plateBuf);
  display.print(F(" gr"));

  clearBottomBand();
  centerText("Mant. calibrar", 50, 1);

  clearBottomRightArtifacts();
  display.display();
}

void updateDisplay() {
  if (!displayOn || !oledOk) return;

  if (currentMenu == MENU_1) {
    drawMenu1();
  } else if (currentMenu == MENU_2) {
    drawMenu2();
  } else {
    drawMenu3();
  }
}

void showBootScreen() {
  if (!displayOn || !oledOk) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  display.setTextSize(2);
  display.setCursor(16, 10);
  display.print(F("Balanza"));

  display.setCursor(38, 30);
  display.print(F("Log"));

  clearBottomBand();
  display.setTextSize(1);
  display.setCursor(35, 54);
  display.print(F("Iniciando"));

  clearBottomRightArtifacts();
  display.display();
}

// ============================================================
// ===================== PESAJE =====================
// ============================================================

float runWeighRoutine(bool fromAutomaticMode) {
  isWeighing = true;
  registerInteraction();

  Serial.println(F("WEIGH: START"));

  currentMenu = MENU_1;
  setWeighStatus(STATUS_BAJANDO);
  updateDisplay();

  if (!actuatorIsDown) {
    Serial.println(F("WEIGH: BAJANDO PARA ASEGURAR POSICION"));
    actuatorMoveDownFixed();
  }

  delay(WAIT_BEFORE_TARE_MS);

  setWeighStatus(STATUS_TARE);
  updateDisplay();
  doTare();

  delay(WAIT_AFTER_TARE_MS);

  setWeighStatus(STATUS_SUBIENDO);
  updateDisplay();
  Serial.println(F("WEIGH: SUBIENDO"));
  actuatorMoveUpFixed();

  delay(WAIT_AFTER_UP_MS);

  setWeighStatus(STATUS_PESANDO);
  updateDisplay();
  Serial.println(F("WEIGH: MIDIENDO"));
  float gross = measureAverageFor2Seconds();

  if (isnan(gross)) {
    Serial.println(F("WEIGH: ERROR DE LECTURA"));
  } else {
    lastWeight = gross - plateWeight;

    if (fromAutomaticMode) {
      saveSampleToEEPROM(lastWeight, true);
      autoSampleCount++;
    }

    Serial.print(F("WEIGHT_G,"));
    Serial.println(lastWeight, 3);
  }

  setWeighStatus(STATUS_BAJANDO);
  updateDisplay();
  Serial.println(F("WEIGH: BAJANDO"));
  actuatorMoveDownFixed();

  Serial.println(F("WEIGH: END"));

  setWeighStatus(STATUS_NONE);
  isWeighing = false;
  registerInteraction();
  updateDisplay();

  return lastWeight;
}

float runPlateCalibrationRoutine() {
  isWeighing = true;
  registerInteraction();

  Serial.println(F("PLATE_CAL: START"));

  currentMenu = MENU_1;
  setWeighStatus(STATUS_BAJANDO);
  updateDisplay();

  if (!actuatorIsDown) {
    actuatorMoveDownFixed();
  }

  delay(WAIT_BEFORE_TARE_MS);

  setWeighStatus(STATUS_TARE);
  updateDisplay();
  doTare();

  delay(WAIT_AFTER_TARE_MS);

  setWeighStatus(STATUS_SUBIENDO);
  updateDisplay();
  actuatorMoveUpFixed();

  delay(WAIT_AFTER_UP_MS);

  setWeighStatus(STATUS_PESANDO);
  updateDisplay();
  float measuredPlate = measureAverageFor2Seconds();

  if (!isnan(measuredPlate) && measuredPlate >= 0.0) {
    savePlateWeightToEEPROM(measuredPlate);
    Serial.print(F("PLATE_G,"));
    Serial.println(plateWeight, 3);
  } else {
    Serial.println(F("PLATE_CAL: ERROR DE LECTURA"));
  }

  setWeighStatus(STATUS_BAJANDO);
  updateDisplay();
  actuatorMoveDownFixed();

  setWeighStatus(STATUS_NONE);
  isWeighing = false;
  currentMenu = MENU_3;
  registerInteraction();
  updateDisplay();

  Serial.println(F("PLATE_CAL: END"));
  return plateWeight;
}

// ============================================================
// ===================== MODO AUTO =====================
// ============================================================

void setAutoMode(bool enabled) {
  autoMode = enabled;

  if (autoMode) {
    autoSampleCount = 0;
    resetStoredSamples();
    nextAutoWeighMs = millis() + (unsigned long)intervalMinutes * 60000UL;
    Serial.println(F("AUTO: ON"));
  } else {
    Serial.println(F("AUTO: OFF"));
  }

  registerInteraction();
  updateDisplay();
}

void handleAutomaticMode() {
  if (!autoMode) return;
  if (isWeighing) return;
  if (isEditingInterval) return;

  if (millis() >= nextAutoWeighMs) {
    runWeighRoutine(true);
    nextAutoWeighMs = millis() + (unsigned long)intervalMinutes * 60000UL;
  }
}

// ============================================================
// ===================== MENU / BOTON =====================
// ============================================================

void enterIntervalEdit() {
  isEditingInterval = true;
  editIntervalMinutes = intervalMinutes;
  lastEditActionMs = millis();

  Serial.println(F("EDIT_INTERVAL: START"));
  registerInteraction();
  updateDisplay();
}

void confirmIntervalEdit() {
  intervalMinutes = editIntervalMinutes;
  isEditingInterval = false;

  if (autoMode) {
    nextAutoWeighMs = millis() + (unsigned long)intervalMinutes * 60000UL;
  }

  Serial.print(F("EDIT_INTERVAL: CONFIRM -> "));
  Serial.println(intervalMinutes);

  registerInteraction();
  updateDisplay();
}

void cancelIntervalEdit() {
  isEditingInterval = false;
  editIntervalMinutes = intervalMinutes;

  Serial.println(F("EDIT_INTERVAL: CANCEL"));
  registerInteraction();
  updateDisplay();
}

void handleShortPress() {
  if (!displayOn) {
    registerInteraction();
    updateDisplay();
    return;
  }

  registerInteraction();

  if (isEditingInterval) {
    editIntervalMinutes += 5;
    if (editIntervalMinutes > 120) {
      editIntervalMinutes = 5;
    }
    lastEditActionMs = millis();
    Serial.print(F("EDIT_INTERVAL: "));
    Serial.println(editIntervalMinutes);
    updateDisplay();
    return;
  }

  if (currentMenu == MENU_1) {
    menu1ClickCount++;
    menu1LastClickMs = millis();
    return;
  }

  if (currentMenu == MENU_2) {
    currentMenu = MENU_3;
    updateDisplay();
    return;
  }

  if (currentMenu == MENU_3) {
    currentMenu = MENU_1;
    updateDisplay();
    return;
  }
}

void handleLongPress() {
  registerInteraction();

  if (isEditingInterval) {
    confirmIntervalEdit();
    return;
  }

  if (currentMenu == MENU_1) {
    setAutoMode(!autoMode);
    return;
  }

  if (currentMenu == MENU_2) {
    enterIntervalEdit();
    return;
  }

  if (currentMenu == MENU_3) {
    runPlateCalibrationRoutine();
    return;
  }
}

void evaluatePendingMenu1Clicks() {
  if (menu1ClickCount == 0) return;
  if (millis() - menu1LastClickMs < MULTICLICK_WINDOW_MS) return;

  uint8_t clicks = menu1ClickCount;
  menu1ClickCount = 0;

  if (clicks >= 3) {
    runWeighRoutine(false);
  } else if (clicks == 1) {
    currentMenu = MENU_2;
    updateDisplay();
  }
}

void handleEditTimeout() {
  if (isEditingInterval) {
    if (millis() - lastEditActionMs >= EDIT_TIMEOUT_MS) {
      cancelIntervalEdit();
    }
  }
}

void updateButton() {
  bool reading = digitalRead(PIN_BTN);

  if (reading != lastButtonReading) {
    lastDebounceMs = millis();
  }

  if ((millis() - lastDebounceMs) > debounceDelayMs) {
    if (reading != stableButtonState) {
      stableButtonState = reading;

      if (stableButtonState == LOW) {
        buttonPressStartMs = millis();
        longPressHandled = false;
      } else {
        if (!longPressHandled) {
          handleShortPress();
        }
      }
    }
  }

  if (stableButtonState == LOW && !longPressHandled) {
    if (millis() - buttonPressStartMs >= LONG_PRESS_MS) {
      longPressHandled = true;
      handleLongPress();
    }
  }

  lastButtonReading = reading;
}

// ============================================================
// ===================== UART =====================
// ============================================================

void printStatus() {
  Serial.println(F("=== STATUS ==="));
  Serial.print(F("LAST_WEIGHT_G="));
  Serial.println(lastWeight, 3);

  Serial.print(F("PLATE_WEIGHT_G="));
  Serial.println(plateWeight, 3);

  Serial.print(F("AUTO="));
  Serial.println(autoMode ? F("ON") : F("OFF"));

  Serial.print(F("AUTO_SAMPLE_COUNT="));
  Serial.println(autoSampleCount);

  Serial.print(F("INTERVAL_MIN="));
  Serial.println(intervalMinutes);

  Serial.print(F("ACTUATOR_IS_DOWN="));
  Serial.println(actuatorIsDown ? F("1") : F("0"));

  Serial.print(F("DISPLAY_ON="));
  Serial.println(displayOn ? F("1") : F("0"));

  Serial.print(F("OLED_OK="));
  Serial.println(oledOk ? F("1") : F("0"));

  Serial.print(F("EEPROM_STORED="));
  Serial.println(eepromHeader.storedCount);

  if (autoMode) {
    unsigned long rem = 0;
    if (millis() < nextAutoWeighMs) {
      rem = (nextAutoWeighMs - millis()) / 1000UL;
    }
    Serial.print(F("NEXT_AUTO_SEC="));
    Serial.println(rem);
  }

  Serial.println(F("=============="));
}

void printHelp() {
  Serial.println(F("=== COMANDOS UART ==="));
  Serial.println(F("HELP            -> ayuda"));
  Serial.println(F("STATUS          -> estado general"));
  Serial.println(F("WEIGH           -> ejecutar pesaje manual"));
  Serial.println(F("AUTO ON         -> activar modo automatico"));
  Serial.println(F("AUTO OFF        -> desactivar modo automatico"));
  Serial.println(F("SETINT x        -> intervalo automatico en min (1..120)"));
  Serial.println(F("GETINT          -> leer intervalo automatico"));
  Serial.println(F("LAST            -> ultimo peso"));
  Serial.println(F("PLATE           -> leer peso de plato"));
  Serial.println(F("CALPLATE        -> calibrar plato"));
  Serial.println(F("TARE            -> tare directo"));
  Serial.println(F("DUMP            -> enviar muestras guardadas"));
  Serial.println(F("READCELL        -> leer celda sin mover actuador"));
  Serial.println(F("ACT UP          -> subir actuador 6 s"));
  Serial.println(F("ACT DOWN        -> bajar actuador 6 s"));
  Serial.println(F("ACT STOP        -> detener actuador"));
  Serial.println(F("SCREEN ON       -> encender pantalla"));
  Serial.println(F("SCREEN OFF      -> apagar pantalla"));
  Serial.println(F("====================="));
}

void processSerialCommand(char* cmd) {
  while (*cmd == ' ') cmd++;

  if (strcasecmp(cmd, "HELP") == 0) {
    printHelp();
  }
  else if (strcasecmp(cmd, "STATUS") == 0) {
    printStatus();
  }
  else if (strcasecmp(cmd, "WEIGH") == 0) {
    runWeighRoutine(false);
  }
  else if (strcasecmp(cmd, "AUTO ON") == 0) {
    setAutoMode(true);
  }
  else if (strcasecmp(cmd, "AUTO OFF") == 0) {
    setAutoMode(false);
  }
  else if (strncasecmp(cmd, "SETINT ", 7) == 0) {
    int v = atoi(cmd + 7);
    if (v >= 1 && v <= 120) {
      intervalMinutes = (uint16_t)v;
      if (autoMode) {
        nextAutoWeighMs = millis() + (unsigned long)intervalMinutes * 60000UL;
      }
      Serial.print(F("OK: INTERVAL_MIN="));
      Serial.println(intervalMinutes);
      registerInteraction();
      updateDisplay();
    } else {
      Serial.println(F("ERROR: SETINT DEBE SER 1..120"));
    }
  }
  else if (strcasecmp(cmd, "GETINT") == 0) {
    Serial.print(F("INTERVAL_MIN="));
    Serial.println(intervalMinutes);
  }
  else if (strcasecmp(cmd, "LAST") == 0) {
    Serial.print(F("LAST_WEIGHT_G="));
    Serial.println(lastWeight, 3);
  }
  else if (strcasecmp(cmd, "PLATE") == 0) {
    Serial.print(F("PLATE_WEIGHT_G="));
    Serial.println(plateWeight, 3);
  }
  else if (strcasecmp(cmd, "CALPLATE") == 0) {
    runPlateCalibrationRoutine();
  }
  else if (strcasecmp(cmd, "TARE") == 0) {
    doTare();
  }
  else if (strcasecmp(cmd, "DUMP") == 0) {
    dumpSamplesFromEEPROM();
  }
  else if (strcasecmp(cmd, "READCELL") == 0) {
    readCellWithoutMoving();
  }
  else if (strcasecmp(cmd, "ACT UP") == 0) {
    Serial.println(F("ACT: UP"));
    actuatorMoveUpFixed();
  }
  else if (strcasecmp(cmd, "ACT DOWN") == 0) {
    Serial.println(F("ACT: DOWN"));
    actuatorMoveDownFixed();
  }
  else if (strcasecmp(cmd, "ACT STOP") == 0) {
    Serial.println(F("ACT: STOP"));
    actuatorStop();
  }
  else if (strcasecmp(cmd, "SCREEN ON") == 0) {
    displayOn = true;
    if (oledOk) {
      display.ssd1306_command(SSD1306_DISPLAYON);
    }
    registerInteraction();
    updateDisplay();
    Serial.println(F("SCREEN: ON"));
  }
  else if (strcasecmp(cmd, "SCREEN OFF") == 0) {
    turnDisplayOff();
    Serial.println(F("SCREEN: OFF"));
  }
  else if (strlen(cmd) > 0) {
    Serial.print(F("ERROR: COMANDO NO VALIDO -> "));
    Serial.println(cmd);
  }
}

// ============================================================
// ===================== SETUP / LOOP =====================
// ============================================================

void setup() {
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);

  actuatorStop();
  digitalWrite(PIN_LED, LOW);

  Serial.begin(115200);
  delay(200);

  Wire.begin();

  oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledOk) {
    Serial.println(F("ERROR: NO SE ENCONTRO OLED"));
  } else {
    display.clearDisplay();
    display.display();
    display.setTextWrap(false);
  }

  scale.begin(HX_DOUT, HX_SCK);
  scale.set_scale(calibration_factor);

  initEepromStorage();

  showBootScreen();
  Serial.println(F("BALANZA-LOG INICIANDO"));

  actuatorMoveDownFixed();

  delay(200);

  lastInteractionMs = millis();
  currentMenu = MENU_1;
  updateDisplay();

  Serial.println(F("BALANZA-LOG LISTA"));
  printHelp();
}

void loop() {
  while (Serial.available()) {
    char ch = Serial.read();

    if (ch == '\n' || ch == '\r') {
      if (serialCmdPos > 0) {
        serialCmd[serialCmdPos] = '\0';
        processSerialCommand(serialCmd);
        serialCmdPos = 0;
      }
    } else {
      if (serialCmdPos < sizeof(serialCmd) - 1) {
        serialCmd[serialCmdPos++] = ch;
      }
    }
  }

  updateButton();
  evaluatePendingMenu1Clicks();
  handleEditTimeout();
  handleAutomaticMode();

  if (displayOn && !isWeighing && !isEditingInterval) {
    if (millis() - lastInteractionMs >= DISPLAY_TIMEOUT_MS) {
      turnDisplayOff();
    }
  }

  if (displayOn && isEditingInterval) {
    updateDisplay();
  }
}