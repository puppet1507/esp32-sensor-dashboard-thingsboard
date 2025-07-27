/*
 * ESP32 WiFi + Sensors with SH1106 OLED Display and ThingsBoard
 * Interface optimized for 1.3 inch display (128x64)
 * Connections:
 * - SH1106: VCC->3.3V, GND->GND, SCL->GPIO22, SDA->GPIO21
 * - DHT22: VCC->3.3V, GND->GND, Data->GPIO4
 * - MQ135: VCC->5V, GND->GND, A0->GPIO34
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <SH1106.h>
#include <DHT.h>
#include <PubSubClient.h>

SH1106 display;

#define SDA_PIN 21
#define SCL_PIN 22

#define DHT_PIN 4
#define DHT_TYPE DHT22
#define MQ135_PIN 34

DHT dht(DHT_PIN, DHT_TYPE);

// WiFi information
const char* ssid = "MyWiFiNetwork";
const char* password = "MyWiFiPassword";

// ThingsBoard information
const char* mqtt_server = "cloud.thingsboard.io";
const char* mqtt_token = "YOUR_THINGSBOARD_DEVICE_TOKEN";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// Status variables
bool wifiConnected = false;
bool wifiConnecting = true;
bool mqttConnected = false;
int dotCount = 0;
unsigned long lastUpdate = 0;
unsigned long wifiStartTime = 0;
unsigned long lastWifiRetry = 0;
unsigned long lastMqttRetry = 0;

// Sensor variables
float temperature = 0.0;
float humidity = 0.0;
int analogValue = 0;

// Timeout and retry intervals
const unsigned long WIFI_TIMEOUT = 10000;
const unsigned long WIFI_RETRY_INTERVAL = 60000;
const unsigned long SENSOR_UPDATE_INTERVAL = 2000;
const unsigned long MQTT_RETRY_INTERVAL = 10000;
unsigned long lastSensorUpdate = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 WiFi + Sensors + OLED + ThingsBoard ===");
  
  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(SH1106_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.display();
  
  dht.begin();
  client.setServer(mqtt_server, mqtt_port);
  startWiFiConnection();
}

void loop() {
  unsigned long currentTime = millis();
  
  // Read sensors
  if (currentTime - lastSensorUpdate > SENSOR_UPDATE_INTERVAL) {
    lastSensorUpdate = currentTime;
    readSensors();
    if (wifiConnected && mqttConnected) {
      publishSensorData();
    }
  }

  // Update display
  if (currentTime - lastUpdate > 500) {
    lastUpdate = currentTime;

    if (wifiConnecting) {
      updateConnectingDisplay();
      checkWiFiStatus();
      if (currentTime - wifiStartTime > WIFI_TIMEOUT) {
        wifiConnecting = false;
        wifiConnected = false;
        Serial.println("WiFi timeout - switching to sensor display mode");
      }
    } else {
      updateSensorDisplay();
      if (!wifiConnected && (currentTime - lastWifiRetry > WIFI_RETRY_INTERVAL)) {
        Serial.println("Retrying WiFi connection...");
        startWiFiConnection();
      }
    }
  }

  // Maintain MQTT connection
  if (wifiConnected && !client.connected() && (currentTime - lastMqttRetry > MQTT_RETRY_INTERVAL)) {
    reconnectMQTT();
  }
  
  client.loop();
  delay(10);
}

void startWiFiConnection() {
  Serial.println("Starting WiFi connection...");
  wifiConnecting = true;
  wifiStartTime = millis();
  lastWifiRetry = millis();

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("ESP32 Starting...");
  display.println("");
  display.println("Connecting WiFi...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("Connecting to: %s\n", ssid);
}

void updateConnectingDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WiFi Connecting...");
  display.drawFastHLine(0, 10, 128, WHITE);
  display.setCursor(0, 15);
  display.printf("SSID: %s", ssid);
  display.setCursor(0, 25);
  display.print("Status: ");
  for(int i = 0; i < dotCount; i++) {
    display.print(".");
  }
  unsigned long timeLeft = (WIFI_TIMEOUT - (millis() - wifiStartTime)) / 1000;
  display.setCursor(0, 35);
  display.printf("Timeout: %lu s", timeLeft > 0 ? timeLeft : 0);
  display.display();
  dotCount = (dotCount + 1) % 4;
}

void checkWiFiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiConnecting = false;
    Serial.println("WiFi connected!");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println("WiFi Connected!");
    display.printf("IP: %s", WiFi.localIP().toString().c_str());
    display.display();
    delay(2000);

    reconnectMQTT();
  }
}

void reconnectMQTT() {
  lastMqttRetry = millis();
  Serial.print("Connecting to ThingsBoard MQTT...");
  if (client.connect("ESP32_Device", mqtt_token, NULL)) {
    mqttConnected = true;
    Serial.println("Connected!");
  } else {
    mqttConnected = false;
    Serial.print("Failed, rc=");
    Serial.println(client.state());
  }
}

void readSensors() {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  int analogValueRaw = analogRead(MQ135_PIN);
  analogValue = map(analogValueRaw, 0, 4095, 0, 1023);

  if (isnan(temperature)) temperature = -999;
  if (isnan(humidity)) humidity = -999;

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" °C");

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  Serial.print("MQ135 raw: ");
  Serial.print(analogValueRaw);
  Serial.print(" | Scaled: ");
  Serial.print(analogValue);
  Serial.println();
  Serial.println("-------------------");
}

void publishSensorData() {
  if (temperature != -999 && humidity != -999) {
    char payload[100];
    snprintf(payload, sizeof(payload), "{\"temperature\":%.1f,\"humidity\":%.1f,\"mq135\":%d}", 
             temperature, humidity, analogValue);
    
    client.publish("v1/devices/me/telemetry", payload);
    Serial.println("Data sent to ThingsBoard: ");
    Serial.println(payload);
  } else {
    Serial.println("Invalid sensor data, not sending!");
  }
}

// Function to draw temperature icon
void drawTemperatureIcon(int x, int y) {
  display.fillCircle(x + 3, y + 12, 4, WHITE);  
  display.drawRect(x + 1, y, 4, 10, WHITE);     
  display.fillRect(x + 2, y + 7, 2, 3, WHITE);  
  for(int i = 0; i < 4; i++) {
    display.drawPixel(x + 6, y + 2 + i*2, WHITE);
  }
}

// Function to draw humidity icon
void drawHumidityIcon(int x, int y) {
  
  display.fillCircle(x + 3, y + 10, 4, WHITE);
  display.fillTriangle(x + 3, y, x - 1, y + 7, x + 7, y + 7, WHITE);
  display.drawPixel(x + 2, y + 8, BLACK);
  display.drawPixel(x + 1, y + 9, BLACK);
}

// Function to draw air quality icon
void drawAirQualityIcon(int x, int y) {
  display.fillCircle(x + 2, y + 8, 3, WHITE);
  display.fillCircle(x + 6, y + 7, 3, WHITE);
  display.fillCircle(x + 10, y + 8, 3, WHITE);
  display.fillCircle(x + 4, y + 10, 3, WHITE);
  display.fillCircle(x + 8, y + 10, 3, WHITE);
  display.drawPixel(x + 1, y + 5, WHITE);
  display.drawPixel(x + 4, y + 4, WHITE);
  display.drawPixel(x + 7, y + 5, WHITE);
  display.drawPixel(x + 11, y + 5, WHITE);
}

// Function to draw WiFi icon 
void drawWiFiIcon(int x, int y, bool connected) {
  if (connected) {
    display.drawFastVLine(x, y + 6, 2, WHITE);     
    display.drawFastVLine(x + 2, y + 4, 4, WHITE); 
    display.drawFastVLine(x + 4, y + 2, 6, WHITE); 
    display.drawFastVLine(x + 6, y, 8, WHITE);     
  }
}

// Function to draw progress bar for sensor values
void drawProgressBar(int x, int y, int width, int height, int value, int maxValue) {
  display.drawRect(x, y, width, height, WHITE);
  int fillWidth = map(value, 0, maxValue, 0, width - 2);
  if (fillWidth > 0) {
    display.fillRect(x + 1, y + 1, fillWidth, height - 2, WHITE);
  }
}

void updateSensorDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SENSOR MONITOR");
  if (wifiConnected) {
    drawWiFiIcon(118, 2, true);
  }
  
  // Header separator line
  display.drawFastHLine(0, 12, 128, WHITE);
  
  // === SENSOR DATA AREA ===
  
  // Temperature - Left side
  drawTemperatureIcon(5, 18);
  display.setTextSize(1);  
  display.setCursor(20, 24);  
  if (temperature != -999) {
    display.printf("%.1f", temperature);
    display.drawCircle(display.getCursorX() + 2, 25, 1, WHITE);  
    display.setCursor(display.getCursorX() + 5, 24);  
    display.print("C");
  } else {
    display.setCursor(20, 24);
    display.println("ERROR");
  }
  
  // Vertical separator
  display.drawFastVLine(64, 15, 25, WHITE);
  
  // Humidity - Right side
  drawHumidityIcon(70, 18);
  display.setTextSize(1);  
  display.setCursor(85, 24); 
  if (humidity != -999) {
    display.printf("%.1f", humidity);
    display.setCursor(display.getCursorX() + 1, 24);
    display.print("%");
  } else {
    display.setCursor(85, 24);
    display.println("ERROR");
  }
  
  // Horizontal separator line in the middle
  display.drawFastHLine(0, 42, 128, WHITE);
  
  // === Air quality and system information ===
  
  // Air quality
  drawAirQualityIcon(5, 47);
  display.setTextSize(1);
  display.setCursor(22, 52);  
  display.printf("Air: %d", analogValue);
  
  // Vertical separator for bottom section
  display.drawFastVLine(72, 45, 19, WHITE);
  
  // Uptime - Bottom right corner
  display.setTextSize(1);
  display.setCursor(78, 47);
  display.println("Uptime:");
  display.setCursor(78, 56);
  unsigned long uptimeSeconds = millis() / 1000;
  unsigned long uptimeMinutes = uptimeSeconds / 60;
  unsigned long uptimeHours = uptimeMinutes / 60;
  
  if (uptimeHours > 0) {
    display.printf("%luh%lum", uptimeHours, uptimeMinutes % 60);
  } else if (uptimeMinutes > 0) {
    display.printf("%lum%lus", uptimeMinutes, uptimeSeconds % 60);
  } else {
    display.printf("%lus", uptimeSeconds);
  }
  
  display.display();
}
