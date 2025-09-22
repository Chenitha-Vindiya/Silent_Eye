// #define BLYNK_TEMPLATE_ID           "TMPL6pLUFnhEn"
// #define BLYNK_TEMPLATE_NAME         "Quickstart Template"
// #define BLYNK_AUTH_TOKEN            "04Q0xlBXm140X4lF7SW-pBYM-AaRgDdH"

#include <WiFi.h>
#include <WiFiClient.h>
// #include <BlynkSimpleEsp32.h> //Blynk
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include "RTClib.h"
#include <Keypad_I2C.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>

// ===== CONFIGURATION =====
// WiFi credentials
// const char* ssid = "YourWiFiSSID";
// const char* password = "YourWiFiPassword";

// IFTTT Webhooks for alerts
const char* ifttt_host = "maker.ifttt.com";
const char* ifttt_key = "YOUR_IFTTT_KEY";
const char* ifttt_event = "security_alert";

// Security thresholds
const float TEMP_THRESHOLD = 36.0;     // Temperature alert threshold (°C)
const float TEMP_CRITICAL = 45.0;      // Critical temperature (°C)
const int GAS_THRESHOLD = 1500;        // Gas level alert threshold (PPM)
const int GAS_CRITICAL = 2500;         // Critical gas level (PPM)
const int CLOSE_TIME = 19;             // Hour to check if window should be closed
const int LDR_THRESHOLD = 1000;        // LDR threshold for laser detection

// Access control
String correctPassword = "1234";       // Default keypad password
String emergencyPassword = "9911";     // Emergency override password
String authorizedRFID[] = {"E993D505", "AABBCCDD", "11223344"}; // Multiple authorized cards
int numAuthorizedCards = 3;

// ESP32-CAM configuration (optional)
String camIP = "http://192.168.8.105/";

// System modes
enum SystemMode {
  NORMAL,
  ARMED,
  EMERGENCY,
  MAINTENANCE
};

// ===== PIN DEFINITIONS =====
// Sensors
#define DHTPIN 25
#define DHTTYPE DHT22

#define MQ2_PIN 35

#define PIR_1_PIN 26
#define PIR_2_PIN 4

#define LDR_PIN 34

#define LIMIT_SWITCH 33

// Actuators
#define LED_PIN 13
#define RELAY_PIN 27


// Communication
#define SDA 21
#define SCL 22
#define RFID_SDA 17
#define RST_PIN 16

// I2C addresses
#define LCD_ADDR 0x27
#define KEYPAD_ADDR 0x20

// Virtual pins for Blynk
// #define MANUAL_TRIGGER_VPIN V4
// #define ARM_SYSTEM_VPIN V5
// #define TEMP_VPIN V0
// #define HUMIDITY_VPIN V1
// #define GAS_VPIN V2
// #define MOTION_VPIN V3

// EEPROM addresses
#define EEPROM_SIZE 512
#define EEPROM_PASSWORD_ADDR 0
#define EEPROM_RFID_ADDR 50
#define EEPROM_LOG_ADDR 200

// ===== NON-BLOCKING TIMER CLASS =====
class NonBlockingTimer {
  unsigned long interval;
  unsigned long previousMillis;
  bool runningFlag;

public:
  NonBlockingTimer() : interval(0), previousMillis(0), runningFlag(false) {}

  void start(unsigned long ms) {
    interval = ms;
    previousMillis = millis();
    runningFlag = true;
  }

  bool isDone() {
    if (!runningFlag) return true;
    if (millis() - previousMillis >= interval) {
      previousMillis = millis();
      return true;
    }
    return false;
  }

  void reset() {
    runningFlag = false;
  }

  bool isRunning() {
    return runningFlag;
  }
  
  unsigned long getElapsed() {
    return millis() - previousMillis;
  }
};

// ===== ALERT SYSTEM CLASS =====
class AlertSystem {
private:
  bool buzzerActive;
  NonBlockingTimer buzzerTimer;
  int buzzerPattern;
  
public:
  AlertSystem() : buzzerActive(false), buzzerPattern(0) {}
  
  void triggerAlert(int pattern) {
    buzzerActive = true;
    buzzerPattern = pattern;
    buzzerTimer.start(100);
  }
  
  void stopAlert() {
    buzzerActive = false;
    digitalWrite(LED_PIN, LOW);
  }
  
  void update() {
    if (buzzerActive && buzzerTimer.isDone()) {
      static bool buzzerState = false;
      buzzerState = !buzzerState;
      digitalWrite(LED_PIN, buzzerState);
      
      // Different patterns for different alerts
      switch(buzzerPattern) {
        case 1: // Motion
          buzzerTimer.start(buzzerState ? 200 : 100);
          break;
        case 2: // Gas
          buzzerTimer.start(buzzerState ? 500 : 500);
          break;
        case 3: // Temperature
          buzzerTimer.start(buzzerState ? 100 : 50);
          break;
        case 4: // Emergency
          buzzerTimer.start(buzzerState ? 50 : 50);
          break;
        default:
          buzzerTimer.start(100);
      }
    }
  }
};
 
