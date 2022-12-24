/*
 * Author: Ken Ross
 * https://github.com/verykross
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the MIT License.
 *
 * It is my hope that this program will be as useful to others as it is
 * for me, but comes WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * MIT License for more details.
 *
 * A copy of the MIT License is included with this program.
 *
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <string.h>
#include <Arduino.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266mDNS.h>
#include <Time.h>

#include <Fonts/FreeSans9pt7b.h>

#include <ESP8266WebServer.h>
#include <EEPROM.h>

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define OLED_RESET -1     // Reset pin # (or -1 if sharing Arduino reset pin)

#define BG_VER 1.1        // Current version of BG Buddy, displayed at startup

IPAddress local_IP(10,10,10,10);
IPAddress gateway(10,10,10,1);
IPAddress subnet(255,255,255,0);

ESP8266WebServer server(80);

WiFiEventHandler stationConnectedHandler;

WiFiClientSecure client;

int nsConnectCount = 0;
int nsRetryDelay = 15;            // Initial retry delay in seconds
int nsRetryMax = 9;               // Max # of retries before asking for help
int nsRetryNoticeThreshold = 20;  // The point at which the display will indicate a retry delay

bool askReset = false;
bool nsConnectFailed = false;
bool nsFirstUpdate = true;
bool wifiInitialized = false;
bool connectionValidated = false;

unsigned long curMiliCount = 0;
unsigned long prevMiliCount = 0;
int nsApiInterval = 60000;        // Call the NS API every minute

struct settings {
  int initialized;
  char ssid[30];
  char password[30];
  char nsurl[50];
  char nstoken[50];
} user_settings = {};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
GFXcanvas1 dispCanvas(SCREEN_WIDTH, SCREEN_HEIGHT);

const int errorLedPin = 14; // this corresponds to pin D5 on the ESP8266 (future use)
const int busyLedPin = 12;  // this corresponds to pin D6

// Define NTP Client so we can retrieve current UTC time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// This are the specific API value being retrieved; changing this will require
// changes to the parseReadings() method so best not to change it.
char apiName[] = "/api/v2/properties/bgnow,pump.data,pump.uploader";
String serverName = "";
String apiToken = "";

// The following 5 values represent what is presented on the display
String batteryLevel=""; // Battery level of the phone running Loop
String lastUpdate="";   // How long since last Nightscout update
String bgLevel="";      // Current Blood Glucose level
String insLevel="";     // Insuline level reported by pump
String arrowDir="";     // Arrow trend direction

void setup() {
  Serial.begin(115200);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  // Let the user know we're awake
  display.clearDisplay();
  display.setCursor(0,16);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.println("BG Buddy");
  display.setCursor(0,32);
  display.println("Init...");
  display.setTextSize(1);
  display.println();
  display.print("Version ");
  display.println(BG_VER);
  display.display();

  // Setup the pin to control the Busy LED
  pinMode(busyLedPin, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  EEPROM.begin(sizeof(struct settings) );
  EEPROM.get( 0, user_settings );

  serverName = user_settings.nsurl;
  if(String(user_settings.nstoken) != ""){
    apiToken = "?Token=" + String(user_settings.nstoken);
  }

  if(user_settings.initialized != 1) {
    wifiInitialized = false;
  } else {
    wifiInitialized = true;
  }
  
  Serial.println("BG Buddy");
  Serial.print("Version ");
  Serial.println(BG_VER);
  Serial.println("---------------------");

  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.hostname("BGBuddy");
  WiFi.begin(user_settings.ssid, user_settings.password);

  byte tries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    if (tries++ > 15) {
      Serial.print("Switching to Access Point mode ... ");
      Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Ready" : "Failed!");
      WiFi.mode(WIFI_AP);
      WiFi.softAP("BG Buddy Portal", "12345678");
      Serial.println("Starting Access Point at 10.10.10.10 ...");

      stationConnectedHandler = WiFi.onSoftAPModeStationConnected(&onStationConnected);
      break;
    }
  }

  if(WiFi.getMode() == WIFI_AP){
    Serial.println("Could not connect to WiFi - ask user to connect to Buddy.");

    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.println("Connect to WiFi:");
    display.setCursor(0,16);
    display.println("BG Buddy Portal");
    display.setCursor(0,32);
    display.println("Pwd: 12345678");
    display.display();
  } else {
    Serial.println("Connected to WiFi: ");
    printWiFiStatus();

    // Since we're connected to WiFi, let's display our IP address
    // In case the user wants to connect to our configuration portal
    display.clearDisplay();
    display.setCursor(0,16);
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.println("BG Buddy");
    display.setCursor(0,32);
    display.println("Init...");
    display.setTextSize(1);
    display.println();
    display.println(WiFi.localIP());
    display.display();

    // Setup mDNS so we can serve our web page at http://bgbuddy.local/
    // which is easier to remember than our IP address
    MDNS.begin("BGBuddy");
  }

  // Start the internal web server for the configuration page.
  server.on("/",  handlePortal);
  server.begin();

  client.setInsecure();
}

void loop() 
{
  MDNS.update();

  server.handleClient();

  // If user had connected to the BG Buddy access point, 
  // remind them of the IP address to browse to for setup.
  if (askReset == true) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.println("Once WiFi Connected");
    display.println("Browse to:");
    display.println("10.10.10.10");
    display.display();
    askReset = false;
  }  

  // Make the API call immediately after startup or roughly every minute
  curMiliCount = millis();
  if(nsFirstUpdate || curMiliCount - prevMiliCount >= nsApiInterval){
    prevMiliCount = curMiliCount;
    checkNSapi();
  }
}

// Turns on/off LEDs to indicate activity/busy status
void showBusy(bool ledOn){
  if(ledOn){
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(busyLedPin, HIGH); // Busy LED On
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
    digitalWrite(busyLedPin, LOW); // Busy LED Off
  }
}

// Calls the Nightscout API to retrieve updated information
void checkNSapi(){
  if(wifiInitialized) {
    Serial.println("--- Calling Nightscout API ...");
    nsFirstUpdate = false;

    // If we've previously connected, the settings are likely correct so adjust our patience
    if(connectionValidated) {
      nsRetryDelay = 30;
      nsRetryMax = 50;
    }
  
    showBusy(true);

    if (!client.connect(serverName, 443) && !nsConnectFailed) {
      Serial.printf("Attempt %i of %i failed.\n", nsConnectCount + 1, nsRetryMax);
      showBusy(false);
      
      if(connectionValidated && (nsConnectCount >= nsRetryNoticeThreshold && nsConnectCount < nsRetryMax)){
        display.clearDisplay();
        display.setCursor(0,16);
        display.setTextSize(2);
        display.setTextColor(WHITE);
        display.println("API Update");
        display.println("Failing...");
        display.printf("Retry %i\n", nsConnectCount);
        display.display();
      }

      if(nsConnectCount >= nsRetryMax) {
        nsConnectCount = 0;      
        nsConnectFailed = true;

        display.clearDisplay();
        display.setCursor(0,0);
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.println("NS Connect Fail");
        display.println("Browse to:");
        display.println(WiFi.localIP());
        display.println("to check settings.");
        display.display();
      } 

      Serial.printf("Retry in %i seconds...\n", nsRetryDelay);
      nsConnectCount++;
      delay(nsRetryDelay * 1000);
      return;
    }
  
    // Call the Nightscout API
    client.print(String("GET ") + apiName + apiToken + " HTTP/1.1\r\n" +
              "Host: " + serverName + "\r\n" +
              "User-Agent: BuildFailureDetectorESP8266\r\n" +
              "Connection: close\r\n\r\n");

    // Read (and toss) the response header
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") {
        break;
      }
    }

    // Read the JSON response data
    String rawJSON = client.readStringUntil('\n');

    DynamicJsonDocument doc(1536);
    DeserializationError error = deserializeJson(doc, rawJSON);

    if (error) {
      Serial.print(F("Failed to deserialize JSON data: "));
      Serial.println(error.f_str());
      return;
    } else {
      parseReadings(doc);
    }

    showBusy(false);
    displayInfo();

    // We know we can successfully connect and process the data - huzzah!
    connectionValidated = true;
    nsConnectCount = 0;
    
    Serial.println("Update Complete");
    Serial.println("------------------------------");
  }  
  
}

// Mostly used as a debugging aid for WiFi connectivity.
// Outputs connected SSID and IP address to the serial monitor.
void printWiFiStatus() {
  // Print the SSID of the network we connected to
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // Print the WiFi IP address
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // Print the received signal strength
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
  Serial.println();
}

void onStationConnected(const WiFiEventSoftAPModeStationConnected& evt) {
  Serial.print("Station connected: ");
  Serial.println(macToString(evt.mac));
  Serial.print("AID: ");
  Serial.println(evt.aid);

  if (evt.aid > 0) {
    askReset = true;
  }
}

// Utility function to return a formatted MAC address
String macToString(const unsigned char* mac) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

// This method is called when a user connects to BG Buddy via a web browser
// and provides an easy web-based configuration interface.
void handlePortal() {
  IPAddress userIP = server.client().remoteIP();
  Serial.print("Remote user connected to portal: ");
  Serial.println(userIP);
  String pg;
  
  if (server.method() == HTTP_POST) {

    strncpy(user_settings.ssid, server.arg("ssid").c_str(), sizeof(user_settings.ssid));
    strncpy(user_settings.password, server.arg("password").c_str(), sizeof(user_settings.password));
    strncpy(user_settings.nsurl, server.arg("nsurl").c_str(), sizeof(user_settings.nsurl));
    strncpy(user_settings.nstoken, server.arg("nstoken").c_str(), sizeof(user_settings.nstoken));
    user_settings.initialized=1;
    
    user_settings.ssid[server.arg("ssid").length()] = user_settings.password[server.arg("password").length()] = '\0';
    user_settings.nsurl[server.arg("nsurl").length()] = user_settings.nstoken[server.arg("nstoken").length()] = '\0';

    EEPROM.put(0, user_settings);
    EEPROM.commit();

    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0,16);
    display.println("Please");
    display.println("Reset");
    display.display();

    // Save confirmation page    
    Serial.println("Configuration saved to internal memory. Please reset the device.");
    wifiInitialized = false;

    pg = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    pg += "<title>BG Buddy Setup</title><style>*,::after,::before{box-sizing:border-box;}body{margin:0;font-family:\"Segoe UI\",Roboto,\"Helvetica Neue\",Arial,\"Noto Sans\",\"Liberation Sans\";";
    pg += "font-size:1rem;font-weight:400;line-height:1.5;color:#212529;background-color:#f5f5f5;}.form-control{display:block;width:100%; height:calc(1.5em + .75rem + 2px);border:1px solid #ced4da;";
    pg += "}button{border:1px solid transparent;color:#fff;background-color:#007bff;border-color:#007bff;padding:.5rem 1rem;font-size:1.25rem;line-height:1.5;border-radius:.3rem;width:100%}";
    pg += ".form-signin{width:100%;max-width:400px;padding:15px;margin:auto;}h1,p{text-align: center}</style> </head> <body><main class=\"form-signin\"> <h1>BG Buddy Setup</h1> <br/>";
    pg += "<p>Your settings have been saved successfully!<br/>Please restart the device.</p></main></body></html>";

  } else {

    String ssid = user_settings.ssid;
    String password = user_settings.password;
    String nsurl = user_settings.nsurl;
    String nstoken = user_settings.nstoken;

    // If we haven't initialized, all of these values will be junk and need to be cleared
    if(!wifiInitialized){
      ssid = "";
      password = "";
      nsurl = "";
      nstoken = "";
    }

    // Setup/Configuration page
    Serial.println("Configuration web page requested.");

    pg = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>BG Buddy Setup</title>";
    pg += "</head><body><main><form action=\"/\" method=\"post\"><h1>BG Buddy Setup</h1><p>The following settings allow you to connect BG Buddy to your local WiFi access point and allow BG Buddy ";
    pg += "to connect to your Nightscout site. Note that if you have setup an API access token on your website, you'll need to provide it here - otherwise you can leave it blank.</p>";
    pg += "<p>Also note that when providing the URL value, don't enter the \"HTTPS://\" prefix, just the text that comes after that (e.g. NOT \"https://mybgsite.myserver.com\" but THIS \"mybgsite.myserver.com\").</p>";
    pg += "<h2>WiFi Settings</h2><div><div><div><div><label>SSID: </label></div><div><input name=\"ssid\" type=\"text\" value=\"" + ssid + "\"/></div></div>";
    pg += "<div><div><label>Password: </label></div><div><input name=\"password\" type=\"password\" value=\"" + password + "\"/></div></div></div></div>";
    pg += "<h2>Nightscout Website Settings</h2><div><div><div><div><label>Nightscout URL: </label></div><div><input name=\"nsurl\" type=\"text\" size=\"50\" value=\"" + nsurl + "\"/></div></div>";
    pg += "<div><div><label>Nightscout API Token (optional): </label></div><div><input name=\"nstoken\" type=\"text\" value=\"" + nstoken + "\"/></div></div></div></div>";
    pg += "<br /><button type=\"submit\">Save</button></form>Always restart BG Buddy after saving for the changes to take effect.</main></body></html>";

  }

  server.send(200, "text/html", pg);
}

// Refresh the display with current info, including drawing the trend arrow
// Canvas is used in order to avoid any update flickering
void displayInfo(){
  // Determine the trend arrow angle and offset
  int arrowOffset = 0;
  int arrowAngle = 0;
  bool doubleArrow = false;

  char* directarr = "";
  doubleArrow=false;

  if (String(arrowDir) == "Flat") {
    directarr = "→";
    arrowAngle = 90;
    arrowOffset = -10;
  }  else if (String(arrowDir) == "FortyFiveUp") {
    directarr = "↗";
    arrowAngle = 45;
    arrowOffset = -5;
  }  else if (String(arrowDir) == "FortyFiveDown") {
    directarr = "↘";
    arrowAngle = 135;
    arrowOffset = -20;
  } else if (String(arrowDir) == "DoubleUp") {
    directarr = "↑↑";
    arrowAngle = 0;
    arrowOffset = 0;
    doubleArrow = true;
  } else if (String(arrowDir) == "DoubleDown") {
    directarr = "↓↓";
    arrowAngle = 179;
    arrowOffset = -22;
    doubleArrow = true;
  }  else if (String(arrowDir) == "SingleUp") {
    directarr = "↑";
    arrowAngle = 0;
    arrowOffset = 0;
  }  else if (String(arrowDir) == "SingleDown") {
    directarr = "↓";
    arrowAngle = 179;
    arrowOffset = -22;
  }

  display.clearDisplay();
  dispCanvas.setFont(&FreeSans9pt7b);

  dispCanvas.fillScreen(BLACK);
  dispCanvas.setCursor(0,12);
  dispCanvas.setTextSize(1);
  dispCanvas.setTextColor(WHITE);

  if(batteryLevel != "") {
    dispCanvas.print(batteryLevel);
    dispCanvas.print("  ");
  }
  dispCanvas.print(lastUpdate);
  
  dispCanvas.setCursor(0,42);
  dispCanvas.setTextSize(2);
  dispCanvas.setTextColor(WHITE);
  dispCanvas.print(bgLevel);

  if (directarr != "") {
    drawArrow(78, 42+arrowOffset, 10, arrowAngle, 24, 24, WHITE);
    if(doubleArrow){
      drawArrow(105, 42+arrowOffset, 10, arrowAngle, 24, 24, WHITE);
    }
  }

  dispCanvas.setCursor(0,62);
  dispCanvas.setTextSize(1);
  if(insLevel != "") {
    dispCanvas.print("Res: ");
    dispCanvas.print(insLevel);
  }

  display.drawBitmap(0,0,dispCanvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT, WHITE, BLACK);
  display.display();
}

// Returns the elapsed time since data updated based on the supplied epoch time
String getElapsedTime(long long epochTime){
  //setTime(epochTime);
  long long epochSec = epochTime / 1000;
  unsigned long longNow = getEpochTime();

  int seconds = longNow - epochSec;
  int minutes = seconds / 60;

  String retval = "As of Now";
  if(minutes > 0){
    retval = "As of " + String(minutes) + "m ago";
  }
  return retval;
}

// Returns the current epoch time
unsigned long getEpochTime() {
  timeClient.update();
  unsigned long now = timeClient.getEpochTime();
  return now;
}

// Parses the JSON data for current readings
// Parsing code adapted from ArduinoJson Assistant generated code
// https://arduinojson.org/v6/assistant
void parseReadings(DynamicJsonDocument doc){
  JsonObject bgnow = doc["bgnow"];
  int bgnow_mean = bgnow["mean"]; // 87
  int bgnow_last = bgnow["last"]; // 87
  long long bgnow_mills = bgnow["mills"]; // 1669594578838.8948

  JsonObject bgnow_sgvs_0 = bgnow["sgvs"][0];
  const char* bgnow_sgvs_0_id = bgnow_sgvs_0["_id"]; // "6383fddfc32fe61a70eca7b8"
  int bgnow_sgvs_0_mgdl = bgnow_sgvs_0["mgdl"]; // 87
  long long bgnow_sgvs_0_mills = bgnow_sgvs_0["mills"]; // 1669594578838.8948
  const char* bgnow_sgvs_0_device = bgnow_sgvs_0["device"]; // "CGMBLEKit Dexcom G6 21.0"
  const char* bgnow_sgvs_0_direction = bgnow_sgvs_0["direction"]; // "Flat"
  const char* bgnow_sgvs_0_type = bgnow_sgvs_0["type"]; // "sgv"
  double bgnow_sgvs_0_scaled = bgnow_sgvs_0["scaled"]; // 87

  JsonObject pump_data = doc["pump"]["data"];
  int pump_data_level = pump_data["level"]; // -3

  JsonObject pump_data_clock = pump_data["clock"];
  const char* pump_data_clock_value = pump_data_clock["value"]; // "2022-11-28T00:16:33.000Z"
  const char* pump_data_clock_label = pump_data_clock["label"]; // "Last Clock"
  const char* pump_data_clock_display = pump_data_clock["display"]; // "2m ago"
  int pump_data_clock_level = pump_data_clock["level"]; // -3

  const char* pump_data_reservoir_label = pump_data["reservoir"]["label"]; // "Reservoir"
  const char* pump_data_reservoir_display = pump_data["reservoir"]["display"]; // "50+ U"

  const char* pump_data_manufacturer = pump_data["manufacturer"]; // "Insulet"
  const char* pump_data_model = pump_data["model"]; // "Eros"

  const char* pump_data_device_label = pump_data["device"]["label"]; // "Device"
  const char* pump_data_device_display = pump_data["device"]["display"]; // "loop://iPhone"

  const char* pump_data_title = pump_data["title"]; // "Pump Status"

  JsonObject pump_uploader = doc["pump"]["uploader"];
  const char* pump_uploader_timestamp = pump_uploader["timestamp"]; // "2022-11-28T00:16:33Z"
  int pump_uploader_battery = pump_uploader["battery"]; // 73
  const char* pump_uploader_name = pump_uploader["name"]; // "iPhone"
  int pump_uploader_value = pump_uploader["value"]; // 73
  const char* pump_uploader_display = pump_uploader["display"]; // "73%"
  int pump_uploader_level = pump_uploader["level"]; // 75

  // Capture the values used in the display update.
  batteryLevel = pump_uploader_display;
  lastUpdate = pump_data_clock_display;
  if(bgnow_sgvs_0_mgdl == bgnow_sgvs_0_scaled){
    bgLevel = String(bgnow_sgvs_0_mgdl);
  } else {
    bgLevel = String(bgnow_sgvs_0_scaled, 1);
  }
  insLevel = pump_data_reservoir_display;
  arrowDir = bgnow_sgvs_0_direction;

  if(lastUpdate == ""){
    lastUpdate = getElapsedTime(bgnow_mills);
  }

}

// Used to draw the arrows indicating the BG trend
void drawArrow(int x, int y, int asize, int aangle, int pwidth, int plength, uint16_t color){
  aangle = aangle - 2;
  float dx = (asize-10)*cos(aangle-90)*PI/180+x; // calculate X position  
  float dy = (asize-10)*sin(aangle-90)*PI/180+y; // calculate Y position  
  float x1 = 0;         float y1 = plength;
  float x2 = pwidth/2;  float y2 = pwidth/2;
  float x3 = -pwidth/2; float y3 = pwidth/2;
  float angle = aangle*PI/180-135;
  float xx1 = x1*cos(angle)-y1*sin(angle)+dx;
  float yy1 = y1*cos(angle)+x1*sin(angle)+dy;
  float xx2 = x2*cos(angle)-y2*sin(angle)+dx;
  float yy2 = y2*cos(angle)+x2*sin(angle)+dy;
  float xx3 = x3*cos(angle)-y3*sin(angle)+dx;
  float yy3 = y3*cos(angle)+x3*sin(angle)+dy;

  dispCanvas.fillTriangle(xx1,yy1,xx3,yy3,xx2,yy2, color);
  dispCanvas.drawLine(x, y, xx1, yy1, color);
  dispCanvas.drawLine(x+1, y, xx1+1, yy1, color);
  dispCanvas.drawLine(x, y+1, xx1, yy1+1, color);
  dispCanvas.drawLine(x-1, y, xx1-1, yy1, color);
  dispCanvas.drawLine(x, y-1, xx1, yy1-1, color);
  dispCanvas.drawLine(x+2, y, xx1+2, yy1, color);
  dispCanvas.drawLine(x, y+2, xx1, yy1+2, color);
  dispCanvas.drawLine(x-2, y, xx1-2, yy1, color);
  dispCanvas.drawLine(x, y-2, xx1, yy1-2, color);
}
