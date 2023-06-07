/*
* SAMPACO, VILLAMOR - IOT-BASED WATER CONSUMPTION MONITORING SYSTEM
* ITD108 - INFORMATION ENGINEERING
* FINAL PROJECT (S.Y. 2022-2023 2ND SEM)
*/

#include <Arduino.h>
#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <OneWire.h>
#include <DallasTemperature.h>
//includes for timestamp
#include <NTPClient.h> 
#include <WiFiUdp.h> 

#define WIFI_SSID "YOUARESHATE"
#define WIFI_PASSWORD "randompass23"

#define API_KEY "AIzaSyAPk6sIYpsnmXYfHq41DsQjuM8Vooa7UYo"
#define FIREBASE_PROJECT_ID "waterms-dea9e"

#define FLOW_SENSOR_PIN D3
#define ONE_WIRE_BUS D8

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

volatile int flow_frequency;
float vol = 0.0, l_minute;
unsigned long dataMillis = 0;

//bool signupOK = false;

#define USER_EMAIL "admin@gmail.com"
#define USER_PASSWORD "091601"

// Variable to save USER UID
String uid;

void setupWiFi() {
  Serial.begin(9600);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);

  /* FIREBASE AUTHENTICATION ENABLED */
  config.api_key = API_KEY;
  // Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback;  //see addons/TokenHelper.h

  // Limit the size of response payload to be collected in FirebaseData
  fbdo.setResponseSize(2048);

  Firebase.begin(&config, &auth);

  Firebase.reconnectWiFi(true);
  
  // Getting the user UID might take a few seconds
  Serial.println("Getting User UID");
  while ((auth.token.uid) == "") {
    Serial.print('.');
    delay(1000);
  }
  // Print user UID
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.print(uid);

}

void setupSensors() {
  pinMode(FLOW_SENSOR_PIN, INPUT);
  Serial.begin(9600);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), increase, RISING);

  sensors.begin();
}

void setup() {
  setupWiFi();
  setupSensors();

  timeClient.begin();
}

void temperature() {

  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  float tempF = (tempC * 9.0) / 5.0 + 32.0;

  Serial.print("Temperature: ");
  Serial.print(tempC);
  Serial.write(0xC2);
  Serial.write(0xB0);
  Serial.print("C | ");

  Serial.print(tempF);
  Serial.write(0xC2);
  Serial.write(0xB0);
  Serial.print("F");
  Serial.println();

  String tempPath = "temperature/tempId";
  String celsius = String(tempC, 1);
  String fahrenheit = String(tempF, 1);

  FirebaseJson tempContent;

  tempContent.set("fields/celsius/doubleValue", celsius);
  tempContent.set("fields/fahrenheit/doubleValue", fahrenheit);

  timeClient.update();
  time_t now = timeClient.getEpochTime();

  if (now == 0) {
    Serial.println("Error getting current time");
  } else {
    // Set timestamp field with current time
    char timestampStr[30];
    strftime(timestampStr, sizeof(timestampStr), "%FT%TZ", gmtime(&now));
    // send to firestore
    tempContent.set("fields/DateTime/timestampValue", timestampStr);
  }

  Serial.print("Updating temperature document... ");
  if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", tempPath.c_str(), tempContent.raw(), "celsius,fahrenheit,DateTime")) {
    Serial.println("Succeed");
    Serial.println(fbdo.payload().c_str());

  } else {
    Serial.println("Failed");
    Serial.println(fbdo.errorReason());
  }

  Serial.print("Creating temperature document... ");
  if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", tempPath.c_str(), tempContent.raw())) {
    Serial.println("Succeed");
    Serial.println(fbdo.payload().c_str());

  } else {
    Serial.println("Failed");
    Serial.println(fbdo.errorReason());
  }
}

void water() {

  // Water Volume Reading
  if (flow_frequency != 0) {
    l_minute = (flow_frequency / 5.5);

    Serial.print("Flow Rate: ");
    Serial.print(l_minute);
    Serial.println(" L/M");

    l_minute = l_minute / 60;
    vol = vol + l_minute;

    Serial.print("Water Volume: ");
    Serial.print(vol);
    Serial.println(" L");

    flow_frequency = 0;  // Reset Counter
    Serial.println(l_minute, DEC);
  } else {
    Serial.print("Flow Rate =  ");
    Serial.print(flow_frequency);
    Serial.println(" L/M");

    Serial.print("Water Volume: ");
    Serial.print(vol);
    Serial.println(" L");
  }

  String volume = String(vol, 2);
  // Create document path using auto-id
  String volumePath = "waterVolume";
  // Create FirebaseJson object for volume payload
  FirebaseJson volumeContent;
  volumeContent.set("fields/volume/doubleValue", volume);
  volumeContent.set("fields/unit/stringValue", " L");

  timeClient.update();
  time_t now = timeClient.getEpochTime();

  if (now == 0) {
    Serial.println("Error getting current time");
  } else {
    // Set timestamp field with current time
    char timestampStr[30];
    strftime(timestampStr, sizeof(timestampStr), "%FT%TZ", gmtime(&now));
    volumeContent.set("fields/DateTime/timestampValue", timestampStr);
  }

  // Send the volume document to Firestore
  Serial.print("Creating water volume document... ");
  if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", volumePath.c_str(), volumeContent.raw())) {
    Serial.println("Succeed");
    Serial.println(fbdo.payload().c_str());
  } else {
    Serial.println("Failed");
    Serial.println(fbdo.errorReason());
  }
}

void loop() {
  if (Firebase.ready() && (millis() - dataMillis > 30000 || dataMillis == 0)) {
    dataMillis = millis();

    temperature();
    water();
  }
}

ICACHE_RAM_ATTR void increase() {
  flow_frequency++;
}