// ===== GLOBAL OBJECTS =====
// Sensors
DHT dht(DHTPIN, DHTTYPE);
RTC_DS3231 rtc;
MFRC522 rfid(RFID_SDA, RST_PIN);

// Display
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// Custom systems
AlertSystem alertSystem;

// Keypad configuration
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {0, 1, 2, 3};  // PCF8574 pins P0-P3
byte colPins[COLS] = {4, 5, 6, 7};  // PCF8574 pins P4-P7
Keypad_I2C customKeypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS, KEYPAD_ADDR, 1, &Wire);

// ===== GLOBAL VARIABLES =====
// Timers
NonBlockingTimer printTimer;
NonBlockingTimer ledTimer;
NonBlockingTimer relayTimer;
NonBlockingTimer lcdTimer;
NonBlockingTimer sensorLogTimer;
NonBlockingTimer heartbeatTimer;

// LED control
unsigned long ledOnTime = 500;
unsigned long ledOffTime = 500;
bool ledState = false;

// System states
SystemMode currentMode = NORMAL;
bool systemArmed = false;
bool relayState = false;
String enteredPassword = "";
String motionStatus = "";
String tempAlert = "";
String gasAlert = "";
int lcdPage = 0;
int failedAttempts = 0;
unsigned long lastAlertTime = 0;
bool silentMode = false;

// Statistics
struct SystemStats {
  unsigned long totalMotionEvents;
  unsigned long totalAccessGrants;
  unsigned long totalAccessDenials;
  unsigned long totalAlerts;
  float maxTemp;
  float minTemp;
  int maxGas;
};
SystemStats stats = {0, 0, 0, 0, -999, 999, 0};

// Sensor data structure
struct SensorData {
  float temperature;
  float humidity;
  int gasLevel;
  int pirOneState;
  int pirTwoState;
  int ldrValue;
  int limitSwitchValue;
  DateTime timestamp;
};

// ===== SETUP FUNCTION =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n===== Silent Eye V1.0 Starting =====");
  
  // Initialize I2C
  Wire.begin(SDA, SCL);

  // Initialize pins
  initializePins();

  // Initialize sensors
  dht.begin();
  
  // Initialize display
  initializeLCD();
  
  // Initialize timers
  initializeTimers();
  
  // Initialize WiFi
  initializeWiFi();
  
  // Initialize RTC
  initializeRTC();
  
  // Initialize keypad
  customKeypad.begin(makeKeymap(keys));
  
  // Initialize RFID
  SPI.begin();
  rfid.PCD_Init();
  
  // Scan I2C devices
  scanI2CDevices();
  
  // Initialize Blynk
  // #ifdef USE_BLYNK
  //   if (WiFi.status() == WL_CONNECTED) {
  //     Blynk.config(BLYNK_AUTH_TOKEN);
  //     Blynk.connect();
  //   }
  // #endif
  
  Serial.println("===== System Ready =====\n");
}

// ===== MAIN LOOP ===========================================================
void loop() {
  // Read all sensors
  SensorData sensors = readSensors();
  updateStatistics(sensors);
  
  // Process keypad input
  processKeypad();
  
  // Process RFID
  processRFID();
  
  // Check security triggers
  checkSecurityTriggers(sensors);
  
  // Update alert system
  alertSystem.update();
  
  // Handle relay timeout
  handleRelayTimeout();
  
  // Update serial output
  if (printTimer.isDone()) {
    displaySerialData(sensors);
  }
  
  // Update LCDinitializePinsloop
  if (lcdTimer.isDone()) {
    updateLCD(sensors);
  }
  
  
  // Process Blynk
  // #ifdef USE_BLYNK
  //   if (WiFi.status() == WL_CONNECTED) {
  //     Blynk.run();
  //     updateBlynkData(sensors);
  //   }
  // #endif
  
  // Process serial commands
  processSerialCommands();

}

// ===== INITIALIZATION FUNCTIONS =====
void initializeLCD() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Silent Eye V1.0");
  lcd.setCursor(2, 1);
  lcd.print("Secure Home");
  delay(2000);
  lcd.clear();
}

