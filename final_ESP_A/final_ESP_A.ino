#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <SD.h>
#include <WebServer.h>

const char webpage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 File Access Point</title>
  <style>
    body { font-family: Arial, sans-serif; max-width: 600px; margin: auto; padding: 1em; }
    h2 { color: #333; }
    form, ul, p { margin: 20px 0; }
    input[type="submit"], button { cursor: pointer; padding: 8px 12px; margin-top: 5px; }
    button { background-color: #f44336; color: white; border: none; border-radius: 4px; }
    ul { list-style-type: none; padding: 0; }
    li { margin: 5px 0; }
    a { text-decoration: none; color: #1e90ff; }
    #storage-info { font-weight: bold; }
  </style>
</head>
<body>
  <h2>Upload File to ESP32</h2>
  <form method="POST" action="/upload" enctype="multipart/form-data">
    <input type="file" name="file" required>
    <input type="submit" value="Upload">
  </form>
  
  <h2>Files on ESP32 (click to download):</h2>
  <ul id="file-list"></ul>
  
  <h2>Storage Information:</h2>
  <p id="storage-info">Loading...</p>

  <script>
    // Fetch the file list from the server and display it
    async function fetchFileList() {
      try {
        const response = await fetch('/list');
        if (!response.ok) throw new Error('Failed to fetch file list');
        const files = await response.json();
        const fileListElement = document.getElementById('file-list');
        fileListElement.innerHTML = files.map(file => `
          <li>
            <a href="/download?filename=${file}" download>${file}</a> 
            <button onclick="deleteFile('${file}')">Delete</button>
          </li>
        `).join('');
      } catch (error) {
        alert(`Error loading file list: ${error.message}`);
      }
    }


    // Fetch storage information from the server
    async function fetchStorageInfo() {
      try {
        const response = await fetch('/storage');
        if (!response.ok) throw new Error('Failed to fetch storage info');
        const data = await response.text();
        document.getElementById('storage-info').textContent = data;
      } catch (error) {
        document.getElementById('storage-info').textContent = `Error: ${error.message}`;
      }
    }

    // Delete a file after user confirmation
    async function deleteFile(filename) {
      if (confirm(`Are you sure you want to delete ${filename}?`)) {
        try {
          const response = await fetch(`/delete?filename=${filename}`, { method: 'GET' });
          const result = await response.text();
          alert(result);
          fetchFileList(); // Refresh file list after deletion
        } catch (error) {
          alert(`Error deleting file: ${error.message}`);
        }
      }
    }

    // Initialize the page by fetching file list and storage info
    fetchFileList();
    fetchStorageInfo();
  </script>
</body>
</html>
)=====";


// Web server object on port 80
WebServer server(80);

// LoRa Pin Definitions (VSPI)
#define LORA_CS 5     // Chip Select for LoRa (VSPI)
#define LORA_RST 4    // Reset for LoRa
#define LORA_IRQ 2    // IRQ for LoRa
#define VSPI_SCK 18   // VSPI Clock
#define VSPI_MISO 19  // VSPI MISO
#define VSPI_MOSI 23  // VSPI MOSI

// SD Card Pin Definitions (HSPI)
#define SD_CS 15      // Chip Select for SD Card (HSPI)
#define HSPI_SCK 14   // HSPI Clock
#define HSPI_MISO 35  // HSPI MISO
#define HSPI_MOSI 13  // HSPI MOSI

// WiFi SoftAP settings
const char *ssid = "ESP32-server";
const char *password = "12345678";

SPIClass vspi(VSPI);  // VSPI instance for LoRa
SPIClass hspi(HSPI);  // HSPI instance for SD card

void wifi_setup(){
  IPAddress local_ip(200,100,1,1);
  IPAddress gateway(200,100,1,1);
  IPAddress subnet(255,255,255,0);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  
  Serial.print("Access Point IP Address: ");
  Serial.println(IP);
}

void lora_setup(){
  // Initialize VSPI for LoRa
  vspi.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, LORA_CS); // Initialize VSPI pins
  LoRa.setSPI(vspi); // Set the LoRa to use VSPI
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ); // CS, RST, IRQ

  if (!LoRa.begin(433E6)) {
    Serial.println("Starting LoRa failed!");
  } else {
    Serial.println("LoRa Initialized");
  }
}

