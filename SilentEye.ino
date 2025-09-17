#define BLYNK_TEMPLATE_ID           "TMPL6pLUFnhEn"
#define BLYNK_TEMPLATE_NAME         "Quickstart Template"
#define BLYNK_AUTH_TOKEN            "04Q0xlBXm140X4lF7SW-pBYM-AaRgDdH"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>  // Blynk Library
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include "RTClib.h"

const char ssid[] = "Chenitha";
const char password[] = "Cheni@20050630";

RTC_DS3231 rtc;

#define DHTPIN 25
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

#define MQ2_PIN 35
#define PIR_1_PIN 26
#define PIR_2_PIN 19
#define LDR_PIN 34
#define LED_PIN 13
#define LIMIT_SWITCH 33
#define SDA 21
#define SCL 22

#define MANUAL_TRIGGER_VPIN V4

/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial

const float TEMP_THRESHOLD = 36.0;
const int GAS_THRESHOLD = 1500;
const int CLOSE_TIME = 19;

String camIP = "http://192.168.8.105/";  // Replace with ESP32-CAM IP

BLYNK_WRITE(MANUAL_TRIGGER_VPIN) {
  int buttonState = param.asInt(); // 1 = pressed, 0 = released
  if (buttonState == 1) {
    Serial.println("Manual trigger activated!");
    digitalWrite(LED_PIN, HIGH);  // Turn on LED for testing
    delay(300);
    digitalWrite(LED_PIN, LOW);
  }
}

void setup() {
  Serial.begin(115200); //Starting Serial Communication
  Wire.begin(SDA, SCL);
  dht.begin();
  WiFi.begin(ssid, password);
  
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);

  pinMode(PIR_1_PIN, INPUT);
  pinMode(PIR_2_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LIMIT_SWITCH, INPUT_PULLUP); //Limit_Switch 3V3 - GPIO33

  digitalWrite(LED_PIN, LOW); //keep Lights off start of the system

  //Checking Wifi connection
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected successfully");
  
  //Is RTC Connected
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC!");
    while (1);
  }

  //IF RTC lost power SET Time and Date
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void printTwoDigits(int value) {
  if (value < 10) Serial.print('0');
  Serial.print(value);
}


//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void loop() {
  int pirOneState = digitalRead(PIR_1_PIN); //Store PIR 1 Value
  int pirTwoState = digitalRead(PIR_2_PIN); //Store PIR 2 Value
  int ldrValue = analogRead(LDR_PIN); //Store LDR Value
  int limitSwitchValue = digitalRead(LIMIT_SWITCH); //Store Limit Switch Value
  bool triggered = false;
  DateTime now = rtc.now(); //store Date & Time


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
      if(now.hour()>=CLOSE_TIME) {
        triggered = true;
        }
      } else {
        Serial.print("Window Closed | ");
        }


  //For PIR #1, #2 and Laser module
  if(pirOneState == HIGH|| pirTwoState == HIGH || ldrValue > 1000) {
    triggered = true;
  }

  String motionStatus = "";
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

  // Read DHT22 sensor
  float humidity = dht.readHumidity(); // read humidity and store as float
  float temp = dht.readTemperature();  // read temp and store as float

  if (isnan(humidity) || isnan(temp)) {
    Serial.println("Failed to read from DHT sensor!"); //if DHT22 sensor output's not a number (nan) print sensor failure message
  }

  String tempAlert = "";
  if (!isnan(temp) && temp > TEMP_THRESHOLD) {
    tempAlert = "Temp High! ";
    triggered = true;
  }

  // Read MQ-2 analog value
  int gasLevel = analogRead(MQ2_PIN);

  String gasAlert = "";
  if (gasLevel > GAS_THRESHOLD){
    gasAlert = "Gas High! ";
    triggered = true; 
  }

  // Print all info in one line
  displayValues(motionStatus, humidity, temp, tempAlert, gasLevel, gasAlert);

  //For trigger alert 
  if(triggered) {
    digitalWrite(LED_PIN, HIGH);
    delay(300);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }

  Blynk.run();

}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void displayValues(String motionStatus, float humidity, float temp, String tempAlert, float gasLevel, String gasAlert) {
  // Print all info in one line
  Serial.print("Motion: ");
  Serial.print(motionStatus);
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

  delay(1000);
}
