/**
- STM32 RFID + Password Door Lock System with Admin Control & EEPROM Storage
- Adapted specifically for Wokwi STM32 Blue Pill Simulation
*/

#include <SPI.h>
#include <MFRC522.h>
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <EEPROM.h>

// --- Pin Definitions (Matched to your Wokwi diagram) ---
constexpr uint8_t RFID_SS_PIN  = PA4; 
constexpr uint8_t RFID_RST_PIN = PB1;
constexpr uint8_t SERVO_PIN    = PA8;
constexpr uint8_t BUZZER_PIN   = PB0;
constexpr uint8_t RED_LED      = PA0;
constexpr uint8_t GREEN_LED    = PA1;

// --- EEPROM Mapping Config ---
#define EEPROM_INIT_FLAG_ADDR  0
#define EEPROM_INIT_VALUE      0xBB  // Sentinel value for initialization
#define EEPROM_PIN_ADDR        1     // Dynamic Password (up to 6 chars + \0)
#define EEPROM_DB_START        10    // Card database start address
#define MAX_SLOTS              10    // Support up to 10 RFID cards
#define SLOT_SIZE              8     // 1 byte length + 7 bytes UID

// --- Master Card Setup ---
const byte MASTER_UID[] = {0x01, 0x02, 0x03, 0x04}; // Blue Wokwi Card
const byte MASTER_UID_LEN = 4;

// --- Keypad Map Configuration (4x4 mapped to STM32 pins) ---
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {PB12, PB13, PB14, PB15};
byte colPins[COLS] = {PB8, PB9, PB10, PB11};

// --- Instantiate Modules ---
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo lockServo;

// --- System State Machine ---
enum SystemState {
  STATE_LOCKED,
  STATE_UNLOCKED,
  STATE_ADMIN_MENU,
  STATE_ADMIN_ADD,
  STATE_ADMIN_DEL,
  STATE_ADMIN_PIN
};

SystemState currentState = STATE_LOCKED;
unsigned long stateTimer = 0;
unsigned long lastRfidPoll = 0;

// Safe Memory Variables for STM32
char currentPin[7] = "1234"; 
char pinInput[7] = "";
byte pinIndex = 0;

// --- EXPLICIT FUNCTION PROTOTYPES (Fixes the "not declared" errors) ---
void enterStateLocked();
void enterStateUnlocked();
void enterStateAdminMenu();
void enterStateAdminAdd();
void enterStateAdminDel();
void enterStateAdminPin();
void updateLockedDisplay();
void updateAdminPinDisplay();
void triggerAccessDenied();
void playBeep(int duration, int frequency);
void playSuccessTone();
void playAlertTone();
void playDeleteTone();
void playAdminModeTone();
void initEEPROM();
void savePIN(const char* newPin);
int findCardSlot(byte *uid, byte len);
bool addCard(byte *uid, byte len);
bool deleteCard(byte *uid, byte len);
bool isCardAuthorized(byte *uid, byte len);
bool isMasterCard(byte *uid, byte len);

void setup() {
  Serial.begin(115200);
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  
  // CRITICAL FIX: Forces STM32 SPI Master Mode. Prevents 30% performance crash
  pinMode(RFID_SS_PIN, OUTPUT);
  digitalWrite(RFID_SS_PIN, HIGH);
  SPI.setMOSI(PA7);
  SPI.setMISO(PA6);
  SPI.setSCLK(PA5);
  SPI.begin();
  
  mfrc522.PCD_Init();
  
  Wire.begin();
  lcd.init();
  lcd.backlight();
  
  lockServo.attach(SERVO_PIN);
  
  // Initialize EEPROM and Load PIN
  initEEPROM();
  
  enterStateLocked();
}

