/*
   Copyright (c) 2015, Majenko Technologies
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification,
   are permitted provided that the following conditions are met:

 * * Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

 * * Redistributions in binary form must reproduce the above copyright notice, this
     list of conditions and the following disclaimer in the documentation and/or
     other materials provided with the distribution.

 * * Neither the name of Majenko Technologies nor the names of its
     contributors may be used to endorse or promote products derived from
     this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
   ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
   ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <StreamString.h>
#include <SoftwareSerial.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <LittleFS.h>

// EEPROM addresses for SSID, password, IP, subnet mask, and gateway
#define SSID_ADDR 0
#define PASS_ADDR 32
#define IP_ADDR 64
#define SUBNET_ADDR 96
#define GATEWAY_ADDR 128

// Fallback SSID and password
String uniqueDeviceName = "LabPowerSupply_" + String(ESP.getChipId());
const char* fallbackSSID = uniqueDeviceName.c_str();
const char* fallbackPassword = "12345678"; //Change this default password if you want to have different password for the AP

const int RXPin = D6; // Wemos D1 Mini RX pin
const int TXPin = D5; // Wemos D1 Mini TX pin
const int virtualSerialTimeoutVal = 2000;
SoftwareSerial virtualSerial(RXPin, TXPin);

ESP8266WebServer server(80);

String voltageCommand = "<12000000000>"; // Read voltage command
String currentCommand = "<14000000000>"; // Read current command

/*-------------- GLOBAL VARIABLES --------------*/
float voltageValue = 0.0;
float currentValue = 0.0;
float powerValue = 0.0;
float tSafevoltageValue = 0.0;
float tSafecurrentValue = 0.0;
float tSafepowerValue = 0.0;
bool pcIsMaster = false; // Flag to check if PC is master

void connectWiFi() {
  Serial.println("Connecting to default WiFi...");
  WiFi.hostname(uniqueDeviceName.c_str()); // Set a default hostname
  WiFi.begin("SET_PubWiFi", "Set2011a");

  // Wait for connection to be established
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to default WiFi!");
    return; // Exit if connected successfully
  }

  String ssid = readStringFromEEPROM(SSID_ADDR);
  String password = readStringFromEEPROM(PASS_ADDR);
  // Attempt to connect to stored WiFi network
  if (ssid.length() > 0 && password.length() > 0) {
    Serial.println("Connecting to stored WiFi...");
    WiFi.hostname(uniqueDeviceName.c_str());
    WiFi.begin(ssid.c_str(), password.c_str());
  } else {
    Serial.println("Connecting to fallback WiFi...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(fallbackSSID, fallbackPassword);
  }

  // Wait for connection to be established
  attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  // If connection failed, start the access point
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nConnection failed. Starting AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(fallbackSSID, fallbackPassword);
  } else {
    Serial.println("\nConnected to WiFi!");
  }
}

bool connectPcAsMaster() {
  if(pcIsMaster){
    return true;
  }
  // List of commands to send
  String commands[] = {
    "<09100000000>"
  };

  for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
    String response = sendCommandAndWait(commands[i], 2000);
    if (response.isEmpty()) {
      Serial.println("No ACK/response for command: " + commands[i]);
      pcIsMaster = false; // Reset flag if any command fails
      return false;
    }
    // Optionally, check for specific ACK content here
  }
  pcIsMaster = true; // Set flag indicating PC is master
  return true;
}

bool disconnectPcAsMaster() {
  if(!pcIsMaster){
    return true;
  }
  // List of commands to send for disconnection
  String commands[] = {
    "<09200000000>"
  };

  for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
    String response = sendCommandAndWait(commands[i], 2000);
    if (response.isEmpty()) {
      Serial.println("No ACK/response for command: " + commands[i]);
      pcIsMaster = false; // Reset flag if command fails
      return false;
    }
    // Optionally, check for specific ACK content here
  }
  pcIsMaster = false; // Reset flag indicating PC is no longer master
  return true;
}

void setOutputEnable(){
  controlSupplyOutput(true);
}

void setOutputDisable(){
  controlSupplyOutput(false);
}