void sd_setup(){
  hspi.begin(HSPI_SCK, HSPI_MISO, HSPI_MOSI, SD_CS); // Initialize HSPI pins

  if (!SD.begin(SD_CS, hspi,1000000)) {//4000000 is temp remove it later
    Serial.println("SD Card Mount Failed");
    return;
  } 
  
  uint64_t cardSize = SD.cardSize()/(1024*1024);
  Serial.print("SD card size: ");
  Serial.print(cardSize);
  Serial.println("MB");

  if(cardSize==0){
    Serial.println("Sd module initialization Failedor card size is 0.");
  }
}

void loraTask(void *pvParameters) {
  while (true) {
    if (LoRa.parsePacket()) {
      String command = LoRa.readString();
      Serial.print("Received command: ");
      Serial.println(command);
      handleLoRaCommand(command);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);  // Small delay to prevent task starvation
  }
} 

void setup() {
  Serial.begin(115200);
  //modules setups
  wifi_setup();
  sd_setup();
  lora_setup();


  // Initialize the web server routes
  server.on("/", HTTP_GET, handleRoot);           // Serve the web page
  server.on("/list", HTTP_GET, listFiles);        // Route to list files
  server.on("/upload", HTTP_POST, []() {          // Handle file upload
    server.send(200);
  }, handleFileUpload);
  server.on("/download", HTTP_GET, handleFileDownload);  // Handle file download
  server.on("/delete", HTTP_GET, handleFileDelete);      // Handle file delete
  server.on("/storage", HTTP_GET, sendStorageInfo);

  server.begin();
  Serial.println("HTTP server started");
  
  xTaskCreate(
    loraTask,           // Task function
    "LoRa Task",        // Name of the task
    4096,               // Stack size (in words, not bytes)
    NULL,               // Task input parameter
    2,                  // Priority of the task
    NULL                // Task handle (not needed)
  );

}

void loop() {
  server.handleClient();
}

// Function to handle serving the HTML page
void handleRoot() {
  server.send_P(200, "text/html", webpage);
}

// Handle incoming LoRa commands
void handleLoRaCommand(String command) {
  if (command == "LIST_FILES") {
    sendFileListViaLoRa();
  } 
  else if (command.startsWith("GET_FILE ")) {
    String filename = command.substring(9);
    sendFileViaLoRa(filename);
  }
}

// List files and send over LoRa
void sendFileListViaLoRa() {
  File root = SD.open("/");
  File file = root.openNextFile();
  String fileList = "";

  while (file) {
    fileList += String(file.name()) + ";";
    file = root.openNextFile();
  }

  if (fileList.length() > 0) {
    LoRa.beginPacket();
    LoRa.print("FILES:");
    LoRa.print(fileList);
    LoRa.endPacket();
    Serial.println("Sent file list over LoRa");
  } else {
    Serial.println("No files found on SD card");
  }
}

// Send file in chunks over LoRa
void sendFileViaLoRa(String filename) {
  File file = SD.open("/" + filename);
  if (!file) {
    Serial.println("File not found on SD card");
    return;
  }

  while (file.available()) {
    String chunk = "";
    for (int i = 0; i < 240 && file.available(); i++) {
      chunk += (char)file.read();
    }

    LoRa.beginPacket();
    LoRa.print("DATA:");
    LoRa.print(chunk);
    LoRa.endPacket();
    delay(100);
  }
  file.close();
  Serial.println("File sent over LoRa");
  LoRa.beginPacket();
  LoRa.print("DONE");
  LoRa.endPacket();
}

// Function to list files in SD and send to the web server as JSON
void listFiles() {
  File root = SD.open("/");
  File file = root.openNextFile();
  String fileList = "[";

  while (file) {
    fileList += "\"" + String(file.name()) + "\",";
    file = root.openNextFile();
  }

  if (fileList.length() > 1) {
    fileList = fileList.substring(0, fileList.length() - 1);  // Remove trailing comma
  }
  fileList += "]";
  
  server.send(200, "application/json", fileList);  // Send file list as JSON
}