void initializePins() {
  pinMode(PIR_1_PIN, INPUT);
  pinMode(PIR_2_PIN, INPUT);
  pinMode(LIMIT_SWITCH, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

}

void initializeTimers() {
  printTimer.start(1000);       // Serial output every 1s
  ledTimer.start(ledOnTime);    // LED blink interval
  lcdTimer.start(2000);         // LCD update every 2s
  heartbeatTimer.start(300000); // Send heartbeat every 5 minutes
}

void initializeWiFi() {
  lcd.clear();
  lcd.print("Connecting WiFi");
  
  // WiFi.begin(ssid, password);
  
  // int attempts = 0;
  // while (WiFi.status() != WL_CONNECTED && attempts < 20) {
  //   delay(500);
  //   lcd.print(".");
  //   attempts++;
  // }
  
  lcd.clear();
  // if (WiFi.status() == WL_CONNECTED) {
       lcd.print("WiFi Connected!");
  //   lcd.setCursor(0, 1);
  //   lcd.print(WiFi.localIP());
  //   Serial.print("WiFi connected. IP: ");
  //   Serial.println(WiFi.localIP());
  //   delay(2000);
  // } else {
  //   lcd.print("WiFi Failed!");
  //   Serial.println("WiFi connection failed - running in offline mode");
  //   delay(2000);
  // }
}

void initializeRTC() {
  lcd.clear();
  if (!rtc.begin()) {
    lcd.print("RTC Not Found!");
    Serial.println("ERROR: RTC not found!");
    delay(2000);
  } else {
    lcd.print("RTC OK");
    Serial.println("RTC initialized successfully");
    
    if (rtc.lostPower()) {
      lcd.setCursor(0, 1);
      lcd.print("Setting time...");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      Serial.println("RTC time updated");
    }
    delay(1500);
  }
}

void scanI2CDevices() {
  Serial.println("Scanning I2C devices...");
  byte count = 0;
  
  for (byte i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x");
      if (i < 16) Serial.print("0");
      Serial.println(i, HEX);
      count++;
    }
  }
  
  if (count == 0) {
    Serial.println("No I2C devices found!");
  } else {
    Serial.print("Total I2C devices found: ");
    Serial.println(count);
  }
}

// ===== SENSOR READING =====
SensorData readSensors() {
  SensorData data;
  
  data.temperature = dht.readTemperature();
  data.humidity = dht.readHumidity();
  data.gasLevel = analogRead(MQ2_PIN);
  data.pirOneState = digitalRead(PIR_1_PIN);
  data.pirTwoState = digitalRead(PIR_2_PIN);
  data.ldrValue = analogRead(LDR_PIN);
  data.limitSwitchValue = digitalRead(LIMIT_SWITCH);
  data.timestamp = rtc.now();
  
  return data;
}

// ===== ACCESS CONTROL =====
void processKeypad() {
  char key = customKeypad.getKey();
  
  if (key) {
    Serial.print("Key pressed: ");
    Serial.println(key);
    
    // Special function keys
    if (key == 'A') {  // Arm/Disarm system
      toggleSystemArmed();
    }
    else if (key == 'B') {  // Silent mode toggle
      silentMode = !silentMode;
      Serial.print("Silent mode: ");
      Serial.println(silentMode ? "ON" : "OFF");
    }
    else if (key == 'C') {  // Show stats
      displayStats();
    }
    else if (key == 'D') {  // Emergency button
      activateEmergencyMode();
    }
    else if (key == '#') {  // Enter key
      if (enteredPassword == correctPassword) {
        grantAccess("Keypad");
        failedAttempts = 0;
      } else if (enteredPassword == emergencyPassword) {
        activateEmergencyMode();
      } else {
        denyAccess("Keypad - Wrong password");
        failedAttempts++;
        if (failedAttempts >= 5) {
          lockoutMode();
        }
      }
      enteredPassword = "";
    } 
    else if (key == '*') {  // Clear key
      enteredPassword = "";
      Serial.println("Input cleared");
    } 
    else if (key >= '0' && key <= '9') {  // Numeric keys only
      enteredPassword += key;
      // Limit password length
      if (enteredPassword.length() > 8) {
        enteredPassword = "";
        Serial.println("Password too long - cleared");
      }
    }
  }
}

void processRFID() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String cardUID = "";
    
    for (byte i = 0; i < rfid.uid.size; i++) {
      cardUID += (rfid.uid.uidByte[i] < 0x10 ? "0" : "");
      cardUID += String(rfid.uid.uidByte[i], HEX);
    }
    
    cardUID.toUpperCase();
    Serial.print("RFID Card: ");
    Serial.println(cardUID);
    
    bool authorized = false;
    for (int i = 0; i < numAuthorizedCards; i++) {
      if (cardUID == authorizedRFID[i]) {
        authorized = true;
        break;
      }
    }
    
    if (authorized) {
      grantAccess("RFID");
      failedAttempts = 0;
    } else {
      denyAccess("RFID - Unauthorized card: " + cardUID);
      failedAttempts++;
      if (failedAttempts >= 5) {
        lockoutMode();
      }
    }
    
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
}