bool controlSupplyOutput(bool enable) {
  // <07000000000> to enable output, <08000000000> to disable output
  String command = enable ? "<07000000000>" : "<08000000000>";
  String response = sendCommandAndWait(command, 2000);
  if (response.isEmpty()) {
    Serial.println("No ACK/response for command: " + command);
    return false; // Command failed
  }
  return true; // Command succeeded
}

void sendNewOutputVoltage(String newOutVoltage) {
  if(!pcIsMaster){
    if(!connectPcAsMaster()){
      Serial.println("Failed to connect PC as master. Cannot send new output voltage.");
      return;
    }
  }
  
  // <11xxxxxx000> where xxx,xxx is the voltage value
  // Split the input string by the decimal point
  int dotIndex = newOutVoltage.indexOf('.');
  String intPart = dotIndex != -1 ? newOutVoltage.substring(0, dotIndex) : newOutVoltage;
  String decPart = dotIndex != -1 ? newOutVoltage.substring(dotIndex + 1) : "0";

  // Pad integer and decimal parts to 3 digits
  while (intPart.length() < 3) intPart = "0" + intPart;
  while (decPart.length() < 3) decPart += "0";
  decPart = decPart.substring(0, 3); // Ensure only 3 digits

  // Format: <11VVVvvv000>
  String formattedVoltage = "<11" + intPart + decPart + "000>";
  String response = sendCommandAndWait(formattedVoltage, 2000);
  if (response.isEmpty()) {
    Serial.println("No ACK/response for voltage command: " + formattedVoltage);
  }
}

void sendNewOutputCurrent(String newOutCurrent){
  if(!pcIsMaster){
    if(!connectPcAsMaster()){
      Serial.println("Failed to connect PC as master. Cannot send new output current.");
      return;
    }
  }

  // <13xxxxxx000> where xxx,xxx is the current value
  int dotIndex = newOutCurrent.indexOf('.');
  String intPart = dotIndex != -1 ? newOutCurrent.substring(0, dotIndex) : newOutCurrent;
  String decPart = dotIndex != -1 ? newOutCurrent.substring(dotIndex + 1) : "0";

  // Pad integer and decimal parts to 3 digits
  while (intPart.length() < 3) intPart = "0" + intPart;
  while (decPart.length() < 3) decPart += "0";
  decPart = decPart.substring(0, 3); // Ensure only 3 digits

  // Format: <13CCCccc000>
  String formattedCurrent = "<13" + intPart + decPart + "000>";
  String response = sendCommandAndWait(formattedCurrent, 2000);
  if (response.isEmpty()) {
    Serial.println("No ACK/response for current command: " + formattedCurrent);
  }
}

void handleVoltageCommand(String response) {
  // Extract data bits from the response
  int voltage = response.substring(3, 9).toInt();   //Correction from (4,9) to (3,9) as some power supply can get up to 800Vdc!
  // Calculate voltage value (assuming data bits are in the format provided)
  voltageValue = voltage / 1000.0;
  // Calculate power
  powerValue = voltageValue * currentValue;
}

void handleCurrentCommand(String response) {
  // Extract data bits from the response
  int current = response.substring(4, 9).toInt();
  // Calculate current value (assuming data bits are in the format provided)
  currentValue = current / 1000.0;
  // Calculate power
  powerValue = voltageValue * currentValue;
}

void sendVoltageCommand() {
  String response = sendCommandAndWait(voltageCommand, -1);
  if (!response.isEmpty()) {
    handleVoltageCommand(response);
  }
}

void sendCurrentCommand() {
  String response = sendCommandAndWait(currentCommand, -1);
  if (!response.isEmpty()) {
    handleCurrentCommand(response);
  }
}

