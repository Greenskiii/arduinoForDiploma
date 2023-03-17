#include <SD.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <MQ135.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <WiFiClientSecureBearSSL.h>

// Insert Firebase project API Key
#define API_KEY "AIzaSyA3OjDx9awkcJWjYMeYzNqRKaoww3mxW1Q"
#define USER_EMAIL "test@gmail.com"
#define USER_PASSWORD "test12"

// Insert RTDB URLefine the RTDB URL
#define DATABASE_URL "diplma-v1-default-rtdb.europe-west1.firebasedatabase.app"
#define DHTPIN D6
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

//Adafruit_BMP085 bmp;

AsyncWebServer server(80);

const String DEVICE_ID = "rAXm4M5Xs7mF7d8znA75preview";

#define HOST_NAME "fcm.googleapis.com"
#define ACCESS_TOKEN "dQ3aE1YkyE39jge7RBp9pI:APA91bEYbrFVBab7ZMS-7qjqHfM4zrZNKmBzIE_SrLpGRW9Sjtr2hs126dbp8JNjN_A_YQtmdPZLP8mkE4wN4IWNhYst2MEV_bd2WlbUmtcBZcCB3g5hGtkkzUM7G5ntMbAJQcCu7AlS"
#define SERVER_KEY "AAAA_jU-sKc:APA91bEA7AZHFeb38HW6BjvwJdy9cpl8pploFcloUwHgojtlelYCAPeOkJ_XZldnpMAPnaushOp5sTGYSliMiCCGv7ucMhSCMQMabWINSeL0tLuhrPbHSCdYw10Cx0nNaDqDjqKa2cS9"
const char* fcmServer = "fcm.googleapis.com";

// WiFi
String WIFI_SSID = "Znet-Router";
String WIFI_PASSWORD = "0957572601";
//String WIFI_SSID = "";
//String WIFI_PASSWORD = "";

const String acces_point_SSID = "HomeBrain";
const String acces_point_password = "1234567890";

// Define Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

//String uid;

String databasePath;

// Parent Node (to be updated in every loop)
String parentPath;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
</head>
<body>
<section>
<h2>Home Mind</h2>
<p><b>WiFi Network Credentials</b></p>
<form action="/get">
SSID: <input type="text" name="SSID">
Password: <input type="text" name="Password">
<input type="submit" value="Submit">
</form><br>
</section>
</body>
</html>)rawliteral";

FirebaseJson json;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Variable to save current epoch time
int timestamp;
int smokeA0 = A0;
int sensorThres = 600;

// Timer variables (send new readings every three minutes)
unsigned long sendDataPrevMillis = 0;
unsigned long timerDelay = 180000;

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}
// Initialize WiFi
void conectToWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi ..");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(1000);
    }
    Serial.println(WiFi.localIP());
    Serial.println();
}

void initWiFi() {
    
    WiFi.softAP(acces_point_SSID, acces_point_password);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    
    // Send web page with input fields to client
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });
    
    // Send a GET request to <ESP_IP>/get?input1=<inputMessage>
    server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
        String inputSSID;
        String inputPASSWORD;
        // GET input1 value on <ESP_IP>/get?input1=<inputMessage>
        if (request->hasParam("SSID") && request->hasParam("Password")) {
            inputSSID = request->getParam("SSID")->value();
            inputPASSWORD = request->getParam("Password")->value();
            
            WIFI_SSID = inputSSID;
            WIFI_PASSWORD = inputPASSWORD;
            conectToWiFi();
        }
        else {
            inputSSID = "SSID";
            inputPASSWORD = "PASSWORD";
        }
        
        Serial.println(inputSSID);
        request->send(200, "text/html", "HTTP GET request sent to your ESP on input field ("
                      + inputSSID + ") with value: " + inputPASSWORD +
                      "<br><a href=\"/\">Return to Home Page</a>");
    });
    server.onNotFound(notFound);
    server.begin();
    
}

void fairbaseAutorization() {
    config.api_key = API_KEY;
    
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    
    config.database_url = DATABASE_URL;
    
    Firebase.reconnectWiFi(true);
    fbdo.setResponseSize(4096);
    
    config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
    
    config.max_token_generation_retry = 5;
    
    Firebase.begin(&config, &auth);
    
    Serial.println("Getting User UID");
    while ((auth.token.uid) == "") {
        Serial.print('.');
        delay(1000);
    }
    databasePath = "/devices/" + DEVICE_ID;
}