void grantAccess(String method) {
  Serial.print("ACCESS GRANTED - ");
  Serial.println(method);
  
  lcd.clear();
  lcd.print("Access Granted!");
  lcd.setCursor(0, 1);
  lcd.print(method);
  
  digitalWrite(RELAY_PIN, HIGH);
  relayState = true;
  relayTimer.start(3000);  // Keep unlocked for 3 seconds
  
  stats.totalAccessGrants++;

  // Disarm system temporarily
  if (systemArmed) {
    systemArmed = false;
    alertSystem.stopAlert();
  }
  
  // #ifdef USE_BLYNK
  //   Blynk.notify("Access granted via " + method);
  // #endif
}

void denyAccess(String reason) {
  Serial.print("ACCESS DENIED - ");
  Serial.println(reason);
  
  lcd.clear();
  lcd.print("Access Denied!");
  
  stats.totalAccessDenials++;
  
  if (!silentMode) {
    // Flash LED rapidly for denial
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  // #ifdef USE_BLYNK
  //   Blynk.notify("Access denied: " + reason);
  // #endif
}

void lockoutMode() {
  Serial.println("SECURITY LOCKOUT - Too many failed attempts");
  lcd.clear();
  lcd.print("SECURITY LOCKOUT");
  lcd.setCursor(0, 1);
  lcd.print("Wait 1 minutes");
  
  
  if (!silentMode) {
    alertSystem.triggerAlert(4);  // Emergency pattern
  }
  
  delay(100000);  // 1 minute lockout
  failedAttempts = 0;
  alertSystem.stopAlert();
}

void handleRelayTimeout() {
  if (relayState && relayTimer.isDone()) {
    digitalWrite(RELAY_PIN, LOW);
    relayState = false;
    relayTimer.reset();
    Serial.println("Relay auto-locked");
    
    // Re-arm system if it was armed before
    if (currentMode == ARMED) {
      systemArmed = true;
    }
  }
}

// ===== SECURITY MONITORING =====
void checkSecurityTriggers(SensorData& sensors) {
  bool triggered = false;
  String alertType = "";
  String alertDetails = "";
  
  // Check window status after hours
  if (sensors.limitSwitchValue == HIGH && sensors.timestamp.hour() >= CLOSE_TIME) {
    triggered = true;
    alertType = "WINDOW";
    alertDetails = "Window open after hours";
    Serial.println("ALERT: Window open after hours!");
  }
  
  // Check motion sensors (only if armed)
  if (systemArmed) {
    if (sensors.pirOneState == HIGH || sensors.pirTwoState == HIGH) {
      triggered = true;
      alertType = "MOTION";
      updateMotionStatus(sensors);
      alertDetails = "Motion detected: " + motionStatus;
      stats.totalMotionEvents++;
    }
    
    // Check laser tripwire
    if (sensors.ldrValue > LDR_THRESHOLD) {
      triggered = true;
      alertType = "LASER";
      alertDetails = "Laser tripwire activated";
      if (motionStatus.indexOf("Laser") == -1) {
        Serial.println("ALERT: Laser tripwire activated!");
      }
    }
  }
  
  // Check temperature
  if (!isnan(sensors.temperature)) {
    if (sensors.temperature > TEMP_CRITICAL) {
      triggered = true;
      alertType = "FIRE";
      alertDetails = "Critical temperature: " + String(sensors.temperature) + "°C";
      tempAlert = "FIRE DANGER!";
      currentMode = EMERGENCY;
    } else if (sensors.temperature > TEMP_THRESHOLD) {
      triggered = true;
      alertType = "TEMP";
      alertDetails = "High temperature: " + String(sensors.temperature) + "°C";
      tempAlert = "TEMP HIGH!";
    } else {
      tempAlert = "";
    }
  }
  
  // Check gas level
  if (sensors.gasLevel > GAS_CRITICAL) {
    triggered = true;
    alertType = "GAS_CRITICAL";
    alertDetails = "Critical gas level: " + String(sensors.gasLevel) + " PPM";
    gasAlert = "EVACUATE!";
    currentMode = EMERGENCY;
  } else if (sensors.gasLevel > GAS_THRESHOLD) {
    triggered = true;
    alertType = "GAS";
    alertDetails = "Gas detected: " + String(sensors.gasLevel) + " PPM";
    gasAlert = "GAS DETECTED!";
  } else {
    gasAlert = "";
  }
  
  // Handle alerts
  if (triggered) {
    handleSecurityAlert(alertType, alertDetails);
  }
  
  // Control alert LED
  controlAlertLED(triggered);
}

void handleSecurityAlert(String type, String details) {
  // Rate limit alerts (one per minute)
  if (millis() - lastAlertTime < 60000) return;
  lastAlertTime = millis();
  
  Serial.println("SECURITY ALERT: " + type + " - " + details);
  stats.totalAlerts++;
  
  if (!silentMode) {
    // Different alert patterns for different threats
    if (type == "FIRE" || type == "GAS_CRITICAL") {
      alertSystem.triggerAlert(4);  // Emergency
    } else if (type == "GAS") {
      alertSystem.triggerAlert(2);  // Gas
    } else if (type == "MOTION" || type == "LASER") {
      alertSystem.triggerAlert(1);  // Motion
    } else if (type == "TEMP") {
      alertSystem.triggerAlert(3);  // Temperature
    }
  }
  
  
  // Capture photo if motion detected
  // if ((type == "MOTION" || type == "LASER") && WiFi.status() == WL_CONNECTED) {
  //   capturePhoto();
  // }
  
  // #ifdef USE_BLYNK
  //   Blynk.notify("🚨 " + type + ": " + details);
  // #endif
}

void updateMotionStatus(SensorData& sensors) {
  motionStatus = "";
  
  if (sensors.pirOneState == HIGH) {
    motionStatus += "PIR#1";
  }
  
  if (sensors.pirTwoState == HIGH) {
    if (motionStatus.length() > 0) motionStatus += " & ";
    motionStatus += "PIR#2";
  }
  
  if (sensors.ldrValue > LDR_THRESHOLD) {
    if (motionStatus.length() > 0) motionStatus += " & ";
    motionStatus += "Laser";
  }
  
  if (motionStatus.length() == 0) {
    motionStatus = "Clear";
  }
}

void controlAlertLED(bool triggered) {
  if (triggered) {
    // Blink LED
    if (ledTimer.isDone()) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      ledTimer.start(ledState ? ledOnTime : ledOffTime);
    }
  } else {
    // Turn off LED
    digitalWrite(LED_PIN, LOW);
    ledState = false;
    ledTimer.reset();
  }
}

