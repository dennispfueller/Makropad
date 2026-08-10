#include <Arduino.h>
#include <BleKeyboard.h>
#include <Preferences.h>
#include <secrets.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <EPD_1in54g.h>
#include <EPaperDisplay.h>

BleKeyboard bleKeyboard;
Preferences prefs;
AsyncWebServer server(80);
HTTPClient http;
EPaperDisplay epaper;

const unsigned long pollIntervall = 40 * 1000;
unsigned long lastPollTime = 0;
String fluxQuery = "tempData = from(bucket: \"monitoring\") |> range(start: -30s) |> filter(fn: (r) => r._measurement == \"temp\") |> last() |> map(fn: (r) => ({_time: r._time, id: r.sensor, _value: r._value}))\n\ncpuData = from(bucket: \"monitoring\") |> range(start: -30s) |> filter(fn: (r) => r._measurement == \"cpu\" and r.cpu == \"cpu-total\" and r._field == \"usage_idle\") |> last() |> map(fn: (r) => ({_time: r._time, id: \"cpu_usage\", _value: r._value}))\n\nunion(tables: [tempData, cpuData]) |> keep(columns: [\"_time\", \"id\", \"_value\"])";

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

struct ColumnIndices {
  int valueIndex;
  int sensorIndex;
} columnIndices;

struct SensorData {
  String coretemp_package_id_0;
  String coretemp_core_0;
  String cpu_usage;
  String pch_cometlake;
} sensorData;

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

void findValueIndex(String row) {
  row.trim();
  int index = 0;
  int startPosition = 0;
  int endPosition = 0;
  while (endPosition != -1) {
    endPosition = row.indexOf(",", startPosition);
    if (row.substring(startPosition, endPosition) == "_value") {
      columnIndices.valueIndex = index;
    }
    if (row.substring(startPosition, endPosition) == "id") {
      columnIndices.sensorIndex = index;
    }
    index++;
    startPosition = endPosition + 1;
  }
}

String findValue(String row, int searchIndex) {
  row.trim();
  int index = 0;
  int startPosition = 0;
  int endPosition = 0;
  while (endPosition != -1) {
    endPosition = row.indexOf(",", startPosition);
    if (index == searchIndex) {
      if (endPosition == -1) {
        return row.substring(startPosition);
      }
      return row.substring(startPosition, endPosition);
    }
    index++;
    startPosition = endPosition + 1;
  }
  return "";
}

void evaluateString(String string) {
  int index = 0;
  int startPosition = 0;
  int endPosition = 0;
  String value;
  String sensor;
  while (endPosition != -1) {
    endPosition = string.indexOf("\n", startPosition);
    if (index == 0) {
      if (endPosition == -1) {
        findValueIndex(string.substring(startPosition));
      } else {
        findValueIndex(string.substring(startPosition, endPosition));
      }  
      Serial.println("SensorIndex: " + columnIndices.sensorIndex);
      Serial.println("ValueIndex: " + columnIndices.valueIndex);
    } else if (endPosition == -1) {
      value = findValue(string.substring(startPosition), columnIndices.valueIndex);
      sensor = findValue(string.substring(startPosition), columnIndices.sensorIndex);
    } else {
      value = findValue(string.substring(startPosition, endPosition), columnIndices.valueIndex);
      sensor = findValue(string.substring(startPosition, endPosition), columnIndices.sensorIndex);
    }
    index++;
    startPosition = endPosition + 1;
    Serial.println("Sensor: " + sensor);
    Serial.println("Value: " + value);

    if (sensor == "coretemp_package_id_0") {
      sensorData.coretemp_package_id_0 = value;
    }
    else if (sensor == "coretemp_core_0") {
      sensorData.coretemp_core_0 = value;
    }
    else if (sensor == "pch_cometlake") {
      sensorData.pch_cometlake = value;
    }
    else if (sensor == "cpu_usage") {
      sensorData.cpu_usage = value;
    }
  }
  Serial.println("package: " + sensorData.coretemp_package_id_0);
  Serial.println("cometlake: " + sensorData.pch_cometlake);
  Serial.println("usage: " + sensorData.cpu_usage);
  Serial.println("core 0: " + sensorData.coretemp_core_0);
}

bool getServerData() {
  if (pollIntervall < millis() - lastPollTime) {
    int httpCode = http.POST(fluxQuery);
    lastPollTime = millis();
    if (httpCode == 200) {
      String response = http.getString();
      evaluateString(response);
      return true;
    } else {
      return false;
      // Fehler auf Display anzeigen
    }
  }
  return false;
}

void checkKeyPress() {
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

void drawDashboard() {
  DEV_Module_Init();
  EPD_1IN54G_Init();
  epaper.fillScreen(EPD_1IN54G_WHITE);
  epaper.setTextColor(EPD_1IN54G_BLACK);
  epaper.setTextSize(2);
  epaper.setCursor(0, 10);
  epaper.println("CPU Temp: " + sensorData.coretemp_package_id_0 + "C");
  epaper.println("MB Temp: " + sensorData.pch_cometlake + "C");
  String cpuUsage = String(100 - sensorData.cpu_usage.toFloat(), 2);
  cpuUsage.replace(".", ",");
  epaper.println("CPU Usage: " + cpuUsage + "%");
  epaper.display();
  EPD_1IN54G_Sleep();
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

  http.begin("http://192.168.178.73:8086/api/v2/query?org=home");
  http.addHeader("Authorization", "Token " + String(INFLUX_TOKEN));
  http.addHeader("Content-Type", "application/vnd.flux");
}

void loop() {
  checkKeyPress();

  if (getServerData()) {
    drawDashboard();
  }
}