String sendCommandAndWait(String command, int timeout) {
  int timeoutVal;
  if(timeout == -1){
    timeoutVal = virtualSerialTimeoutVal;
  }
  else{
    timeoutVal = timeout;
  }
  
  static volatile bool inUse = false;
  while (__atomic_test_and_set(&inUse, __ATOMIC_ACQUIRE)) {
    delay(1); // Wait until the function is free
  }

  Serial.println("Flag taken!");

  // Clear any incoming data from the serial buffer
  while (virtualSerial.available()) {
      virtualSerial.read(); // Read and discard incoming data
  }

  virtualSerial.print(command);
  Serial.println("Commande envoyée: " + command);
  // Wait for response
  unsigned long startTime = millis();
  while (!virtualSerial.available()) {
    if (millis() - startTime > timeoutVal) { // Timeout after 2 seconds
      Serial.println("Timeout occurred while waiting for response from Power supply.");
      __atomic_clear(&inUse, __ATOMIC_RELEASE);
      Serial.println("Flag released on error!");
      return "";
    }
  }
  // Ignore characters before the first '<'
  char ch;
  while (virtualSerial.available()) {
    ch = virtualSerial.read();
    if (ch == '<') {
      break;
    }
  }
  // Now read the rest of the response including '<'
  String response = "<" + virtualSerial.readStringUntil('>');
  response += '>'; // Add the closing '>' since readStringUntil does not include it
  Serial.println("Power supply response read!");
  __atomic_clear(&inUse, __ATOMIC_RELEASE);
  Serial.println("Flag released on success!");
  return response;
}

void uriSetup(){
  server.on("/", []() {
    File file = LittleFS.open("/index.html", "r");
    if (!file) {
      server.send(404, "text/plain", "File not found");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });
  server.on("/data", HTTP_GET, []() {
    String data = "{\"voltage\": " + String(tSafevoltageValue, 3) + ", \"current\": " + String(tSafecurrentValue, 3) + ", \"power\": " + String(tSafepowerValue, 3) + "}";
    server.send(200, "application/json", data);
  });
  server.on("/config", HTTP_GET, []() {
    String connectedSSID = "";
    String connectedPass = "";
    String ipAddress = "";

    // Check if device is in STA mode and connected to WiFi
    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
      connectedSSID = WiFi.SSID();
      connectedPass = WiFi.psk();
      ipAddress = WiFi.localIP().toString();
    } else {
      ipAddress = WiFi.softAPIP().toString();
    }
    // Load HTML file from LittleFS
    File htmlFile = LittleFS.open("/config_page.html", "r");
    if (htmlFile) {
      String page = htmlFile.readString();
      htmlFile.close();

      // Replace placeholders in HTML template with actual values
      page.replace("$CONNECTED_SSID$", connectedSSID);
      page.replace("$CONNECTED_PASS$", connectedPass);
      page.replace("$IP_ADDRESS$", ipAddress);

      server.send(200, "text/html", page);
    } else {
      server.send(500, "text/plain", "Failed to load configuration page.");
    }
  });
  server.on("/setconfig", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String staticIP = server.arg("static_ip");
    String subnetMask = server.arg("subnet_mask");
    String gateway = server.arg("gateway");
    // Store SSID and password in EEPROM
    writeStringToEEPROM(SSID_ADDR, ssid);
    writeStringToEEPROM(PASS_ADDR, password);
    EEPROM.commit();

    // Check if static IP settings are provided
    if (!staticIP.isEmpty() && !subnetMask.isEmpty() && !gateway.isEmpty()) {
      // Store static IP configuration in EEPROM
      writeStringToEEPROM(IP_ADDR, staticIP);
      writeStringToEEPROM(SUBNET_ADDR, subnetMask);
      writeStringToEEPROM(GATEWAY_ADDR, gateway);
      EEPROM.commit();
      ESP.reset();
    } else {
      // Erase static IP configuration from EEPROM
      for (int i = IP_ADDR; i < GATEWAY_ADDR + 32; ++i) {
        EEPROM.write(i, 0);
      }
      EEPROM.commit();

      // Clear existing static IP configuration
      WiFi.config(0U, 0U, 0U);
      ESP.reset();
    }

    // Attempt to connect to the new WiFi network
    connectWiFi();

    // Send response to client
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to WiFi!");
      server.send(200, "text/plain", "Configuration saved and connected to WiFi.");
    } else {
      Serial.println("\nConnection to new WiFi failed!");
      server.send(200, "text/plain", "Configuration failed. Unable to connect to WiFi.");
    }
  });
  server.on("/setoutput", HTTP_POST, []() {
    if (server.hasArg("plain") == false) {
      server.send(400, "text/plain", "Body not received");
      return;
    }
    String body = server.arg("plain");
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }

    String newVoltage = doc["voltage"] | "";
    String newCurrent = doc["current"] | "";

    if (newVoltage.length() == 0 || newCurrent.length() == 0) {
      Serial.println("Missing newVoltage or newCurrent in JSON");
      server.send(400, "text/plain", "Missing voltage or current");
      return;
    }

    sendNewOutputVoltage(newVoltage);
    sendNewOutputCurrent(newCurrent);

    server.send(200, "text/plain", "OK");
  });
  server.on("/setmaster", HTTP_POST, []() {
    if (server.hasArg("plain") == false) {
      server.send(400, "text/plain", "Body not received");
      return;
    }
    String body = server.arg("plain");
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }

    bool remoteIsMaster = doc["remoteIsMaster"];
    if(remoteIsMaster) {
      connectPcAsMaster();
    } else {
      disconnectPcAsMaster();
    }
    server.send(200, "text/plain", "OK");
  });
  server.on("/setoutputon", HTTP_POST, []() {
    if (server.hasArg("plain") == false) {
      server.send(400, "text/plain", "Body not received");
      return;
    }
    String body = server.arg("plain");
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }

    bool outputIsEnabled = doc["outputIsEnabled"];
    if(outputIsEnabled) {
      setOutputEnable();
    } else {
      setOutputDisable();
    }
    server.send(200, "text/plain", "OK");
  });
  server.on("/chart.js", HTTP_GET, []() {
    if (LittleFS.exists("/chart.js")) {
      File file = LittleFS.open("/chart.js", "r");
      server.streamFile(file, "application/javascript");
      file.close();
    } else {
      server.send(404, "text/plain", "File Not Found");
    }
  });
}