// ===== SYSTEM MODES =====
void toggleSystemArmed() {
  systemArmed = !systemArmed;
  currentMode = systemArmed ? ARMED : NORMAL;
  
  lcd.clear();
  lcd.print("System ");
  lcd.print(systemArmed ? "ARMED" : "DISARMED");
  
  Serial.print("System ");
  Serial.println(systemArmed ? "ARMED" : "DISARMED");
  
  
  if (systemArmed) {
    // Give user time to leave
    lcd.setCursor(0, 1);
    lcd.print("Exit in 10s...");
    delay(10000);
  } else {
    alertSystem.stopAlert();
  }
  
  // #ifdef USE_BLYNK
  //   Blynk.virtualWrite(ARM_SYSTEM_VPIN, systemArmed ? 1 : 0);
  // #endif
}

void activateEmergencyMode() {
  currentMode = EMERGENCY;
  Serial.println("EMERGENCY MODE ACTIVATED!");
  
  lcd.clear();
  lcd.print("!! EMERGENCY !!");
  lcd.setCursor(0, 1);
  lcd.print("Help is coming!");
  
  // Open door
  digitalWrite(RELAY_PIN, HIGH);
  relayState = true;
  
  // Sound alarm
  if (!silentMode) {
    alertSystem.triggerAlert(4);
  }
  
  
  // #ifdef USE_BLYNK
  //   Blynk.notify("🆘 EMERGENCY MODE ACTIVATED!");
  // #endif
}

// ===== DISPLAY FUNCTIONS =====
void displaySerialData(SensorData& sensors) {
  // Date and time
  Serial.print(sensors.timestamp.year());
  Serial.print('/');
  printTwoDigits(sensors.timestamp.month());
  Serial.print('/');
  printTwoDigits(sensors.timestamp.day());
  Serial.print(' ');
  printTwoDigits(sensors.timestamp.hour());
  Serial.print(':');
  printTwoDigits(sensors.timestamp.minute());
  Serial.print(':');
  printTwoDigits(sensors.timestamp.second());
  Serial.print(" | ");
  
  // System mode
  Serial.print("Mode: ");
  switch(currentMode) {
    case NORMAL: Serial.print("NORMAL"); break;
    case ARMED: Serial.print("ARMED"); break;
    case EMERGENCY: Serial.print("EMERGENCY"); break;
    case MAINTENANCE: Serial.print("MAINTENANCE"); break;
  }
  Serial.print(" | ");
  
  // Window status
  Serial.print("Window: ");
  Serial.print(sensors.limitSwitchValue == HIGH ? "OPEN" : "CLOSED");
  Serial.print(" | ");
  
  // Motion status
  Serial.print("Motion: ");
  Serial.print(motionStatus);
  Serial.print(" | ");
  
  // Temperature
  Serial.print("Temp: ");
  if (isnan(sensors.temperature)) {
    Serial.print("ERR");
  } else {
    Serial.print(sensors.temperature, 1);
    Serial.print("°C");
  }
  if (tempAlert != "") Serial.print(" !");
  Serial.print(" | ");
  
  // Humidity
  Serial.print("Humidity: ");
  if (isnan(sensors.humidity)) {
    Serial.print("ERR");
  } else {
    Serial.print(sensors.humidity, 1);
    Serial.print("%");
  }
  Serial.print(" | ");
  
  // Gas level
  Serial.print("Gas: ");
  Serial.print(sensors.gasLevel);
  Serial.print(" PPM");
  if (gasAlert != "") Serial.print(" !");
  
  Serial.println();
}

