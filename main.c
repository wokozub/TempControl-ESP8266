#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#define ONE_WIRE_BUS D3
#define RELAY_PIN D8

#define BUTTON_SET D4
#define BUTTON_UP D5
#define BUTTON_DOWN D6

// DS18B20
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// variables
volatile float targetTemperature = 25.0;
volatile float hysteresis = 0.5;
volatile bool menuActive = false;
volatile int menuState = 0; // 0: Edit Target, 1: Edit Hysteresis, 2: IP Address
volatile unsigned long lastInterruptTime = 0; // Debounce
volatile unsigned long menuLastActiveTime = 0; // Last activity time in menu
const unsigned long menuTimeout = 3000; // miliseconds to auto exit

bool relayState = false;

// Interrupt
void IRAM_ATTR handleButtonUp() {
  if (millis() - lastInterruptTime > 200) {
    lastInterruptTime = millis();
    menuLastActiveTime = millis();
    if (menuActive) {
      if (menuState == 0) {
        targetTemperature += 1;
      } else if (menuState == 1) {
        hysteresis += 0.1;
      }
    }
  }
}

void IRAM_ATTR handleButtonDown() {
  if (millis() - lastInterruptTime > 200) {
    lastInterruptTime = millis();
    menuLastActiveTime = millis();
    if (menuActive) {
      if (menuState == 0) {
        targetTemperature -= 1;
      } else if (menuState == 1) {
        hysteresis -= 0.1;
      }
    }
  }
}

void IRAM_ATTR handleButtonSet() {
  if (millis() - lastInterruptTime > 200) {
    lastInterruptTime = millis();
    menuLastActiveTime = millis();
    if (menuActive) {
      menuState = (menuState + 1) % 3;
    } else {
      menuActive = true;
    }
  }
}

// WiFi credentials
const char* ssid = "Your_network";
const char* password = "password";

// web server init
ESP8266WebServer server(80);

void setup() {
  WiFi.begin(ssid, password);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Temp Regulator");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SET, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BUTTON_UP), handleButtonUp, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_DOWN), handleButtonDown, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_SET), handleButtonSet, FALLING);

  sensors.begin();

  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("WiFi");
  lcd.setCursor(0, 1);
  lcd.print("Connecting...");

  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 10) {
    delay(1000);
    retryCount++;
    lcd.setCursor(0, 1);
    lcd.print("Retry: ");
    lcd.print(retryCount);
    lcd.print("   ");
  }

  // chceck connection
  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.setCursor(0, 0);
    lcd.print("WiFi: Connected");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
  } else {
    lcd.setCursor(0, 0);
    lcd.print("WiFi: Failed");
    lcd.setCursor(0, 1);
    lcd.print("No Connection");
  }

  // endpoints
  server.on("/", HTTP_GET, handleRoot);  // main page
  server.on("/data", HTTP_GET, handleGetData);
  server.on("/settings", HTTP_POST, handlePostSettings);
  server.begin();

  delay(2000);
  lcd.clear();
}

