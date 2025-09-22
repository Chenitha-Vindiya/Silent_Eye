// #define BLYNK_TEMPLATE_ID           "TMPL6pLUFnhEn"
// #define BLYNK_TEMPLATE_NAME         "Quickstart Template"
// #define BLYNK_AUTH_TOKEN            "04Q0xlBXm140X4lF7SW-pBYM-AaRgDdH"

#include <WiFi.h>
#include <WiFiClient.h>
//#include <BlynkSimpleEsp32.h>  // Blynk Library
#include <HTTPClient.h>
#include <DHT.h> //DHT22
#include <Wire.h>
#include "RTClib.h" //RTC
#include <Keypad_I2C.h> //Keypad I2C
#include <Keypad.h> //Keypad
#include <LiquidCrystal_I2C.h> //LCD Display I2C
#include <SPI.h>
#include <MFRC522.h> //RFID

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
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
    if (!runningFlag) return true;  // allow immediate first run
    if (millis() - previousMillis >= interval) {
      previousMillis = millis();    // reset timer
      return true;
    }
    return false;
  }

  void reset() {
    runningFlag = false;
  }
};

// ----- Declare timers globally-----
NonBlockingTimer printTimer;   // Timer for displayValues()
NonBlockingTimer ledTimer;     // Timer for LED blinking
NonBlockingTimer relayTimer;   // Timer for relay activation
NonBlockingTimer lcdTimer;   // Timer for LCD updates

unsigned long ledOnTime = 500;   // LED ON duration (ms)
unsigned long ledOffTime = 500;  // LED OFF duration (ms)
bool ledState = false;           // LED state

bool relayState = false;

String motionStatus = "";
String tempAlert = "";
String gasAlert = "";


// const char ssid[] = "Chenitha";
// const char password[] = "Cheni@20050630";

//RTC object
RTC_DS3231 rtc;

#define DHTPIN 25
#define DHTTYPE DHT22

//DHT22 object
DHT dht(DHTPIN, DHTTYPE);

// Create LCD object at I2C address 0x27 (common) with 16 chars & 2 lines
LiquidCrystal_I2C lcd(0x27, 16, 2);


#define MQ2_PIN 35
#define PIR_1_PIN 26
#define PIR_2_PIN 4
#define LDR_PIN 34
#define LED_PIN 13
#define LIMIT_SWITCH 33
#define SDA 21
#define SCL 22
#define RELAY_PIN 27

#define RFID_SDA 17 //RFID SDA
#define RST_PIN 16 //RFID RST

#define MANUAL_TRIGGER_VPIN V4

/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial

const float TEMP_THRESHOLD = 36.0;
const int GAS_THRESHOLD = 1500;
const int CLOSE_TIME = 19;

String enteredPassword = "";
String correctPassword = "1234";

#define I2CADDR 0x20  // PCF8574 default if A0,A1,A2 = GND

// Create RTC object
MFRC522 rfid(RFID_SDA, RST_PIN);

const byte ROWS = 4;
const byte COLS = 4;

// Define keypad layout
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// Row and column pins via PCF8574
byte rowPins[ROWS] = {0, 1, 2, 3}; // P0–P3
byte colPins[COLS] = {4, 5, 6, 7}; // P4–P7

// Create keypad instance
Keypad_I2C customKeypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS, I2CADDR, 1, &Wire);

String camIP = "http://192.168.8.105/";  // Replace with ESP32-CAM IP

// BLYNK_WRITE(MANUAL_TRIGGER_VPIN) {
//   int buttonState = param.asInt(); // 1 = pressed, 0 = released
//   if (buttonState == 1) {
//     Serial.println("Manual trigger activated!");
//     digitalWrite(LED_PIN, HIGH);  // Turn on LED for testing
//     delay(300);
//     digitalWrite(LED_PIN, LOW);
//   }
// }



