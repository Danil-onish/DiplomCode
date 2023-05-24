#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <EEPROM.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define API_KEY "***"
#define USER_EMAIL "***"
#define USER_PASSWORD "***"
#define DATABASE_URL "https://climate-mon-default-rtdb.europe-west1.firebasedatabase.app/"

// Define Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Variable to save USER UID
String uid;
// Database main path (to be updated in setup with the user UID)
String databasePath;
// Parent Node (to be updated in every loop)
String parentPath;

FirebaseJson json;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Variable to save current epoch time
int timestamp;

String tempPath = "/temperature";
String humPath = "/humidity";
String timePath = "/timestamp";

char buff[30];
int temperature;
int humidity;

volatile byte indx;
int fIndx = 0;

String ssid = "test";
String password = "test";

bool isStandart = true;
bool isFailed = false;

ESP8266WebServer server(80);

unsigned long getTime() {
  timeClient.update();
  unsigned long now = timeClient.getEpochTime();
  return now;
}

bool initWiFi(String ssid, String password, int timeout) {
  Serial.print("Connecting to ");
  Serial.print(ssid);
  WiFi.begin(ssid, password);             // Connect to the network

  int i = 0;
  while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
    delay(1000);
    Serial.print('.');
    i++;
    if (i > timeout)    {
      Serial.println("\nConnection failed!");
      return false;
    }
  }
  Serial.println("\nConnection established!");
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());
  Serial.println();
  return true;
}

void EEPROM_write(String buffer, int N) //Запись в eeprom
{
  EEPROM.begin(512);
  delay(10);
  for (int L = 0; L < 32; ++L)
  {
    EEPROM.write(N + L, buffer[L]);
  }
  EEPROM.commit();
}
String EEPROM_read (int min, int max) //Чтение eeprom
{
  EEPROM.begin(512);
  delay(10);
  String buffer;
  for (int L = min; L < max; ++L)
    buffer += char(EEPROM.read(L));
  return buffer;
}

void handleData() {
  if (server.hasArg("ssid-data")) {
    String data = server.arg("ssid-data");
    String pass = server.arg("pass-data");
    Serial.println("Received data: " + data);
    Serial.println("Received pass: " + pass);

    ssid = data;
    EEPROM_write(data, 0);
    EEPROM_write(pass, 32);
  }
  server.sendHeader("Location", "/", true);  // Устанавливаем заголовок "Location" с адресом перенаправления
  server.send(301);  // Отправляем код состояния 301 (перемещено навсегда)
}

void handleRoot() {

  String htmlContent = "<body style='background-color: rgb(131, 131, 131);'>";
  htmlContent += "<form style='position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%);' method='POST' action='/save_data'>";
  htmlContent += "<p style='font-size: 40px; padding-top: 20px'><b>Current Wi-Fi: </b></p>";
  htmlContent += "<p style='font-size: 40px'>";
  htmlContent += ssid;
  htmlContent += "</p>";
  htmlContent += "<label style='font-size: 30px' for='ssid-data' >Enter Wi-Fi network name</label>";
  htmlContent += "<input style='display: block; font-size: 25px; margin-bottom: 30px' type='text' maxlength='32' name='ssid-data'/>";
  htmlContent += "<label style='font-size: 30px' for='pass-data'>Enter Wi-Fi network password</label>";
  htmlContent += "<input style='display: block; font-size: 25px; margin-bottom: 30px' type='text' maxlength='32' name='pass-data'/>";
  htmlContent += "<input style='display: block; font-size: 40px'  type='submit'  value='Submit'/>";
  htmlContent += "</form>";
  htmlContent += "</body>";

  server.send(200, "text/html", htmlContent);
}

const int BUTTON = 4;
IPAddress local_ip(192, 168, 0, 103);
IPAddress gateway(192, 168, 0, 103);
IPAddress subnet(255, 255, 255, 0);