// Function to get SD storage info and send to the web server
void sendStorageInfo() {
  size_t totalBytes = SD.totalBytes();
  size_t usedBytes = SD.usedBytes();

  String storageInfo = "Total: " + String(totalBytes / (1024.0*1024.0), 2) + " MB\n";
  storageInfo += "Used: " + String(usedBytes / (1024.0*1024.0), 2) + " MB\n";
  storageInfo += "Available: " + String((totalBytes - usedBytes) / (1024.0*1024.0), 2) + " MB\n";

  server.send(200, "text/plain", storageInfo);
}

// Helper function to get MIME type based on file extension
String getContentType(String filename) {
  if (filename.endsWith(".htm") || filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) return "image/jpeg";
  else if (filename.endsWith(".pdf")) return "application/pdf";
  else if (filename.endsWith(".doc") || filename.endsWith(".docx")) return "application/msword";
  else if (filename.endsWith(".json")) return "application/json";
  else if (filename.endsWith(".txt")) return "text/plain";
  else if (filename.endsWith(".xml")) return "text/xml";
  return "application/octet-stream";  // Default for other file types
}

// Handle file uploads with debugging (while upploading check if file is open in append mode for upload file)
void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  static File uploadFile;

  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;

    // Debug: Log the filename being uploaded
    Serial.print("File upload started: ");
    Serial.println(filename);

    // Open the file for writing
    uploadFile = SD.open(filename, FILE_WRITE);
    if (!uploadFile) {
      Serial.println("File open failed for writing.");
      server.send(500, "text/plain", "File upload failed");
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // Write the file as it is being uploaded in chunks
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);

      // Debug: Log the size of each chunk being written
      Serial.print("Writing chunk: ");
      Serial.println(upload.currentSize);
    } else {
      Serial.println("File handle invalid during write.");
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    // Close the file after the upload is complete
    if (uploadFile) {
      uploadFile.close();

      // Debug: Log the final file size
      Serial.print("Upload complete. Final file size: ");
      File checkFile = SD.open("/" + upload.filename, FILE_READ);
      Serial.println(checkFile.size());
      checkFile.close();
    }
    server.send(200, "text/plain", "File successfully uploaded");
  }
}

// Handle file download requests with additional debugging
void handleFileDownload() {
  if (server.hasArg("filename")) {
    String filename = "/" + server.arg("filename"); // Prefix with "/"

    // Debugging: Print the requested filename
    Serial.print("Requested file: ");
    Serial.println(filename);
    
    // Open the file for reading
    File file = SD.open(filename, FILE_READ);
    
    // Debugging: Check if the file opened correctly and print file size
    if (!file) {
      Serial.println("Failed to open file.");
      server.send(404, "text/plain", "File not found");
      return;
    } else {
      Serial.print("File opened successfully. Size: ");
      Serial.println(file.size());
    }

    if (file.size() == 0) {
      Serial.println("File is empty.");
      server.send(404, "text/plain", "File is empty");
      file.close();
      return;

    }

    // Get the content type based on file extension
    String contentType = getContentType(filename);

    // Set headers before streaming the file
    server.sendHeader("Content-Type", contentType);
    server.sendHeader("Content-Disposition", "attachment; filename=" + filename.substring(1)); // Remove leading "/"
    server.sendHeader("Content-Length", String(file.size()));
    
    // Stream the file to the client
    server.streamFile(file, contentType);
    file.close();  // Close the file after streaming
  } else {
    // Send a bad request response if no 'filename' parameter is provided
    Serial.println("No 'filename' parameter provided.");
    server.send(400, "text/plain", "Bad request, 'filename' parameter missing");
  }
}

// Handle file deletion requests
void handleFileDelete() {
  if (server.hasArg("filename")) {
    String filename = "/" + server.arg("filename"); // Prefix with "/"
    if (SD.exists(filename)) {
      if (SD.remove(filename)) {
        server.send(200, "text/plain", "File deleted successfully");
      } else {
        server.send(500, "text/plain", "File deletion failed");
      }
    } else {
      server.send(404, "text/plain", "File not found");
    }
  } else {
    server.send(400, "text/plain", "Bad request, 'filename' parameter missing");
  }
}