#include <Arduino.h>
#include <map>
#include <LittleFS.h>
#include <BleKeyboard.h>
#include <Preferences.h>
#include <secrets.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <EPD_1in54g.h>
#include <EPaperDisplay.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

BleKeyboard bleKeyboard;
Preferences prefs;
AsyncWebServer server(80);
HTTPClient http;
EPaperDisplay epaper;

const unsigned long pollIntervall = 40 * 1000;
unsigned long lastPollTime = 0;
String fluxQuery;

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
  float coretemp_package_id_0;     // CPU Durchschnittstemperatur aller Kerne
  float coretemp_core_0;           // Temperatur Kern 1
  float coretemp_core_1;           // Temperatur Kern 2
  float coretemp_core_2;           // Temperatur Kern 3
  float coretemp_core_3;           // Temperatur Kern 4
  float coretemp_core_4;           // Temperatur Kern 5
  float coretemp_core_5;           // Temperatur Kern 6
  float pch_cometlake;             // Chipsatz Temperatur
  float cpu_usage;                 // ~Usage idle -> 100 - cpu_usage = tatsächliche Auslastung
  float disk_used;                 // Auslastung in Bytes
  float disk_used_percent;         // Auslastung in Prozent
  float memory_used;               // Auslastung in Bytes
  float memory_used_percent;       // Auslastung in Prozent
} sensorData;

std::map<String, float*> fieldMap = {
  {"coretemp_package_id_0", &sensorData.coretemp_package_id_0},
  {"coretemp_core_0", &sensorData.coretemp_core_0},
  {"coretemp_core_1", &sensorData.coretemp_core_1},
  {"coretemp_core_2", &sensorData.coretemp_core_2},
  {"coretemp_core_3", &sensorData.coretemp_core_3},
  {"coretemp_core_4", &sensorData.coretemp_core_4},
  {"coretemp_core_5", &sensorData.coretemp_core_5},
  {"pch_cometlake", &sensorData.pch_cometlake},
  {"cpu_usage", &sensorData.cpu_usage},
  {"disk_used", &sensorData.disk_used},
  {"disk_used_percent", &sensorData.disk_used_percent},
  {"memory_used", &sensorData.memory_used},
  {"memory_used_percent", &sensorData.memory_used_percent},
};

int displayMode = 0;
bool refreshDisplay = true;

QueueHandle_t displayQueue;