void setup(void) {
  Serial.begin(9600);
  Serial.println(" ");
  Serial.println(" ");
  isFailed = false;
  //  Serial.println("Enter setup\n");
  ssid = EEPROM_read(0, 32);
  password = EEPROM_read(32, 64);
  pinMode (BUTTON, INPUT);

  if (digitalRead(BUTTON) == 1)
  {
    isStandart = false;
    Serial.println("Reset WiFi");
    const char* ssidLocal = "ESP8266Net";
    const char* passwordLocal = "1248163264";

    WiFi.softAPConfig(local_ip, gateway, subnet);
    WiFi.softAP(ssidLocal, passwordLocal);

    server.on("/", handleRoot);
    server.on("/save_data", handleData);

    server.begin();
    Serial.println("Server started");
  }
  else
  {
    Serial.println("Standart run");

    if (initWiFi(ssid, password, 10)) {
      Serial.println("Connected successfully to " + ssid);

      timeClient.begin();

      // Assign the api key (required)
      config.api_key = API_KEY;

      // Assign the user sign in credentials
      auth.user.email = USER_EMAIL;
      auth.user.password = USER_PASSWORD;

      // Assign the RTDB URL (required)
      config.database_url = DATABASE_URL;
      Firebase.reconnectWiFi(true);
      fbdo.setResponseSize(50);

      // Assign the callback function for the long running token generation task */
      config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
      // Assign the maximum retry of token generation
      config.max_token_generation_retry = 5;

      // Initialize the library with the Firebase authen and config
      Firebase.begin(&config, &auth);

      // Getting the user UID might take a few seconds
      int timeOut = 0;
      Serial.println("Getting User UID");
      while ((auth.token.uid) == "") {
        timeOut++;
        if (timeOut == 5) {
          Serial.println("Failed to get User UID");
          isFailed = true;
          break;
        }
        Serial.print('.');
        delay(1000);
      }
      // Print user UID
      if ((auth.token.uid) != "") {
        uid = auth.token.uid.c_str();
        Serial.print("User UID: ");
        Serial.println(uid);
        Serial.println();
        // Update database path
        databasePath = "/UsersData/" + uid + "/readings";
        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, LOW);
        delay(500);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(500);
        digitalWrite(LED_BUILTIN, LOW);
        delay(500);
        digitalWrite(LED_BUILTIN, HIGH);
      }
    }
    else {
      Serial.println("Failed to connect to " + ssid);
      isFailed = true;
    }
  }

  if (isFailed) {
    failLed();
  }

  pinMode(LED_BUILTIN, OUTPUT);
  if (isStandart == 0)
    digitalWrite(LED_BUILTIN, LOW);
  else
    digitalWrite(LED_BUILTIN, HIGH);
}

void failLed() {
  Serial.println("Fail loop");
  pinMode(LED_BUILTIN, OUTPUT);

  while (true) {
    digitalWrite(LED_BUILTIN, false);
    delay(1500);
    digitalWrite(LED_BUILTIN, true);
    delay(1500);
    switchModes();
  }
}

void switchModes() {
  if ((isStandart == 0 && digitalRead(BUTTON) == 0) || (isStandart == 1 && digitalRead(BUTTON) == 1)) {
    Serial.println("restart");
    Serial.println(" ");
    ESP.restart();
  }
}

void loop(void) {
  //  Serial.println("Enter loop\n");
  switchModes();

  if (isStandart && Serial.available() > 0) {
    byte c = Serial.read();
    fIndx++;
    if (c == '\r') {
      if (fIndx > 4) {
        temperature = ((buff[0] ) * 10 + buff[1]);
        humidity = ((buff[2] ) * 10 + buff[3]);
        Serial.print("tmp=");
        Serial.println(temperature);
        Serial.print("hum=");
        Serial.println(humidity);
      }
      indx = 0;
      fIndx = 0;
      // Send new readings to database
      if (Firebase.ready()) {
        //Get current timestamp
        timestamp = getTime();
        Serial.print ("time: ");
        Serial.println (timestamp);

        parentPath = databasePath + "/" + String(timestamp);

        json.set(tempPath.c_str(), String(temperature));
        json.set(humPath.c_str(), String(humidity));
        json.set(timePath, String(timestamp));
        Serial.printf("Set json... %s\n",
                      Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json) ?
                      "ok" : fbdo.errorReason().c_str());
        Serial.println ();
      }
      delay(5000);
    }
    else
      buff [indx++] = c;
  }
  server.handleClient();
}
