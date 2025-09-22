// #define BLYNK_TEMPLATE_ID           "TMPL6pLUFnhEn"
// #define BLYNK_TEMPLATE_NAME         "Quickstart Template"
// #define BLYNK_AUTH_TOKEN            "04Q0xlBXm140X4lF7SW-pBYM-AaRgDdH"
// #define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
// #include <BlynkSimpleEsp32.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include "RTClib.h"
#include <Keypad_I2C.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>

// ===== CONFIGURATION =====
// WiFi credentials
// const char* ssid = "YourWiFiSSID";
// const char* password = "YourWiFiPassword";

// Security thresholds
const float TEMP_THRESHOLD = 36.0;     // Temperature alert threshold (°C)
const int GAS_THRESHOLD = 1500;        // Gas level alert threshold (PPM)
const int CLOSE_TIME = 19;             // Hour to check if window should be closed
const int LDR_THRESHOLD = 1000;        // LDR threshold for laser detection

// Access control
String correctPassword = "1234";       // Default keypad password
String authorizedRFID = "E993D505";    // Authorized RFID card UID

// ESP32-CAM configuration (optional)
String camIP = "http://192.168.8.105/";

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

// Virtual pin for Blynk manual trigger
#define MANUAL_TRIGGER_VPIN V4

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
};

// ===== GLOBAL OBJECTS =====
// Sensors
DHT dht(DHTPIN, DHTTYPE);
RTC_DS3231 rtc;
MFRC522 rfid(RFID_SDA, RST_PIN);

// Display
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

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

// LED control
unsigned long ledOnTime = 500;
unsigned long ledOffTime = 500;
bool ledState = false;

// System states
bool relayState = false;
String enteredPassword = "";
String motionStatus = "";
String tempAlert = "";
String gasAlert = "";
int lcdPage = 0;

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
  
  // Initialize sensors
  dht.begin();
  
  // Initialize display
  initializeLCD();
  
  // Initialize pins
  initializePins();
  
  // Initialize timers
  initializeTimers();
  
  // Initialize WiFi (optional)
  // initializeWiFi();
  
  // Initialize RTC
  initializeRTC();
  
  // Initialize keypad
  customKeypad.begin(makeKeymap(keys));
  
  // Initialize RFID
  SPI.begin();
  rfid.PCD_Init();
  
  // Scan I2C devices
  scanI2CDevices();
  
  // Initialize Blynk (optional)
  // Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
  
  Serial.println("===== System Ready =====\n");
}

// ===== MAIN LOOP =====
void loop() {
  // Read all sensors
  SensorData sensors = readSensors();
  
  // Process keypad input
  processKeypad();
  
  // Process RFID
  processRFID();
  
  // Check security triggers and control LED
  checkSecurityTriggers(sensors);
  
  // Handle relay timeout
  handleRelayTimeout();
  
  // Update serial output
  if (printTimer.isDone()) {
    displaySerialData(sensors);
  }
  
  // Update LCD
  if (lcdTimer.isDone()) {
    updateLCD(sensors);
  }
  
  // Process Blynk (optional)
  // Blynk.run();
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
  printTimer.start(1000);    // Serial output every 1s
  ledTimer.start(ledOnTime); // LED blink interval
  lcdTimer.start(2000);      // LCD update every 2s
}

void initializeWiFi() {
  lcd.clear();
  lcd.print("Connecting WiFi");
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    lcd.print(".");
    attempts++;
  }
  
  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    lcd.print("WiFi Failed!");
    delay(2000);
  }
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
    
    if (key == '#') {  // Enter key
      if (enteredPassword == correctPassword) {
        grantAccess("Keypad");
      } else {
        denyAccess("Keypad - Wrong password");
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
    
    if (cardUID == authorizedRFID) {
      grantAccess("RFID");
    } else {
      denyAccess("RFID - Unauthorized card");
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
}

void denyAccess(String reason) {
  Serial.print("ACCESS DENIED - ");
  Serial.println(reason);
  
  lcd.clear();
  lcd.print("Access Denied!");
  
  // Flash LED rapidly for denial
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

void handleRelayTimeout() {
  if (relayState && relayTimer.isDone()) {
    digitalWrite(RELAY_PIN, LOW);
    relayState = false;
    relayTimer.reset();
    Serial.println("Relay auto-locked");
  }
}

// ===== SECURITY MONITORING =====
void checkSecurityTriggers(SensorData& sensors) {
  bool triggered = false;
  
  // Check window status after hours
  if (sensors.limitSwitchValue == HIGH && sensors.timestamp.hour() >= CLOSE_TIME) {
    triggered = true;
    Serial.println("ALERT: Window open after hours!");
  }
  
  // Check motion sensors
  if (sensors.pirOneState == HIGH || sensors.pirTwoState == HIGH) {
    triggered = true;
    updateMotionStatus(sensors);
  }
  
  // Check laser tripwire
  if (sensors.ldrValue > LDR_THRESHOLD) {
    triggered = true;
    if (!motionStatus.contains("Laser")) {
      Serial.println("ALERT: Laser tripwire activated!");
    }
  }
  
  // Check temperature
  if (!isnan(sensors.temperature) && sensors.temperature > TEMP_THRESHOLD) {
    triggered = true;
    tempAlert = "TEMP HIGH!";
  } else {
    tempAlert = "";
  }
  
  // Check gas level
  if (sensors.gasLevel > GAS_THRESHOLD) {
    triggered = true;
    gasAlert = "GAS DETECTED!";
  } else {
    gasAlert = "";
  }
  
  // Control alert LED
  controlAlertLED(triggered);
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
      lcd.print("System Status:");
      
      lcd.setCursor(0, 1);
      if (relayState) {
        lcd.print("Door: UNLOCKED");
      } else if (tempAlert != "" || gasAlert != "") {
        lcd.print("ALERT ACTIVE!");
      } else {
        lcd.print("All Secure");
      }
      break;
  }
  
  // Cycle to next page
  lcdPage = (lcdPage + 1) % 4;
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

// ===== ESP32-CAM INTEGRATION (Optional) =====
void capturePhoto() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // Turn on flash
    http.begin(camIP + "/control?var=flash&val=255");
    http.GET();
    http.end();
    delay(100);
    
    // Capture photo
    http.begin(camIP + "/capture");
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      Serial.println("Photo captured successfully");
      // The response contains JPEG data - could be saved/sent
    } else {
      Serial.print("Photo capture failed: ");
      Serial.println(httpCode);
    }
    http.end();
    
    // Turn off flash
    http.begin(camIP + "/control?var=flash&val=0");
    http.GET();
    http.end();
  }
}

// ===== BLYNK CALLBACKS (Optional) =====
/*
BLYNK_WRITE(MANUAL_TRIGGER_VPIN) {
  int buttonState = param.asInt();
  if (buttonState == 1) {
    Serial.println("Manual trigger from Blynk");
    capturePhoto();
    
    // Send notification
    Blynk.notify("Manual security check triggered!");
  }
}

BLYNK_CONNECTED() {
  Serial.println("Connected to Blynk server");
  
  // Sync virtual pins
  Blynk.syncVirtual(MANUAL_TRIGGER_VPIN);
}
*/
