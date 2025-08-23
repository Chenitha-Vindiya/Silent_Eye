#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define DHTPIN 25
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

#define MQ2_PIN 32
#define PIR_PIN 35
#define LDR_PIN 34
#define LED_PIN 13

const float TEMP_THRESHOLD = 30.0;
const int GAS_THRESHOLD = 1500;

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  dht.begin();
}

void sendSensorData(float temp, int gasLevel) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("http://localhost:5000/api/sensor"); // Replace with backend URL
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"temperature\":" + String(temp, 1) + ",\"gas\":" + String(gasLevel) + "}";
    int httpResponseCode = http.POST(payload);

    http.end();
  }
}

void loop() {
  int pirState = digitalRead(PIR_PIN);
  int ldrValue = analogRead(LDR_PIN);
  bool triggered = false;

  //For PIR and Laser module
  if(pirState == HIGH || ldrValue > 1000) {
    triggered = true;
  }

  String motionStatus = "";
  if (pirState == HIGH) motionStatus += "PIR ";
  if (ldrValue > 1000) {
    if (motionStatus.length() > 0) 
    motionStatus += "& ";
    motionStatus += "Laser";
  }
  if (motionStatus.length() == 0) motionStatus = "No motion";

  // Read DHT22 sensor
  float humidity = dht.readHumidity();
  float temp = dht.readTemperature();

  if (isnan(humidity) || isnan(temp)) {
    Serial.println("Failed to read from DHT sensor!");
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

  // Print all info in one line, overwrite previous line
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
  Serial.print(gasLevel);
  Serial.print(" ");
  Serial.print(gasAlert);
  Serial.println("");

  //For trigger alert 
  if(triggered) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }

  delay(1000);

}