//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200); //Starting Serial Communication
  Wire.begin(SDA, SCL);
  dht.begin();

  // Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Silent Eye V1.0");
  lcd.setCursor(2, 1);
  lcd.print("Secure Home");
  delay(1500);
  lcd.clear();
  lcd.setCursor(0, 0);
  
  pinMode(PIR_1_PIN, INPUT);
  pinMode(PIR_2_PIN, INPUT);

  pinMode(LIMIT_SWITCH, INPUT_PULLUP); //Limit_Switch GND - GPIO33

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Keep relay OFF at start
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); //keep Lights off start of the system

  printTimer.start(1000);       // print every 1s
  ledTimer.start(ledOnTime);    // LED blinking interval
  lcdTimer.start(1000);         // update LCD every 1s
  
  // WiFi.begin(ssid, password);

  //Checking Wifi connection
  lcd.print("WiFi connecting");
  delay(1500);
  // while (WiFi.status() != WL_CONNECTED) {
  //   delay(500);
  //   lcd.print(".");
  // }
  // lcd.setCursor(0, 1);
  // lcd.println("WiFi connected!");

  lcd.clear();
  lcd.setCursor(0, 0);
  //Is RTC Connected
  if (!rtc.begin()) {
    lcd.print("RTC Not Found");
  }else {
    lcd.print("RTC OK");
  }

  delay(1500);
  lcd.clear();
  lcd.setCursor(0, 0);
  //IF RTC lost power SET Time and Date
  if (rtc.lostPower()) {
    lcd.print("RTC lost power");
    lcd.setCursor(0, 1);
    lcd.print("setting time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  customKeypad.begin(makeKeymap(keys)); //Keypad

  Serial.println("\nI2C Scanner");
  byte count = 0;
  for (byte i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x");
      Serial.println(i, HEX);
      count++;
    }
  }
  if(count==0) Serial.println("No I2C devices found");

  //RDID Initialization
  SPI.begin();           // Init SPI bus
  rfid.PCD_Init();       // Init RC522
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//For Serial print
void printTwoDigits(int value) {
  if (value < 10) Serial.print('0');
  Serial.print(value);
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//For LCD print
String twoDigits(int value) {
  if (value < 10) return "0" + String(value);
  return String(value);
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void loop() {
  int pirOneState = digitalRead(PIR_1_PIN); //Store PIR 1 Value
  int pirTwoState = digitalRead(PIR_2_PIN); //Store PIR 2 Value
  int ldrValue = analogRead(LDR_PIN); //Store LDR Value
  int limitSwitchValue = digitalRead(LIMIT_SWITCH); //Store Limit Switch Value

  // Read DHT22 sensor
  float humidity = dht.readHumidity(); // read humidity and store as float
  float temp = dht.readTemperature();  // read temp and store as float

  // Read MQ-2 analog value
  int gasLevel = analogRead(MQ2_PIN);

  bool triggered = false;

  DateTime now = rtc.now(); //store Date & Time
  
  char key = customKeypad.getKey();
  if (key) {
    Serial.print("Key Pressed: ");
    Serial.println(key);

    if (key == '#') {  // # = Enter
      if (enteredPassword == correctPassword) {
        Serial.println("Access Granted");
        digitalWrite(RELAY_PIN, HIGH);   // Turn ON relay
        relayState = true;
        relayTimer.start(3000);          // keep unlocked for 3 sec
      } else {
        Serial.println("Access Denied");
        relayState = false;
      }
      enteredPassword = ""; // reset input
    } 
    else if (key == '*') {  // * = Clear
      enteredPassword = "";
      Serial.println("Input cleared");
    } 
    else {
      enteredPassword += key; // add digit to input
    }
  }

  // Turn off relay when timer expires
  if (relayState && relayTimer.isDone()) {
      digitalWrite(RELAY_PIN, LOW);
      relayState = false;
      relayTimer.reset();
  }

  // //communicating with esp32 cam
  // if (WiFi.status() == WL_CONNECTED) {
  //   HTTPClient http;

  //   // Example: turn flash LED ON
  //   http.begin(camIP + "/control?var=flash&val=255"); 
  //   int httpCode = http.GET();
  //   http.end();

  //   delay(3000);

  //   // Example: turn flash LED OFF
  //   http.begin(camIP + "/control?var=flash&val=0"); 
  //   httpCode = http.GET();
  //   http.end();

  //   delay(1000);

  //   // Example: capture a photo
  //   http.begin(camIP + "/capture");
  //   httpCode = http.GET();
  //   if (httpCode == HTTP_CODE_OK) {
  //     Serial.println("Captured image!");
  //     // NOTE: response body contains JPEG, you can save/send it
  //   }
  //   http.end();

  //   delay(3000);
  // }


  // Check for new RFID card
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String content = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
          content += (rfid.uid.uidByte[i] < 0x10 ? "0" : "") + String(rfid.uid.uidByte[i], HEX);
      }
      content.toUpperCase();
      Serial.println("RFID: " + content);

      if (content == "E993D505") {  
          Serial.println("Access Granted by RFID");
          digitalWrite(RELAY_PIN, HIGH);   // Turn ON relay
          relayState = true;
          relayTimer.start(3000);          // 3-second timer
      } else {
          Serial.println("Access Denied");
          relayState = false;
      }

    //Release
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // Turn off relay when timer expires
  if (relayState && relayTimer.isDone()) {
      digitalWrite(RELAY_PIN, LOW);
      relayState = false;
      relayTimer.reset();
  }


  // checkTriggersAndControlLed(
  //     limitSwitchValue,
  //     pirOneState,
  //     pirTwoState,
  //     ldrValue,
  //     temp,
  //     gasLevel,
  //     humidity,
  //     triggered,
  //     ledState,
  //     ledTimer,
  //     ledOnTime,
  //     ledOffTime
  // );

  // Print all info in one line
  if (printTimer.isDone()) {
      displayValues(humidity, temp, gasLevel, limitSwitchValue, pirOneState, pirTwoState, ldrValue);
  }
  
  // LCD Print
  if (lcdTimer.isDone()) {
      updateLcdPages(limitSwitchValue, temp, humidity, gasLevel, now);
  }

  // Blynk.run();
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void checkTriggersAndControlLed(
  int limitSwitchValue,
  int pirOneState,
  int pirTwoState,
  int ldrValue,
  float temp,
  int gasLevel,
  float humidity,
  bool &triggered,
  bool &ledState,
  NonBlockingTimer &ledTimer,
  unsigned long ledOnTime,
  unsigned long ledOffTime
) {

  DateTime now = rtc.now();
  // Reset trigger flag
  triggered = false;
  //Check window open or close using Limit_Switch Value
  if(limitSwitchValue == HIGH) {
    if(now.hour()>=CLOSE_TIME) {
      triggered = true;
    }
  }

  //For PIR #1, #2 and Laser module
  if(pirOneState == HIGH|| pirTwoState == HIGH || ldrValue > 1000) {
    triggered = true;
  }

  //For High Temp Trigger
  if (!isnan(temp) && temp > TEMP_THRESHOLD) {
    triggered = true;
  }

  //For High Gas Trigger
  if (gasLevel > GAS_THRESHOLD){
    triggered = true; 
  }

  //For trigger alert with ON and OFF times
  if(triggered) {
      if(ledState) {
          if(ledTimer.isDone()) {  // LED ON duration finished
              digitalWrite(LED_PIN, LOW);
              ledState = false;
              ledTimer.start(ledOffTime); // start OFF duration
          }
      } else {
          if(ledTimer.isDone()) {  // LED OFF duration finished
              digitalWrite(LED_PIN, HIGH);
              ledState = true;
              ledTimer.start(ledOnTime); // start ON duration
          }
      }
  } else {
      digitalWrite(LED_PIN, LOW);
      ledState = false;
      ledTimer.reset();  // stop LED timer
  }
}


//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void displayValues(float humidity, float temp, float gasLevel, int limitSwitchValue, int pirOneState, int pirTwoState, int ldrValue) {

  DateTime now = rtc.now(); //store Date & Time

  // display date and time
  Serial.print(now.year());
  Serial.print('/');
  printTwoDigits(now.month());
  Serial.print('/');
  printTwoDigits(now.day());
  Serial.print(' ');
  printTwoDigits(now.hour());
  Serial.print(':');
  printTwoDigits(now.minute());
  Serial.print(':');
  printTwoDigits(now.second());
  Serial.print(" | ");

  //Check window open or close using Limit_Switch Value
  if(limitSwitchValue == HIGH) {
    Serial.print("Window Open | ");
  } else {
    Serial.print("Window Closed | ");
  }

    motionStatus = "";
  if (pirOneState == HIGH) motionStatus += "#1 PIR";

  if (pirTwoState == HIGH) {
  if (motionStatus.length() > 0) motionStatus += " & ";
  motionStatus += "#2 PIR";
  }

  if (ldrValue > 1000) {
    if (motionStatus.length() > 0) motionStatus += " & ";
    motionStatus += "Laser";
  }

  if (motionStatus.length() == 0) motionStatus = "No motion";

  tempAlert = "";
  if (!isnan(temp) && temp > TEMP_THRESHOLD) {
    tempAlert = "Temp High! ";
  }

  gasAlert = "";
  if (gasLevel > GAS_THRESHOLD){
    gasAlert = "Gas High! ";
  }

  // Print all info in one line
  Serial.print("Motion: ");
  Serial.print(motionStatus);
  if (isnan(humidity) || isnan(temp)) Serial.print(" | Failed to read from DHT sensor!"); //if DHT22 sensor output's not a number (nan) print sensor failure message
  Serial.print(" | Humidity: ");
  if (isnan(humidity)) Serial.print("Err");
  else Serial.print(humidity, 1);
  Serial.print("% | Temp: ");
  if (isnan(temp)) Serial.print("Err");
  else Serial.print(temp, 1);
  Serial.print("°C ");
  Serial.print(tempAlert);
  Serial.print("| Gas: ");
  if (gasLevel == 0) Serial.print("Err");
  else Serial.print(gasLevel, 1);
  Serial.print(" PPM ");
  Serial.print(gasAlert);
  Serial.println("");
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void updateLcdPages(int limitSwitchValue, float temp, float humidity, int gasLevel, DateTime now) {
  //LCD Output
  static int lcdPage = 0;
  lcd.clear();

  switch (lcdPage) {
    case 0: // Date & Time
      lcd.setCursor(0, 0);
      lcd.print("Date: ");
      lcd.print(now.year()); lcd.print('/');
      lcd.print(twoDigits(now.month())); lcd.print('/');
      lcd.print(twoDigits(now.day()));
      lcd.setCursor(0, 1);
      lcd.print("Time: ");
      lcd.print(twoDigits(now.hour())); lcd.print(':');
      lcd.print(twoDigits(now.minute())); lcd.print(':');
      lcd.print(twoDigits(now.second()));
      break;

    case 1: // Window & Motion
      lcd.setCursor(0, 0);
      if(limitSwitchValue == HIGH) lcd.print("Window: Open");
      else lcd.print("Window: Closed");
      lcd.setCursor(0, 1);
      lcd.print("Motion: ");
      lcd.print(motionStatus);
      break;

    case 2: // Temp & Humidity
      lcd.setCursor(0, 0);
      lcd.print("Temp:");

      if (isnan(temp)) lcd.print("Err");
      else { lcd.print(temp, 1); lcd.print("C"); }
      if (tempAlert != "") lcd.print("!");
      lcd.setCursor(0, 1);
      lcd.print("Hum:");

      if (isnan(humidity)) lcd.print("Err");
      else { lcd.print(humidity, 1); lcd.print("%"); }
      break;

    case 3: // Gas level
      lcd.setCursor(0, 0);
      lcd.print("Gas:");

      if (gasLevel == 0) lcd.print("Err");
      else { lcd.print(gasLevel, 1); lcd.print(" PPM"); }

      lcd.setCursor(0, 1);
      lcd.print(gasAlert);
      break;
  }

  // cycle to next page next time
  lcdPage++;
  if (lcdPage > 3) lcdPage = 0;
}