void updateLCD(SensorData& sensors) {
  lcd.clear();
  
  switch (lcdPage) {
    case 0:  // Date and time
      lcd.setCursor(0, 0);
      lcd.print("Date: ");
      lcd.print(sensors.timestamp.year());
      lcd.print('/');
      lcd.print(twoDigits(sensors.timestamp.month()));
      lcd.print('/');
      lcd.print(twoDigits(sensors.timestamp.day()));
      
      lcd.setCursor(0, 1);
      lcd.print("Time: ");
      lcd.print(twoDigits(sensors.timestamp.hour()));
      lcd.print(':');
      lcd.print(twoDigits(sensors.timestamp.minute()));
      lcd.print(':');
      lcd.print(twoDigits(sensors.timestamp.second()));
      break;
      
    case 1:  // Security status
      lcd.setCursor(0, 0);
      lcd.print("Window: ");
      lcd.print(sensors.limitSwitchValue == HIGH ? "Open" : "Closed");
      
      lcd.setCursor(0, 1);
      lcd.print("Motion: ");
      if (motionStatus.length() <= 8) {
        lcd.print(motionStatus);
      } else {
        lcd.print(motionStatus.substring(0, 8));
      }
      break;
      
    case 2:  // Environmental data
      lcd.setCursor(0, 0);
      lcd.print("T:");
      if (isnan(sensors.temperature)) {
        lcd.print("ERR");
      } else {
        lcd.print(sensors.temperature, 1);
        lcd.print("C");
      }
      if (tempAlert != "") lcd.print("!");
      
      lcd.print(" H:");
      if (isnan(sensors.humidity)) {
        lcd.print("ERR");
      } else {
        lcd.print(sensors.humidity, 1);
        lcd.print("%");
      }
      
      lcd.setCursor(0, 1);
      lcd.print("Gas: ");
      lcd.print(sensors.gasLevel);
      lcd.print(" PPM");
      if (gasAlert != "") lcd.print("!");
      break;
      
    case 3:  // System status
      lcd.setCursor(0, 0);
      lcd.print("System: ");
      switch(currentMode) {
        case NORMAL: lcd.print("NORMAL"); break;
        case ARMED: lcd.print("ARMED"); break;
        case EMERGENCY: lcd.print("EMERG!"); break;
        case MAINTENANCE: lcd.print("MAINT"); break;
      }
      
      lcd.setCursor(0, 1);
      if (relayState) {
        lcd.print("Door: UNLOCKED");
      } else if (tempAlert != "" || gasAlert != "") {
        lcd.print("ALERT ACTIVE!");
      } else {
        lcd.print("All Secure");
      }
      break;
      
    case 4:  // Statistics
      lcd.setCursor(0, 0);
      lcd.print("Alerts: ");
      lcd.print(stats.totalAlerts);
      
      lcd.setCursor(0, 1);
      lcd.print("Access: ");
      lcd.print(stats.totalAccessGrants);
      lcd.print("/");
      lcd.print(stats.totalAccessDenials);
      break;
  }
  
  // Cycle to next page
  lcdPage = (lcdPage + 1) % 5;
}

void displayStats() {
  lcd.clear();
  lcd.print("=== STATS ===");
  delay(1000);
  
  lcd.clear();
  lcd.print("Motion Events:");
  lcd.setCursor(0, 1);
  lcd.print(stats.totalMotionEvents);
  delay(2000);
  
  lcd.clear();
  lcd.print("Access G/D:");
  lcd.setCursor(0, 1);
  lcd.print(stats.totalAccessGrants);
  lcd.print(" / ");
  lcd.print(stats.totalAccessDenials);
  delay(2000);
  
  lcd.clear();
  lcd.print("Total Alerts:");
  lcd.setCursor(0, 1);
  lcd.print(stats.totalAlerts);
  delay(2000);
  
  lcd.clear();
  lcd.print("Temp Range:");
  lcd.setCursor(0, 1);
  lcd.print(stats.minTemp, 1);
  lcd.print(" - ");
  lcd.print(stats.maxTemp, 1);
  lcd.print("C");
  delay(2000);
  
  lcd.clear();
  lcd.print("Max Gas:");
  lcd.setCursor(0, 1);
  lcd.print(stats.maxGas);
  lcd.print(" PPM");
  delay(2000);
}

