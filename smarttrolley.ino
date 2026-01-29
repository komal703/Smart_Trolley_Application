/*
 * Smart Trolley and Billing System
 * Arduino Code for NodeMCU ESP8266 with RFID RC522
 * 
 * Hardware Connections:
 * RFID RC522 -> NodeMCU
 * SDA/SS -> D8 (GPIO15)
 * SCK -> D5 (GPIO14)
 * MOSI -> D7 (GPIO13)
 * MISO -> D6 (GPIO12)
 * RST -> D3 (GPIO0)
 * GND -> GND
 * 3.3V -> 3.3V
 * 
 * LCD I2C -> NodeMCU
 * SDA -> D2 (GPIO4)
 * SCL -> D1 (GPIO5)
 * VCC -> 5V
 * GND -> GND
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// WiFi Configuration
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Server Configuration
const char* serverUrl = "http://192.168.1.100:3000/api";

// RFID Configuration
#define RST_PIN 0  // D3
#define SS_PIN 15  // D8
MFRC522 mfrc522(SS_PIN, RST_PIN);

// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// IR Sensor Pin
#define IR_SENSOR D0

// Variables
String currentUserID = "";
float totalAmount = 0.0;
int itemCount = 0;
bool cartActive = false;
unsigned long lastScanTime = 0;
const unsigned long scanDelay = 2000; // 2 seconds between scans

// Web Server
ESP8266WebServer server(80);

void setup() {
  Serial.begin(115200);
  
  // Initialize SPI bus
  SPI.begin();
  
  // Initialize RFID reader
  mfrc522.PCD_Init();
  delay(100);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Trolley");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  // Initialize IR Sensor
  pinMode(IR_SENSOR, INPUT);
  
  // Connect to WiFi
  connectToWiFi();
  
  // Setup web server routes
  setupWebServer();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready to Scan");
  lcd.setCursor(0, 1);
  lcd.print("Tap Your Card");
  
  Serial.println("System Ready!");
}

void loop() {
  server.handleClient();
  
  // Check for RFID card
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    
    // Prevent rapid re-scanning
    if (millis() - lastScanTime > scanDelay) {
      String rfidTag = getRFIDTag();
      Serial.println("RFID Detected: " + rfidTag);
      
      if (!cartActive) {
        // First scan - User authentication
        authenticateUser(rfidTag);
      } else {
        // Product scan
        scanProduct(rfidTag);
      }
      
      lastScanTime = millis();
    }
    
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }
  
  // Check IR sensor for product removal
  if (cartActive && digitalRead(IR_SENSOR) == HIGH) {
    handleProductRemoval();
  }
  
  delay(100);
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    Serial.println("\nWiFi Connection Failed!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed!");
    lcd.setCursor(0, 1);
    lcd.print("Check Settings");
  }
}

String getRFIDTag() {
  String content = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    content += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    content += String(mfrc522.uid.uidByte[i], HEX);
  }
  content.toUpperCase();
  return content;
}

void authenticateUser(String rfidTag) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Authenticating..");
  
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClient client;
    
    String url = String(serverUrl) + "/users/authenticate";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    // Create JSON payload
    StaticJsonDocument<200> doc;
    doc["rfid"] = rfidTag;
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    
    int httpCode = http.POST(jsonPayload);
    
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println("Response: " + payload);
      
      StaticJsonDocument<500> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, payload);
      
      if (!error && responseDoc["success"] == true) {
        currentUserID = rfidTag;
        cartActive = true;
        String userName = responseDoc["user"]["name"].as<String>();
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Welcome!");
        lcd.setCursor(0, 1);
        lcd.print(userName.substring(0, 16));
        delay(2000);
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Items: 0");
        lcd.setCursor(0, 1);
        lcd.print("Total: Rs.0.00");
        
        Serial.println("User authenticated: " + userName);
      } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Auth Failed!");
        lcd.setCursor(0, 1);
        lcd.print("Invalid Card");
        delay(2000);
        resetDisplay();
      }
    } else {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Server Error!");
      delay(2000);
      resetDisplay();
    }
    
    http.end();
  }
}

void scanProduct(String rfidTag) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scanning...");
  
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClient client;
    
    String url = String(serverUrl) + "/products/scan";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<300> doc;
    doc["rfid"] = rfidTag;
    doc["userId"] = currentUserID;
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    
    int httpCode = http.POST(jsonPayload);
    
    if (httpCode > 0) {
      String payload = http.getString();
      
      StaticJsonDocument<500> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, payload);
      
      if (!error && responseDoc["success"] == true) {
        String productName = responseDoc["product"]["name"].as<String>();
        float price = responseDoc["product"]["price"];
        totalAmount = responseDoc["totalAmount"];
        itemCount = responseDoc["itemCount"];
        
        // Display product info
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(productName.substring(0, 16));
        lcd.setCursor(0, 1);
        lcd.print("Rs.");
        lcd.print(price, 2);
        delay(2000);
        
        // Display cart summary
        updateCartDisplay();
        
        Serial.println("Product added: " + productName);
      } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Product Not");
        lcd.setCursor(0, 1);
        lcd.print("Found!");
        delay(2000);
        updateCartDisplay();
      }
    }
    
    http.end();
  }
}

void handleProductRemoval() {
  // This function would be called when IR sensor detects product removal
  // Implementation depends on your specific requirements
  Serial.println("Product removal detected");
}

void updateCartDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Items: ");
  lcd.print(itemCount);
  lcd.setCursor(0, 1);
  lcd.print("Total: Rs.");
  lcd.print(totalAmount, 2);
}

void resetDisplay() {
  cartActive = false;
  currentUserID = "";
  totalAmount = 0.0;
  itemCount = 0;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready to Scan");
  lcd.setCursor(0, 1);
  lcd.print("Tap Your Card");
}

void setupWebServer() {
  // Handle root request
  server.on("/", HTTP_GET, []() {
    String html = "<html><body>";
    html += "<h1>Smart Trolley System</h1>";
    html += "<p>Cart Active: " + String(cartActive ? "Yes" : "No") + "</p>";
    html += "<p>Items: " + String(itemCount) + "</p>";
    html += "<p>Total: Rs." + String(totalAmount) + "</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });
  
  // Handle status request
  server.on("/status", HTTP_GET, []() {
    StaticJsonDocument<300> doc;
    doc["cartActive"] = cartActive;
    doc["userId"] = currentUserID;
    doc["itemCount"] = itemCount;
    doc["totalAmount"] = totalAmount;
    
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });
  
  // Handle checkout request
  server.on("/checkout", HTTP_POST, []() {
    if (cartActive) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Thank You!");
      lcd.setCursor(0, 1);
      lcd.print("Visit Again");
      delay(3000);
      
      resetDisplay();
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"No active cart\"}");
    }
  });
  
  server.begin();
  Serial.println("Web server started");
}