void setup(void) {
  // Initialize Serial Monitor
  Serial.begin(115200);
  // Initialize SoftwareSerial
  virtualSerial.begin(9600);
  virtualSerial.setTimeout(virtualSerialTimeoutVal);
  // Initialize EEPROM
  EEPROM.begin(512);

  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }

  // Connect to WiFi using stored credentials or fallback
  connectWiFi();

  // Check if stored IP, subnet mask, or gateway in EEPROM are missing
  String storedIP = readStringFromEEPROM(IP_ADDR);
  String storedSubnetMask = readStringFromEEPROM(SUBNET_ADDR);
  String storedGateway = readStringFromEEPROM(GATEWAY_ADDR);

  if (storedIP.isEmpty() || storedSubnetMask.isEmpty() || storedGateway.isEmpty()) {
    Serial.println("Stored network configuration missing or incomplete. Using dynamic IP.");
    WiFi.config(0U, 0U, 0U); // Clear existing static IP configuration
  } else {
    // Configure static IP from EEPROM
    IPAddress ip, mask, gw;
    ip.fromString(storedIP);
    mask.fromString(storedSubnetMask);
    gw.fromString(storedGateway);
    WiFi.config(ip, gw, mask);
  }

  uriSetup();

  if (MDNS.begin("esp8266")) { Serial.println("MDNS responder started"); }
  
  server.begin();
  Serial.println("HTTP server started");

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");
  
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
    // Print STA IP address
    Serial.println("IP Address: " + WiFi.localIP().toString());
  } else {
    // Print AP IP address
    Serial.println("IP Address: " + WiFi.softAPIP().toString());
  }
}

void loop(void) {
  server.handleClient();
  MDNS.update();
}

String readStringFromEEPROM(int addr) {
  String value = "";
  char c;
  for (int i = addr; i < addr + 32; ++i) {
    c = EEPROM.read(i);
    if (c == 0) break;
    value += c;
  }
  Serial.println("EEPROM Read String: " + value); // Print the read value
  return value;
}

void writeStringToEEPROM(int addr, String value) {
  int len = value.length();
  for (int i = 0; i < len; ++i) {
    EEPROM.write(addr + i, value[i]);
  }
  EEPROM.write(addr + len, 0); // Null-terminate the string
}