// ===== UTILITY FUNCTIONS =====
void printTwoDigits(int value) {
  if (value < 10) Serial.print('0');
  Serial.print(value);
}

String twoDigits(int value) {
  if (value < 10) return "0" + String(value);
  return String(value);
}

void updateStatistics(SensorData& sensors) {
  if (!isnan(sensors.temperature)) {
    if (sensors.temperature > stats.maxTemp) stats.maxTemp = sensors.temperature;
    if (sensors.temperature < stats.minTemp) stats.minTemp = sensors.temperature;
  }
  
  if (sensors.gasLevel > stats.maxGas) stats.maxGas = sensors.gasLevel;
}

// ===== ESP32-CAM INTEGRATION =====
// void capturePhoto() {
//   if (WiFi.status() == WL_CONNECTED) {
//     HTTPClient http;
    
//     // Turn on flash
//     http.begin(camIP + "control?var=flash&val=255");
//     http.GET();
//     http.end();
//     delay(100);
    
//     // Capture photo
//     http.begin(camIP + "capture");
//     int httpCode = http.GET();
    
//     if (httpCode == HTTP_CODE_OK) {
//       Serial.println("Photo captured successfully");
//       dataLogger.logEvent("PHOTO_CAPTURED");
//     } else {
//       Serial.print("Photo capture failed: ");
//       Serial.println(httpCode);
//     }
//     http.end();
    
//     // Turn off flash
//     http.begin(camIP + "control?var=flash&val=0");
//     http.GET();
//     http.end();
//   }
// }

// ===== BLYNK INTEGRATION =====
// #ifdef USE_BLYNK
// void updateBlynkData(SensorData& sensors) {
//   static unsigned long lastBlynkUpdate = 0;
  
//   if (millis() - lastBlynkUpdate > 5000) {  // Update every 5 seconds
//     lastBlynkUpdate = millis();
    
//     Blynk.virtualWrite(TEMP_VPIN, sensors.temperature);
//     Blynk.virtualWrite(HUMIDITY_VPIN, sensors.humidity);
//     Blynk.virtualWrite(GAS_VPIN, sensors.gasLevel);
//     Blynk.virtualWrite(MOTION_VPIN, (sensors.pirOneState || sensors.pirTwoState) ? 1 : 0);
//   }
// }

// BLYNK_WRITE(MANUAL_TRIGGER_VPIN) {
//   int buttonState = param.asInt();
//   if (buttonState == 1) {
//     Serial.println("Manual trigger from Blynk");
//     capturePhoto();
    
//     // Send notification
//     Blynk.notify("Manual security check triggered!");
//     dataLogger.logEvent("MANUAL_TRIGGER", "Blynk");
//   }
// }

// BLYNK_WRITE(ARM_SYSTEM_VPIN) {
//   int armState = param.asInt();
//   if (armState != systemArmed) {
//     toggleSystemArmed();
//   }
// }

// BLYNK_CONNECTED() {
//   Serial.println("Connected to Blynk server");
  
//   // Sync virtual pins
//   Blynk.syncVirtual(MANUAL_TRIGGER_VPIN);
//   Blynk.syncVirtual(ARM_SYSTEM_VPIN);
// }
// #endif

// ===== SERIAL COMMAND INTERFACE =====
void processSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "help") {
      printHelp();
    }
    else if (command == "status") {
      printSystemStatus();
    }
    else if (command == "arm") {
      if (!systemArmed) toggleSystemArmed();
    }
    else if (command == "disarm") {
      if (systemArmed) toggleSystemArmed();
    }
    else if (command == "stats") {
      printStatistics();
    }
    else if (command == "reset_stats") {
      resetStatistics();
    }
    else if (command.startsWith("set_password ")) {
      String newPass = command.substring(13);
      setPassword(newPass);
    }
    else if (command.startsWith("add_rfid ")) {
      String newRFID = command.substring(9);
      addRFIDCard(newRFID);
    }
    else if (command == "test_alarm") {
      testAlarm();
    }
    else if (command == "maintenance") {
      currentMode = MAINTENANCE;
      Serial.println("Maintenance mode activated");
    }
    else if (command == "normal") {
      currentMode = NORMAL;
      Serial.println("Normal mode activated");
    }
    // else if (command == "capture") {
    //   capturePhoto();
    // }
    else if (command == "reboot") {
      Serial.println("Rebooting system...");
      ESP.restart();
    }
  }
}