// Function that gets current epoch time
unsigned long getTime() {
    timeClient.update();
    unsigned long now = timeClient.getEpochTime();
    return now;
}
bool notificationSended = true;
void sendDataToFirebase() {
    
    float fireHumid = dht.readHumidity();
    float fireTemp = dht.readTemperature();
    float mqValue = analogRead(A0);
    float mqPercents = mqValue/1024 * 100;
    int alarm = 0;
    String isGas = "";
    alarm = digitalRead(D2);
    
    Serial.print("Humidity: ");
    Serial.print(fireHumid);
    
    Serial.print("%  Temperature: ");
    Serial.print(fireTemp);
    Serial.println("°C ");
    
Serial.print("GAS: ");
  Serial.print(mqPercents);
  Serial.println("%");
  
    if (mqPercents >= 0.1) {
      if (!notificationSended) {
           sendNotificationToFirebase();
        }
        isGas = "Hight";
    } else {
        isGas = "Normal";
        notificationSended = false;
    }
    
    sendDataPrevMillis = millis();
    //Get current timestamp
    timestamp = getTime();
    Serial.print ("time: ");
    Serial.println (timestamp);
    
    parentPath = databasePath;
    json.set("image", "https://firebasestorage.googleapis.com/v0/b/diplma-v1.appspot.com/o/GasStation.png?alt=media&token=5a5a1631-2620-4959-89e8-66220e3cd167");
    json.set("name", "GasCheck");
    json.set("id", DEVICE_ID);
    json.set("time", String(timestamp));
    
    json.set("previewValues/name", "Atmospheric");
    
    json.set("previewValues/1/name", "Temperature");
    json.set("previewValues/1/value", String(fireTemp, 0) + "°C");
    json.set("previewValues/1/imageSystemName", "thermometer.medium");
    
    json.set("previewValues/2/name", "Humidity");
    json.set("previewValues/2/value", String(fireHumid, 0) + " %");
    json.set("previewValues/2/imageSystemName", "humidity");
    
    json.set("values/1/name", "Gas");
    json.set("values/1/value", String(mqPercents, 2) + " %");
    json.set("values/1/imageSystemName", "carbon.dioxide.cloud");
    
    Serial.printf("Set json... %s\n", Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json) ? "ok" : fbdo.errorReason().c_str());
}

void setup(){
    Serial.begin(115200);
    WiFi.mode(WIFI_AP_STA);
    pinMode(smokeA0, INPUT);
    
    initWiFi();
    if (WIFI_SSID.length() != 0) {
        conectToWiFi();
    }
    
    dht.begin();
    timeClient.begin();
    if (WiFi.status() == WL_CONNECTED) {
        fairbaseAutorization();
    }
    sendDataToFirebase();
}

void loop(){
    if (WiFi.status() == WL_CONNECTED && auth.token.uid != "") {
        sendDataToFirebase();
    } else {
        if (WiFi.status() != WL_CONNECTED && WIFI_SSID.length() != 0) {
            conectToWiFi();
        }
        if (WiFi.status() == WL_CONNECTED && auth.token.uid == "") {
            fairbaseAutorization();
        }
    }
    delay(20000);
}

void sendNotificationToFirebase() {
    const uint8_t fingerprint[20] = {0xe3, 0x46, 0x5f, 0x16, 0xd9, 0xdf, 0xf6, 0xe3, 0x94, 0xfd, 0x4c, 0x3b, 0x23, 0x65, 0xd8, 0x0c, 0x11, 0x41, 0x69, 0xbb};
    
    StaticJsonDocument<384> doc;
    
    doc["to"] = "eh6XNvQ9l0dcjbwlfA1uX1:APA91bH3NqO93eH-VN2_3QYWXpnSwg6oOoqe-VzI6b2Ae4yu8sSEhSwvzxlrgSPjth6OV6DgGUGCqQwlD0_nLMp2nsMBoTgokZhDKIbqF1aBjyKfddlz23mP2OK_gQ-m8-uUf_RkSuJb";
    
    JsonObject notification = doc.createNestedObject("notification");
    notification["body"] = "The device detected a gas leak";
    notification["title"] = "Gas danger";
    
    String json;
    serializeJson(doc, json);
    Serial.println(json);
    
    if ((WiFi.status() == WL_CONNECTED)) {
        
        std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
        
        client->setFingerprint(fingerprint);
        // Or, if you happy to ignore the SSL certificate, then use the following line instead:
        // client->setInsecure();
        
        HTTPClient https;
        
        Serial.print("[HTTPS] begin...\n");
        if (https.begin(*client, "https://fcm.googleapis.com/fcm/send")) {  // HTTPS
            
            https.addHeader("Authorization", "key=AAAA_jU-sKc:APA91bEA7AZHFeb38HW6BjvwJdy9cpl8pploFcloUwHgojtlelYCAPeOkJ_XZldnpMAPnaushOp5sTGYSliMiCCGv7ucMhSCMQMabWINSeL0tLuhrPbHSCdYw10Cx0nNaDqDjqKa2cS9");
            https.addHeader("Content-Type", "application/json");
            
            Serial.print("[HTTPS] GET...\n");
            // start connection and send HTTP header
            int httpCode = https.POST(json);
            
            // httpCode will be negative on error
            if (httpCode > 0) {
                // HTTP header has been send and Server response header has been handled
                Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
                
                // file found at server
                if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    String payload = https.getString();
                    Serial.println(payload);
                }
                notificationSended = true;
            } else {
                Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
            }
            
            https.end();
        } else {
            Serial.printf("[HTTPS] Unable to connect\n");
        }
    }
    
}
