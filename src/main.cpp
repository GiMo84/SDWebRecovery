/*
  SDWebRecovery - Web-based SD Card recovery tool for ESP32

  Copyright (c) 2024 Giulio Molinari. All rights reserved.
  Copyright (c) 2015 Hristo Gochkov. All rights reserved.
  
  This file originated from the WebServer library for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  
  Connect the SD Card to the SPI port of the ESP32.
  Use a resistor towards 3V3 or a pin configured as pull-up to pull the MISO line up.
  
  To retrieve the binary image of the SD Card, visit http://esp32sd.local/raw
  
  If the SD Card is formatted in FAT/FAT32, the files contained on it can be directly accessed.
  
  To retrieve the contents of SDcard, visit http://esp32sd.local/list?dir=/
      dir is the argument that needs to be passed to the function PrintDirectory via HTTP Get request.
  
  File extensions with more than 3 charecters are not supported by the SD Library
  File Names longer than 8 charecters will be truncated by the SD library, so keep filenames shorter
  index.htm is the default index (works on subfolders as well)


*/
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <SD.h>

#define DBG_OUTPUT_PORT Serial

#define DAMAGEDSECTOR 0xE5

#define SD_MISO_PULLUP_PIN 21

const char* ssid = "wifi-SSID";
const char* password = "wifi-password";
const char* host = "esp32sd";

WebServer server(80);

static bool hasSD = false;


void returnOK() {
  server.send(200, "text/plain", "");
}

void returnFail(String msg) {
  server.send(500, "text/plain", msg + "\r\n");
}

bool loadFromSdCard(String path) {
  String dataType = "text/plain";
  if (path.endsWith("/")) {
    path += "index.htm";
  }

  if (path.endsWith(".src")) {
    path = path.substring(0, path.lastIndexOf("."));
  } else if (path.endsWith(".htm")) {
    dataType = "text/html";
  } else if (path.endsWith(".css")) {
    dataType = "text/css";
  } else if (path.endsWith(".js")) {
    dataType = "application/javascript";
  } else if (path.endsWith(".png")) {
    dataType = "image/png";
  } else if (path.endsWith(".gif")) {
    dataType = "image/gif";
  } else if (path.endsWith(".jpg")) {
    dataType = "image/jpeg";
  } else if (path.endsWith(".ico")) {
    dataType = "image/x-icon";
  } else if (path.endsWith(".xml")) {
    dataType = "text/xml";
  } else if (path.endsWith(".pdf")) {
    dataType = "application/pdf";
  } else if (path.endsWith(".zip")) {
    dataType = "application/zip";
  }

  File dataFile = SD.open(path.c_str());
  if (dataFile.isDirectory()) {
    path += "/index.htm";
    dataType = "text/html";
    dataFile = SD.open(path.c_str());
  }

  if (!dataFile) {
    return false;
  }

  if (server.hasArg("download")) {
    dataType = "application/octet-stream";
  }

  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
    DBG_OUTPUT_PORT.println("Sent less data than expected!");
  }

  dataFile.close();
  return true;
}

void streamRaw() {
  uint64_t sectors = SD.numSectors();
  //uint64_t sectors = 10240; // for test purposes

  size_t sectorSize = SD.sectorSize();
  size_t sdSize = sectors*sectorSize;
  
  DBG_OUTPUT_PORT.print(sectors);
  DBG_OUTPUT_PORT.print(" sectors, ");
  DBG_OUTPUT_PORT.print(sectorSize);
  DBG_OUTPUT_PORT.print(" bytes per sector, ");
  DBG_OUTPUT_PORT.print(sdSize);
  DBG_OUTPUT_PORT.println(" bytes.");

  uint8_t data[sectorSize];
  uint64_t sector;
  memset(data, 0, sectorSize);

  server.setContentLength(sdSize);
  server.send(200, "application/octet-stream", "");
  
  for (sector = 0; sector < sectors; sector ++) {
    if (!SD.readRAW(data, sector)) {
      memset(data, DAMAGEDSECTOR, sectorSize);
      DBG_OUTPUT_PORT.print("Error reading sector ");
      DBG_OUTPUT_PORT.println(sector);
    }
    server.sendContent_P((char*)data, sectorSize);
    //delay(2);//allow the cpu to switch to other tasks
  }
}

void printDirectory() {
  if (!server.hasArg("dir")) {
    return returnFail("BAD ARGS");
  }
  String path = server.arg("dir");
  if (path != "/" && !SD.exists((char *)path.c_str())) {
    return returnFail("BAD PATH");
  }
  File dir = SD.open((char *)path.c_str());
  path = String();
  if (!dir.isDirectory()) {
    dir.close();
    return returnFail("NOT DIR");
  }
  dir.rewindDirectory();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/json", "");

  server.sendContent("[");
  for (int cnt = 0; true; ++cnt) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    String output;
    if (cnt > 0) {
      output = ',';
    }

    output += "{\"type\":\"";
    output += (entry.isDirectory()) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += entry.path();
    output += "\"";
    output += "}";
    server.sendContent(output);
    entry.close();
  }
  server.sendContent("]");
  dir.close();
}

void handleNotFound() {
  if (hasSD && loadFromSdCard(server.uri())) {
    return;
  }
  String message;
  if (!hasSD) {
    message = "SDCARD Not Detected\n\n";
  }
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " NAME:" + server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  DBG_OUTPUT_PORT.print(message);
}

void setup(void) {
  // Pull-up for SD MISO line
  pinMode(SD_MISO_PULLUP_PIN, INPUT_PULLUP);

  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);
  DBG_OUTPUT_PORT.print("\n");
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(host); //define hostname
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  DBG_OUTPUT_PORT.print("Connecting to ");
  DBG_OUTPUT_PORT.println(ssid);

  // Wait for connection
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {//wait 10 seconds
    delay(500);
  }
  if (i == 21) {
    DBG_OUTPUT_PORT.print("Could not connect to");
    DBG_OUTPUT_PORT.println(ssid);
    while (1) {
      delay(500);
    }
  }
  DBG_OUTPUT_PORT.print("Connected! IP address: ");
  DBG_OUTPUT_PORT.println(WiFi.localIP());

  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    DBG_OUTPUT_PORT.println("MDNS responder started");
    DBG_OUTPUT_PORT.print("You can now connect to http://");
    DBG_OUTPUT_PORT.print(host);
    DBG_OUTPUT_PORT.println(".local");
  }


  server.on("/list", HTTP_GET, printDirectory);
  server.on("/raw", HTTP_GET, streamRaw);
  server.onNotFound(handleNotFound);

  server.begin();
  DBG_OUTPUT_PORT.println("HTTP server started");

  if (SD.begin(SS, SPI, 40000000U)) {
    DBG_OUTPUT_PORT.println("SD Card initialized.");
    hasSD = true;
  }
}

void loop(void) {
  server.handleClient();
  delay(2);//allow the cpu to switch to other tasks
}