struct DisplayUpdate {
  int mode;
  SensorData data;
  bool isError;
  int errorCode;
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

void checkKeyPress() {
  if (bleKeyboard.isConnected()) {
    for (int row = 0; row < 3; row++) {
      digitalWrite(rows[row], HIGH);
      for (int column = 0; column < 5; column++) {
        bool currentState = digitalRead(columns[column]);
        if (checkKeyAction(row, column, currentState)) {
          if (column != 4) {
            typeKeys(currentState, row, column);
          } else if (column == 4 and row == 0 and currentState) {
            if (refreshDisplay) {
              refreshDisplay = false;
            } else {
              refreshDisplay = true;
            }
          } else if (column == 4 and row == 1 and currentState) {
            if (displayMode >= 3) {
              displayMode = 0;
              Serial.println(displayMode);
              DisplayUpdate update;
              update.mode = displayMode;
              update.data = sensorData;
              update.isError = false;
              xQueueSend(displayQueue, &update, 0);
            } else {
              displayMode++;
              Serial.println(displayMode);
              DisplayUpdate update;
              update.mode = displayMode;
              update.data = sensorData;
              update.isError = false;
              xQueueSend(displayQueue, &update, 0);
            }
          } else if (column == 4 and row == 2 and currentState) {
            if (displayMode <= 0) {
              displayMode = 3;
              Serial.println(displayMode);
              DisplayUpdate update;
              update.mode = displayMode;
              update.data = sensorData;
              update.isError = false;
              xQueueSend(displayQueue, &update, 0);
            } else {
              displayMode--;
              Serial.println(displayMode);
              DisplayUpdate update;
              update.mode = displayMode;
              update.data = sensorData;
              update.isError = false;
              xQueueSend(displayQueue, &update, 0);
            }
          }
        }
      }
      digitalWrite(rows[row], LOW);
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
    
    // Weißt den Sensoren die Daten zu
    auto it = fieldMap.find(sensor);
    if (it != fieldMap.end()) {
      *(it->second) = value.toFloat();
    }
  }
  Serial.println("package: " + String(sensorData.coretemp_package_id_0));
  Serial.println("cometlake: " + String(sensorData.pch_cometlake));
  Serial.println("usage: " + String(sensorData.cpu_usage));
  Serial.println("core 0: " + String(sensorData.coretemp_core_0));
  Serial.println("core 1: " + String(sensorData.coretemp_core_1));
  Serial.println("core 2: " + String(sensorData.coretemp_core_2));
  Serial.println("core 3: " + String(sensorData.coretemp_core_3));
  Serial.println("core 4: " + String(sensorData.coretemp_core_4));
  Serial.println("core 5: " + String(sensorData.coretemp_core_5));
  Serial.println("Used space: " + String(sensorData.disk_used));
  Serial.println("Used space: " + String(sensorData.disk_used_percent));
  Serial.println("Used memory: " + String(sensorData.memory_used));
  Serial.println("Used memory: " + String(sensorData.memory_used_percent));
}

void drawErrorDisplay(int error) {
  epaper.fillScreen(EPD_1IN54G_WHITE);
  epaper.setTextColor(EPD_1IN54G_BLACK);
  epaper.setTextSize(2);
  epaper.setCursor(0, 10);
  epaper.println(String(error));
  epaper.display();
  EPD_1IN54G_Sleep();
}

bool getServerData() {
  http.begin("http://192.168.178.73:8086/api/v2/query?org=home");
  http.addHeader("Authorization", "Token " + String(INFLUX_TOKEN));
  http.addHeader("Content-Type", "application/vnd.flux");
  http.setTimeout(10000);

  if (pollIntervall < millis() - lastPollTime) {
    int httpCode = http.POST(fluxQuery);
    lastPollTime = millis();
    if (httpCode == 200) {
      String response = http.getString();
      evaluateString(response);
      http.end();
      return true;
    } else {
      String errorBody = http.getString();
      Serial.println("HTTP " + String(httpCode) + ": " + errorBody);
      http.end();
      DisplayUpdate update;
      update.isError = true;
      update.errorCode = httpCode;
      xQueueSend(displayQueue, &update, 0);
      return false;
    }
  }
  http.end();
  return false;
}

void drawModeIndicator(int mode) {
  int cx = 160, cy = 195, spacing = 10, r = 3;
  for (int i = 0; i < 4; i++) {
    int x = cx + i * spacing;
    if (i == mode) {
      epaper.fillCircle(x, cy, r, EPD_1IN54G_BLACK);
    } else {
      epaper.drawCircle(x, cy, r, EPD_1IN54G_BLACK);
    }
  }
}

void drawCpuMode(SensorData data, int mode) {
  epaper.fillScreen(EPD_1IN54G_WHITE);
  epaper.setTextColor(EPD_1IN54G_BLACK);

  epaper.setTextSize(2);
  epaper.setCursor(5, 5);
  epaper.print("CPU");

  // Große zentrierte Auslastungsanzeige
  float usage = 100 - data.cpu_usage;
  String usageStr = String((int)usage) + "%";
  epaper.setTextSize(4);
  int16_t x1, y1; uint16_t w, h;
  epaper.getTextBounds(usageStr, 0, 0, &x1, &y1, &w, &h); // berechnet Pixelbreite/-höhe des Texts, um ihn zu zentrieren
  epaper.setCursor((200 - w) / 2, 40);
  epaper.print(usageStr);

  // 6 Balken für die Kerntemperaturen, Höhe skaliert zwischen 30-70°C
  float temps[6] = {
    data.coretemp_core_0, data.coretemp_core_1,
    data.coretemp_core_2, data.coretemp_core_3,
    data.coretemp_core_4, data.coretemp_core_5
  };
  int barTop = 90, barBottom = 150, barW = 20, gap = 6;
  int startX = (200 - (6 * barW + 5 * gap)) / 2;
  for (int i = 0; i < 6; i++) {
    int barHeight = map(constrain((int)temps[i], 30, 70), 30, 70, 5, barBottom - barTop);
    int x = startX + i * (barW + gap);
    epaper.drawRect(x, barTop, barW, barBottom - barTop, EPD_1IN54G_BLACK);
    epaper.fillRect(x, barBottom - barHeight, barW, barHeight, EPD_1IN54G_BLACK);
  }

  epaper.setTextSize(2);
  epaper.setCursor(5, 170);
  epaper.print("Pkg " + String((int)data.coretemp_package_id_0) + "C  PCH " + String((int)data.pch_cometlake) + "C");

  drawModeIndicator(mode);

  epaper.display();
}

void drawDiskMode(SensorData data, int mode) {
  epaper.fillScreen(EPD_1IN54G_WHITE);
  epaper.setTextColor(EPD_1IN54G_BLACK);

  epaper.setTextSize(2);
  epaper.setCursor(5, 5);
  epaper.print("DISK");

  float percent = data.disk_used_percent;
  int cx = 100, cy = 105, radius = 70, thickness = 14;

  // Ring als Gauge: äußerer und innerer Kreisrand
  epaper.drawCircle(cx, cy, radius, EPD_1IN54G_BLACK);
  epaper.drawCircle(cx, cy, radius - thickness, EPD_1IN54G_BLACK);

  // Füllung des Rings per Radial-Linien, ein Grad pro Prozentpunkt*3.6
  int steps = (int)(percent * 3.6);
  for (int deg = 0; deg < steps; deg++) {
    float rad = (deg - 90) * PI / 180.0; // -90 = Start bei 12 Uhr statt 3 Uhr
    int xOuter = cx + cos(rad) * radius;
    int yOuter = cy + sin(rad) * radius;
    int xInner = cx + cos(rad) * (radius - thickness);
    int yInner = cy + sin(rad) * (radius - thickness);
    epaper.drawLine(xInner, yInner, xOuter, yOuter, EPD_1IN54G_BLACK);
  }

  String percentStr = String((int)percent) + "%";
  epaper.setTextSize(3);
  int16_t x1, y1; uint16_t w, h;
  epaper.getTextBounds(percentStr, 0, 0, &x1, &y1, &w, &h);
  epaper.setCursor(cx - w / 2, cy - h / 2);
  epaper.print(percentStr);

  float usedGB = data.disk_used / 1073741824.0;
  String usedStr = String(usedGB, 1) + " GB";
  epaper.setTextSize(1);
  epaper.getTextBounds(usedStr, 0, 0, &x1, &y1, &w, &h);
  epaper.setCursor(cx - w / 2, cy + 22);
  epaper.print(usedStr);

  drawModeIndicator(mode);

  epaper.display();
}

void drawMemoryMode(SensorData data, int mode) {
  epaper.fillScreen(EPD_1IN54G_WHITE);
  epaper.setTextColor(EPD_1IN54G_BLACK);

  epaper.setTextSize(2);
  epaper.setCursor(5, 5);
  epaper.print("MEMORY");

  float percent = data.memory_used_percent;
  int barX = 70, barY = 30, barW = 60, barH = 130;

  epaper.drawRect(barX, barY, barW, barH, EPD_1IN54G_BLACK);
  int fillH = (int)(barH * percent / 100.0);
  epaper.fillRect(barX, barY + barH - fillH, barW, fillH, EPD_1IN54G_BLACK);

  // Markierungsstriche bei 25/50/75%
  for (int p = 25; p < 100; p += 25) {
    int y = barY + barH - (int)(barH * p / 100.0);
    epaper.drawLine(barX - 6, y, barX, y, EPD_1IN54G_BLACK);
  }

  String percentStr = String((int)percent) + "%";
  epaper.setTextSize(2);
  int16_t x1, y1; uint16_t w, h;
  epaper.getTextBounds(percentStr, 0, 0, &x1, &y1, &w, &h);
  epaper.setCursor(100 - w / 2, 175);
  epaper.print(percentStr);

  float usedGB = data.memory_used / 1073741824.0;
  String usedStr = String(usedGB, 1) + " GB";
  epaper.setTextSize(2);
  epaper.getTextBounds(usedStr, 0, 0, &x1, &y1, &w, &h);
  epaper.setCursor(200 - w - 5, 5);
  epaper.print(usedStr);

  drawModeIndicator(mode);

  epaper.display();
}

void drawMixedMode(SensorData data, int mode) {
  epaper.fillScreen(EPD_1IN54G_WHITE);
  epaper.setTextColor(EPD_1IN54G_BLACK);

  // Kreuz-Trennlinien für 4 Quadranten
  epaper.drawLine(100, 0, 100, 200, EPD_1IN54G_BLACK);
  epaper.drawLine(0, 100, 200, 100, EPD_1IN54G_BLACK);

  epaper.setTextSize(2);
  epaper.setCursor(10, 10);
  epaper.print("CPU");
  epaper.setTextSize(2);
  epaper.setCursor(30, 55);
  epaper.print(String((int)(100 - data.cpu_usage)) + "%");

  epaper.setTextSize(2);
  epaper.setCursor(110, 10);
  epaper.print("TEMP");
  epaper.setTextSize(2);
  epaper.setCursor(130, 55);
  epaper.print(String((int)data.coretemp_package_id_0) + "C");

  epaper.setTextSize(2);
  epaper.setCursor(10, 110);
  epaper.print("DISK");
  epaper.setTextSize(2);
  epaper.setCursor(30, 155);
  epaper.print(String((int)data.disk_used_percent) + "%");

  epaper.setTextSize(2);
  epaper.setCursor(110, 110);
  epaper.print("MEM");
  epaper.setTextSize(2);
  epaper.setCursor(130, 155);
  epaper.print(String((int)data.memory_used_percent) + "%");

  drawModeIndicator(mode);

  epaper.display();
}

void drawDashboard(SensorData data, int mode) {
  switch (mode) {
    case 0:
      drawMixedMode(data, mode);
      break;
    case 1:
      drawCpuMode(data, mode);
      break;
    case 2:
      drawDiskMode(data, mode);
      break;
    case 3:
      drawMemoryMode(data, mode);
      break;
  }
}

void displayTask(void *parameter) {
  DisplayUpdate update;
  for (;;) {
    if (xQueueReceive(displayQueue, &update, portMAX_DELAY)) {
      Serial.println("Empfangen: " + String(update.data.disk_used_percent));
      if (update.isError) {
        drawErrorDisplay(update.errorCode);
      } else {
        drawDashboard(update.data, update.mode);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  LittleFS.begin();
  File file = LittleFS.open("/query.flux", "r");
  fluxQuery = file.readString();
  file.close(); 
  
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

  DEV_Module_Init();
  EPD_1IN54G_Init();

  displayQueue = xQueueCreate(1, sizeof(DisplayUpdate));

  xTaskCreatePinnedToCore(
    displayTask,
    "DisplayTask",
    4096,
    NULL,
    1,
    NULL,
    0
  );
}

void loop() {
  checkKeyPress();

  if (refreshDisplay) {
    if (getServerData()) {
      DisplayUpdate update;
      update.mode = displayMode;
      update.data = sensorData;
      update.isError = false;
      Serial.println("Vor Queue-Send: " + String(update.data.disk_used_percent));
      xQueueSend(displayQueue, &update, 0);
    }
  }
}