void loop() {
  unsigned long now = millis();
  bool cardScanned = false;
  byte scannedUID[8];
  byte scannedUIDLen = 0;

  // 1. Process active RFID scans (Rate limited to prevent SPI crash)
  if (now - lastRfidPoll >= 150) {
    lastRfidPoll = now;
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      scannedUIDLen = mfrc522.uid.size;
      for (byte i = 0; i < scannedUIDLen; i++) {
        scannedUID[i] = mfrc522.uid.uidByte[i];
      }
      cardScanned = true;
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
    }
  }

  // 2. Process keypad entry
  char key = keypad.getKey();
  if (key != NO_KEY) {
    playBeep(80, 2000); // Key click audio feedback
  }

  // 3. Coordinate State-specific Actions
  switch (currentState) {
    
    case STATE_LOCKED:
      if (key != NO_KEY) {
        if (key >= '0' && key <= '9') {
          if (pinIndex < 6) {
            pinInput[pinIndex++] = key;
            pinInput[pinIndex] = '\0';
            updateLockedDisplay();
          }
        } else if (key == '*') {
          pinIndex = 0;
          pinInput[0] = '\0';
          updateLockedDisplay();
        } else if (key == '#') {
          if (strcmp(pinInput, currentPin) == 0) {
            enterStateUnlocked();
          } else {
            triggerAccessDenied();
          }
        }
      }
      
      if (cardScanned) {
        if (isMasterCard(scannedUID, scannedUIDLen)) {
          enterStateAdminMenu();
        } else if (isCardAuthorized(scannedUID, scannedUIDLen)) {
          enterStateUnlocked();
        } else {
          triggerAccessDenied();
        }
      }
      break;

    case STATE_UNLOCKED:
      if (now - stateTimer >= 3000) {
        enterStateLocked();
      }
      break;

    case STATE_ADMIN_MENU:
      if (key != NO_KEY) {
        if (key == '1') enterStateAdminAdd();
        else if (key == '2') enterStateAdminDel();
        else if (key == '3') enterStateAdminPin();
        else if (key == '*') enterStateLocked();
      }
      break;

    case STATE_ADMIN_ADD:
      if (key == '*') enterStateAdminMenu();
      
      if (cardScanned) {
        if (isMasterCard(scannedUID, scannedUIDLen)) {
          lcd.clear(); lcd.setCursor(0, 0); lcd.print("Cannot Add Master");
          playAlertTone(); delay(1500); enterStateAdminAdd();
        } else if (addCard(scannedUID, scannedUIDLen)) {
          lcd.clear(); lcd.setCursor(0, 0); lcd.print("Card Registered!");
          lcd.setCursor(0, 1); lcd.print("Added to EEPROM");
          playSuccessTone(); delay(1500); enterStateAdminMenu();
        } else {
          lcd.clear(); lcd.setCursor(0, 0);
          if (findCardSlot(scannedUID, scannedUIDLen) != -1) {
            lcd.print("Already Exists!");
          } else {
            lcd.print("Memory Full!");
          }
          playAlertTone(); delay(1500); enterStateAdminMenu();
        }
      }
      break;

    case STATE_ADMIN_DEL:
      if (key == '*') enterStateAdminMenu();
      
      if (cardScanned) {
        if (isMasterCard(scannedUID, scannedUIDLen)) {
          lcd.clear(); lcd.setCursor(0, 0); lcd.print("Cannot Del Master");
          playAlertTone(); delay(1500); enterStateAdminDel();
        } else if (deleteCard(scannedUID, scannedUIDLen)) {
          lcd.clear(); lcd.setCursor(0, 0); lcd.print("Card Revoked!");
          lcd.setCursor(0, 1); lcd.print("Access Deleted");
          playDeleteTone(); delay(1500); enterStateAdminMenu();
        } else {
          lcd.clear(); lcd.setCursor(0, 0); lcd.print("Card Not Found!");
          playAlertTone(); delay(1500); enterStateAdminMenu();
        }
      }
      break;

    case STATE_ADMIN_PIN:
      if (key != NO_KEY) {
        if (key >= '0' && key <= '9') {
          if (pinIndex < 6) {
            pinInput[pinIndex++] = key;
            pinInput[pinIndex] = '\0';
            updateAdminPinDisplay();
          }
        } else if (key == '*') {
          enterStateAdminMenu();
        } else if (key == '#') {
          if (pinIndex >= 4) {
            savePIN(pinInput);
            lcd.clear(); lcd.setCursor(0, 0); lcd.print("PIN Saved!");
            playSuccessTone(); delay(1500); enterStateAdminMenu();
          } else {
            lcd.clear(); lcd.setCursor(0, 0); lcd.print("Too Short! >=4");
            playAlertTone(); delay(1500); enterStateAdminPin();
          }
        }
      }
      break;
  }
  
  delay(10); // CRITICAL: Keeps Wokwi STM32 performance at 100%
}

// --- State Transitions ---

void enterStateLocked() {
  currentState = STATE_LOCKED;
  pinIndex = 0;
  pinInput[0] = '\0';
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  lockServo.write(0); 
  updateLockedDisplay();
}

void enterStateUnlocked() {
  currentState = STATE_UNLOCKED;
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);
  lockServo.write(90); 
  
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Access Granted!");
  lcd.setCursor(0, 1); lcd.print("Door Unlocked");
  
  playSuccessTone();
  stateTimer = millis(); 
}

void enterStateAdminMenu() {
  currentState = STATE_ADMIN_MENU;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("1:Add 2:Del 3:PIN");
  lcd.setCursor(0, 1); lcd.print("*:Exit Menu");
  playAdminModeTone();
}

void enterStateAdminAdd() {
  currentState = STATE_ADMIN_ADD;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Scan Card to Add");
  lcd.setCursor(0, 1); lcd.print("*:Cancel");
}

void enterStateAdminDel() {
  currentState = STATE_ADMIN_DEL;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Scan Card to Del");
  lcd.setCursor(0, 1); lcd.print("*:Cancel");
}

void enterStateAdminPin() {
  currentState = STATE_ADMIN_PIN;
  pinIndex = 0;
  pinInput[0] = '\0';
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("New PIN (4-6dig):");
  lcd.setCursor(0, 1); lcd.print("*:Cancel #:Save");
}