void printHelp() {
  Serial.println("\n===== COMMAND HELP =====");
  Serial.println("help         - Show this help menu");
  Serial.println("status       - Show system status");
  Serial.println("arm          - Arm the system");
  Serial.println("disarm       - Disarm the system");
  Serial.println("stats        - Show statistics");
  Serial.println("reset_stats  - Reset statistics");
  Serial.println("set_password <pass> - Set new password");
  Serial.println("add_rfid <uid>      - Add RFID card");
  Serial.println("test_alarm   - Test alarm system");
  Serial.println("maintenance  - Enter maintenance mode");
  Serial.println("normal       - Return to normal mode");
  Serial.println("capture      - Capture photo");
  Serial.println("reboot       - Restart system");
  Serial.println("========================\n");
}

void printSystemStatus() {
  Serial.println("\n===== SYSTEM STATUS =====");
  Serial.print("Mode: ");
  switch(currentMode) {
    case NORMAL: Serial.println("NORMAL"); break;
    case ARMED: Serial.println("ARMED"); break;
    case EMERGENCY: Serial.println("EMERGENCY"); break;
    case MAINTENANCE: Serial.println("MAINTENANCE"); break;
  }
  Serial.print("Armed: ");
  Serial.println(systemArmed ? "Yes" : "No");
  Serial.print("WiFi: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  Serial.print("Silent Mode: ");
  Serial.println(silentMode ? "On" : "Off");
  Serial.print("Failed Attempts: ");
  Serial.println(failedAttempts);
  Serial.println("=========================\n");
}

void printStatistics() {
  Serial.println("\n===== STATISTICS =====");
  Serial.print("Motion Events: ");
  Serial.println(stats.totalMotionEvents);
  Serial.print("Access Grants: ");
  Serial.println(stats.totalAccessGrants);
  Serial.print("Access Denials: ");
  Serial.println(stats.totalAccessDenials);
  Serial.print("Total Alerts: ");
  Serial.println(stats.totalAlerts);
  Serial.print("Temp Range: ");
  Serial.print(stats.minTemp, 1);
  Serial.print(" - ");
  Serial.print(stats.maxTemp, 1);
  Serial.println("°C");
  Serial.print("Max Gas: ");
  Serial.print(stats.maxGas);
  Serial.println(" PPM");
  Serial.println("======================\n");
}

void resetStatistics() {
  stats = {0, 0, 0, 0, -999, 999, 0};
  Serial.println("Statistics reset");
}

void testAlarm() {
  Serial.println("Testing alarm patterns...");
  
  Serial.println("Pattern 1: Motion");
  alertSystem.triggerAlert(1);
  delay(3000);
  alertSystem.stopAlert();
  
  Serial.println("Pattern 2: Gas");
  alertSystem.triggerAlert(2);
  delay(3000);
  alertSystem.stopAlert();
  
  Serial.println("Pattern 3: Temperature");
  alertSystem.triggerAlert(3);
  delay(3000);
  alertSystem.stopAlert();
  
  Serial.println("Pattern 4: Emergency");
  alertSystem.triggerAlert(4);
  delay(3000);
  alertSystem.stopAlert();
  
  Serial.println("Alarm test complete");
}
  

void saveSettings() {
  // Save password to EEPROM
  char passBuffer[20];
  correctPassword.toCharArray(passBuffer, 20);
  EEPROM.put(EEPROM_PASSWORD_ADDR, passBuffer);
  
  // Save RFID cards
  for (int i = 0; i < numAuthorizedCards; i++) {
    char rfidBuffer[20];
    authorizedRFID[i].toCharArray(rfidBuffer, 20);
    EEPROM.put(EEPROM_RFID_ADDR + (i * 20), rfidBuffer);
  }
  
  EEPROM.commit();
  Serial.println("Settings saved to EEPROM");
}

void setPassword(String newPassword) {
  if (newPassword.length() < 4 || newPassword.length() > 8) {
    Serial.println("Password must be 4-8 digits");
    return;
  }
  
  correctPassword = newPassword;
  saveSettings();
  Serial.print("Password changed to: ");
  Serial.println(correctPassword);
}

void addRFIDCard(String cardUID) {
  if (numAuthorizedCards >= 3) {
    Serial.println("Maximum RFID cards reached");
    return;
  }
  
  cardUID.toUpperCase();
  authorizedRFID[numAuthorizedCards] = cardUID;
  numAuthorizedCards++;
  saveSettings();
  
  Serial.print("RFID card added: ");
  Serial.println(cardUID);
}
