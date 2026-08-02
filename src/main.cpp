#include <Arduino.h>
#include <BleKeyboard.h>
#include <Preferences.h>
#include <secrets.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

BleKeyboard bleKeyboard;
Preferences prefs;
AsyncWebServer server(80);

const char* NVS_KEY_KEYCODES = "keycodes";
const int rows[3] = {4, 5, 6};
const int columns[5] = {7, 15, 16, 17, 18};
uint8_t keycodes[3][5][4] = {
  {
    { 'a', 0, 0, 0 },                         
    { 'c', 0, 0, 0 },                                    
    { KEY_LEFT_CTRL, 'c', 0, 0 },                        
    { KEY_LEFT_CTRL, 'v', 0, 0 },                        
    { KEY_LEFT_CTRL, 'z', 0, 0 },                        
  },
  {
    { KEY_LEFT_CTRL, 'x', 0, 0 },                        
    { KEY_LEFT_CTRL, 'a', 0, 0 },                        
    { KEY_LEFT_CTRL, KEY_LEFT_SHIFT, KEY_ESC, 0 },       
    { KEY_LEFT_ALT, KEY_TAB, 0, 0 },                     
    { KEY_LEFT_GUI, 'd', 0, 0 },                         
  },
  {
    { KEY_RETURN, 0, 0, 0 },                             
    { KEY_ESC, 0, 0, 0 },                                
    { KEY_TAB, 0, 0, 0 },                                
    { KEY_LEFT_CTRL, KEY_LEFT_ALT, KEY_DELETE, 0 },      
    { KEY_BACKSPACE, 0, 0, 0 },                          
  },
};

bool lastState[3][5] = {false};
bool lastRawState[3][5] = {false};
unsigned long lastChangeTime[3][5] = {0};
const int delayTime = 10;

bool checkKeyAction(int row, int column, bool currentState) {
  if (currentState != lastRawState[row][column]) {
    lastRawState[row][column] = currentState;
    lastChangeTime[row][column] = millis();
  }
  if (currentState != lastState[row][column]) {
    if (millis() - lastChangeTime[row][column] >= delayTime) {
      lastState[row][column] = currentState;
      return true; 
    }
  }
  return false;
}

void typeKeys(bool currentState, int row, int column) {
  if (currentState) {
    for (int key = 0; key < 4; key++) {
      if (keycodes[row][column][key] == 0) break;
      bleKeyboard.press(keycodes[row][column][key]);
    }
  }
  else {
    for (int key = 0; key < 4; key++) {
      if (keycodes[row][column][key] == 0) break;
      bleKeyboard.release(keycodes[row][column][key]);
    }
  }
}

String buildPage() {
  String html = "<html><body><form action='/save' method='POST'>";
  for (int row = 0; row < 3; row++) {
    html += "<div>";
    for (int column = 0; column < 5; column++) {
      html += "<fieldset><legend>Taste " + String(row+1) + "/" + String(column+1) + "</legend>";
      for (int key = 0; key < 4; key++) {
        html += String("<input name='key_") + row + "_" + column + "_" + key + "' value='" + String(keycodes[row][column][key]) + "'>";
      }
      html += "</fieldset>";
    }
    html += "</div>";
  }
  html += "<button type='submit'>Speichern</button>";
  html += "</form></body></html>";
  return html;
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  pinMode(7, INPUT_PULLDOWN);
  pinMode(15, INPUT_PULLDOWN);
  pinMode(16, INPUT_PULLDOWN);
  pinMode(17, INPUT_PULLDOWN);
  pinMode(18, INPUT_PULLDOWN);

  bleKeyboard.begin();

  prefs.begin("makropad", true);
  if (prefs.isKey("keycodes")) {
    prefs.getBytes(NVS_KEY_KEYCODES, keycodes, sizeof(keycodes));
  }
  prefs.end();

  WiFi.mode(WIFI_STA);
  Serial.println("Verfügbare Netzwerke:");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    Serial.println(WiFi.SSID(i));
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Verbunden, IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildPage());
  });
  
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    for (int row = 0; row < 3; row++) {
      for (int column = 0; column < 5; column++) {
        for (int key = 0; key < 4; key++) {
          String value = request->getParam(String("key_") + row + "_" + column + "_" + key, true)->value();
          keycodes[row][column][key] = value.toInt();
        }
      }
    }
    prefs.begin("makropad", false);
    prefs.putBytes(NVS_KEY_KEYCODES, keycodes, sizeof(keycodes));
    prefs.end();
    request->send(200, "text/plain", "Gespeichert!");
  });

  server.begin();
}

void loop() {
  if (bleKeyboard.isConnected()) {
    for (int row = 0; row < 3; row++) {
      digitalWrite(rows[row], HIGH);
      for (int column = 0; column < 5; column++) {
        bool currentState = digitalRead(columns[column]);
        if (checkKeyAction(row, column, currentState)) {
          typeKeys(currentState, row, column);
        }
      }
      digitalWrite(rows[row], LOW);
    }
  }
}