void loop() {
  lcd.backlight();
  server.handleClient();

  sensors.requestTemperatures();
  float currentTemperature = sensors.getTempCByIndex(0);

  // relay logic
  if (currentTemperature < targetTemperature - hysteresis) {
    relayState = true;
  } else if (currentTemperature > targetTemperature + hysteresis) {
    relayState = false;
  }
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);

  // menu auto exit
  if (menuActive && millis() - menuLastActiveTime > menuTimeout) {
    // blinking backgroung LCD
    for (size_t i = 0; i < 4; i++)
    {
      lcd.noBacklight();
      delay(250);
      lcd.backlight();
      delay(250);
    }
    menuActive = false;
    menuState = 0;
    lcd.clear();
  }

  if (!menuActive) {
    // main screen
    lcd.setCursor(0, 0);
    lcd.print("TP: ");
    if (currentTemperature == DEVICE_DISCONNECTED_C) {
      lcd.print("Error");
    } else {
      lcd.print(currentTemperature, 1);
      lcd.print((char)223);
      lcd.print("C");
    }

    lcd.setCursor(10, 0);
    lcd.print(relayState ? "   ON " : "   OFF");

    lcd.setCursor(0, 1);
    lcd.print("TS: ");
    lcd.print(targetTemperature, 1);
    lcd.print((char)223);
    lcd.print("C      ");
  } else {
    // active menu
    if (menuState == 0) {
      lcd.setCursor(0, 0);
      lcd.print("Edit Target Temp");
      lcd.setCursor(0, 1);
      lcd.print("TS: ");
      lcd.print(targetTemperature, 1);
      lcd.print((char)223);
      lcd.print("C     ");
    } else if (menuState == 1) {
      lcd.setCursor(0, 0);
      lcd.print("Edit Hysteresis ");
      lcd.setCursor(0, 1);
      lcd.print("H: ");
      lcd.print(hysteresis, 1);
      lcd.print((char)223);
      lcd.print("C    ");
    } else if (menuState == 2) {
      lcd.setCursor(0, 0);
      lcd.print("IP address      ");
      lcd.setCursor(0, 1);
      lcd.print(WiFi.localIP());
    }
  }
  delay(100);
}

void handleGetData() {
  float temperature = sensors.getTempCByIndex(0); 

  // JSON
  String json = "{";
  json += "\"temperature\": " + String(temperature, 1) + ",";
  json += "\"targetTemperature\": " + String(targetTemperature, 1) + ",";
  json += "\"hysteresis\": " + String(hysteresis, 1) + ",";
  json += "\"relayState\": " + String(relayState ? "true" : "false") + ",";
  json += "}";

  // Wysłanie danych w formacie JSON
  server.send(200, "application/json", json);
}

void handlePostSettings() {
  if (server.hasArg("targetTemperature")) {
    targetTemperature = server.arg("targetTemperature").toFloat();
  }
  if (server.hasArg("hysteresis")) {
    hysteresis = server.arg("hysteresis").toFloat();
  }

  // response
  String response = "{\"status\": \"OK\", \"message\": \"Settings updated\"}";
  server.send(200, "application/json", response);
}

void handleRoot() {
  String html = "<html><head><meta charset=\"UTF-8\"><meta http-equiv=\"refresh\"
                content=\"5\"></head><body style='font-family: Arial, sans-serif; color: #333;'>";

  html += "<h1>Regulator temperatury</h1>";
  
  html += "<p><strong>Aktualna temperatura (TP):</strong> ";
  html += String(sensors.getTempCByIndex(0), 1) + " &deg;C</p>";
  
  html += "<p><strong>Docelowa temperatura (TS):</strong> ";
  html += String(targetTemperature, 1) + " &deg;C</p>";
  
  html += "<p><strong>Histereza (H):</strong> ";
  html += String(hysteresis, 1) + " &deg;C</p>";
  
  html += "<p><strong>Stan przekaźnika:</strong> ";
  if (relayState) {
    html += "<span style='color: green;'>Włączony</span>";
  } else {
    html += "<span style='color: red;'>Wyłączony</span>";
  }
  html += "</p>";

  // Form
  html += "<h3>Aktualizuj ustawienia</h3>";
  html += "<form action='/settings' method='POST'>";
  html += "<label for='targetTemperature'>Docelowa temperatura (TS):</label>";
  html += "<input type='number' id='targetTemperature' name='targetTemperature' step='0.1' value='" + String(targetTemperature, 1) + "'><br><br>";
  html += "<label for='hysteresis'>Histereza:</label>";
  html += "<input type='number' id='hysteresis' name='hysteresis' step='0.1' value='" + String(hysteresis, 1) + "'><br><br>";
  html += "<input type='submit' value='Zaktualizuj'>";
  html += "</form>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}