// --- Display Helpers ---

void updateLockedDisplay() {
  lcd.setCursor(0, 0);
  lcd.print("Scan Card...    ");
  lcd.setCursor(0, 1);
  lcd.print("PIN: ");
  for (int i = 0; i < 6; i++) {
    if (i < pinIndex) lcd.print('*');
    else lcd.print(' ');
  }
  lcd.print("      ");
}

void updateAdminPinDisplay() {
  lcd.setCursor(0, 1);
  lcd.print("PIN: ");
  lcd.print(pinInput);
  for (int i = pinIndex; i < 11; i++) lcd.print(' ');
}

void triggerAccessDenied() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Access Denied!");
  lcd.setCursor(0, 1); lcd.print("Wrong Card/Code");
  
  digitalWrite(RED_LED, LOW);
  for (int i = 0; i < 3; i++) {
    digitalWrite(RED_LED, HIGH);
    playBeep(200, 400); 
    digitalWrite(RED_LED, LOW);
    delay(100);
  }
  delay(1000);
  enterStateLocked();
}

// --- Database & EEPROM Operations ---

void initEEPROM() {
  if (EEPROM.read(EEPROM_INIT_FLAG_ADDR) != EEPROM_INIT_VALUE) {
    // Format EEPROM for the first time
    EEPROM.write(EEPROM_INIT_FLAG_ADDR, EEPROM_INIT_VALUE);
    
    char defPin[] = "1234";
    for (int i = 0; i < 4; i++) EEPROM.write(EEPROM_PIN_ADDR + i, defPin[i]);
    EEPROM.write(EEPROM_PIN_ADDR + 4, '\0');
    
    for (int slot = 0; slot < MAX_SLOTS; slot++) {
      EEPROM.write(EEPROM_DB_START + (slot * SLOT_SIZE), 0); 
    }
  } else {
    // Load PIN from EEPROM to currentPin array
    for (int i = 0; i < 6; i++) {
      char c = EEPROM.read(EEPROM_PIN_ADDR + i);
      currentPin[i] = c;
      if (c == '\0') break;
    }
    currentPin[6] = '\0';
  }
}

void savePIN(const char* newPin) {
  int i = 0;
  while (newPin[i] != '\0' && i < 6) {
    EEPROM.write(EEPROM_PIN_ADDR + i, newPin[i]);
    currentPin[i] = newPin[i]; // Update running memory
    i++;
  }
  EEPROM.write(EEPROM_PIN_ADDR + i, '\0');
  currentPin[i] = '\0';
}

int findCardSlot(byte *uid, byte len) {
  for (int slot = 0; slot < MAX_SLOTS; slot++) {
    int baseAddr = EEPROM_DB_START + (slot * SLOT_SIZE);
    byte storedLen = EEPROM.read(baseAddr);
    if (storedLen == len) {
      bool isMatch = true;
      for (int i = 0; i < len; i++) {
        if (EEPROM.read(baseAddr + 1 + i) != uid[i]) {
          isMatch = false; break;
        }
      }
      if (isMatch) return slot;
    }
  }
  return -1; 
}

bool addCard(byte *uid, byte len) {
  if (findCardSlot(uid, len) != -1) return false; 
  
  for (int slot = 0; slot < MAX_SLOTS; slot++) {
    int baseAddr = EEPROM_DB_START + (slot * SLOT_SIZE);
    if (EEPROM.read(baseAddr) == 0) { 
      EEPROM.write(baseAddr, len);
      for (int i = 0; i < len; i++) {
        EEPROM.write(baseAddr + 1 + i, uid[i]);
      }
      return true;
    }
  }
  return false; 
}

bool deleteCard(byte *uid, byte len) {
  int slot = findCardSlot(uid, len);
  if (slot == -1) return false; 
  
  EEPROM.write(EEPROM_DB_START + (slot * SLOT_SIZE), 0); 
  return true;
}

bool isCardAuthorized(byte *uid, byte len) {
  if (isMasterCard(uid, len)) return true;
  return (findCardSlot(uid, len) != -1);
}

bool isMasterCard(byte *uid, byte len) {
  if (len != MASTER_UID_LEN) return false;
  for (byte i = 0; i < len; i++) {
    if (uid[i] != MASTER_UID[i]) return false;
  }
  return true;
}

// --- Audio Generation ---

void playBeep(int duration, int frequency) { tone(BUZZER_PIN, frequency, duration); }
void playSuccessTone() { tone(BUZZER_PIN, 1200, 150); delay(150); tone(BUZZER_PIN, 1800, 300); }
void playAlertTone() { tone(BUZZER_PIN, 400, 400); delay(400); }
void playDeleteTone() { tone(BUZZER_PIN, 1500, 150); delay(150); tone(BUZZER_PIN, 900, 300); }
void playAdminModeTone() { tone(BUZZER_PIN, 1600, 100); delay(100); tone(BUZZER_PIN, 2000, 100); }