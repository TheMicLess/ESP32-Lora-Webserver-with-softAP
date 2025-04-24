#include <SPI.h>
#include <LoRa.h>
#include <SPIFFS.h>
#include <WiFi.h>
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

String buffer; // Variable to store incoming file data
String filename ;

// WiFi SoftAP settings
const char *ssid = "ESP32-AP";
const char *password = "12345678";

SPIClass vspi(VSPI);  // VSPI instance for LoRa

void spiffs_setup() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully");
}

void wifi_setup(){
  WiFi.setSleep(false);
  IPAddress local_ip(210,100,2,1);
  IPAddress gateway(210,100,2,1);
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


void setup() {
  Serial.begin(115200);

  lora_setup();
  spiffs_setup();
  wifi_setup();
  
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

   
}


void loop() {
  server.handleClient();

  // Wait for user input from the Serial Monitor
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');  // Read the user's input command
    command.trim(); // Remove leading and trailing spaces

    if (command == "LIST") {
      // Command to request file list from ESP A
      Serial.println("Sending LIST command to ESP A...");
      sendLoRaCommand("LIST_FILES");
      waitForLoRaResponse("NULL");
    }
    else if (command.startsWith("GET_FILE ")) {
      // Command to request a specific file from ESP A
      String requestedFile = command.substring(9);  // Extract filename from command
      Serial.print("Requesting file: ");
      Serial.println(requestedFile);
      sendLoRaCommand("GET_FILE " + requestedFile);
      waitForLoRaResponse(requestedFile);
    }
    else {
      Serial.println("Invalid command! Please enter 'LIST' or 'FILE:filename'.");
    }
  }
}

// Function to handle serving the HTML page
void handleRoot() {
  server.send_P(200, "text/html", webpage);
}

void sendLoRaCommand(String command) {
  LoRa.beginPacket();
  LoRa.print(command);  // Send the command (LIST or FILE:filename)
  LoRa.endPacket();
}

void waitForLoRaResponse(String filename) {
  unsigned long startTime = millis();
  unsigned long timeout = 5000;  // Set a 5-second timeout for the response
  bool received = false;
  
  while (millis() - startTime < timeout || received) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String receivedData = "";
      while (LoRa.available()) {
        receivedData += (char)LoRa.read();
      }
      
      if (receivedData.startsWith("FILES:")) {
        String fileList = receivedData.substring(6);
        Serial.println("Received file list from ESP A:");
        Serial.println(fileList);
        received = true;
        break;  // Exit after receiving and displaying the file list
      }
      else if (receivedData.startsWith("DATA:")) {
        received = true;
        String fileData = receivedData.substring(5);
        buffer += fileData; // Accumulate file data
      }
      else if (receivedData.startsWith("DONE")){
        saveFileData(filename, buffer);
        buffer = ""; // Clear the buffer after saving
        break;
      }
    }
    delay(10);  // Brief delay to allow LoRa to handle incoming packets
  }

  if (!received) {
    Serial.println("No response received from ESP A within timeout.");
  }
}

// Function to save file data to the SPIFFS filesystem
void saveFileData(String filename, String data) {
  File file = SPIFFS.open("/" + filename, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  file.write((const uint8_t*)data.c_str(), data.length());
  Serial.print("File saved: ");
  Serial.println(filename);
  Serial.print("File Size: ");
  Serial.println(file.size());
  file.close();
}


// Function to list files in SPIFFS and send to the web server as JSON
void listFiles() {
  File root = SPIFFS.open("/");
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

// Function to get SPIFFS storage info and send to the web server
void sendStorageInfo() {
  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();

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
    uploadFile = SPIFFS.open(filename, FILE_WRITE);
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
      File checkFile = SPIFFS.open("/" + upload.filename, FILE_READ);
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
    File file = SPIFFS.open(filename, FILE_READ);
    
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
    if (SPIFFS.exists(filename)) {
      if (SPIFFS.remove(filename)) {
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