#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <espnow.h>
#include <Hash.h>

// Access Point configuration
const char* ssid = "ESP8266-Chat";
const char* password = "12345678"; // Set to "" for open network

// Web server and WebSocket
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Maximum chunk size for ESP-NOW messages
#define CHUNK_SIZE 240  // 240 bytes for payload (max 250 bytes for ESP-NOW)

// Message structure for ESP-NOW
typedef struct struct_message {
  uint8_t messageType;      // 0 for text, 1 for image
  uint8_t chunkID;          // Chunk ID to track the order of chunks
  uint8_t totalChunks;      // Total number of chunks
  char text[CHUNK_SIZE];    // Text data or chunk of binary/base64 image data
} struct_message;

struct_message outgoingMessage;
struct_message incomingMessage;

// Variables for reassembling text and image
String reassembledText = "";
String reassembledImageBase64 = "";

// MAC addresses of peer devices
uint8_t peerDevice[] = {0xFC, 0xF5, 0xC4, 0xA6, 0x40, 0x80}; // Change to your peer device MAC

// Setup Base64 for image encoding/decoding
const char base64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Custom Base64 Encoding Function
String base64_encode_custom(uint8_t* data, size_t length) {
  String encoded = "";
  int i = 0;
  int j = 0;
  uint8_t char_array_3[3];
  uint8_t char_array_4[4];

  while (length--) {
    char_array_3[i++] = *(data++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for (i = 0; i < 4; i++)
        encoded += base64_alphabet[char_array_4[i]];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

    for (j = 0; j < i + 1; j++)
      encoded += base64_alphabet[char_array_4[j]];

    while (i++ < 3)
      encoded += '=';
  }

  return encoded;
}

// WebSocket event handler
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      }
      break;
    
    case WStype_TEXT:
      {
        // Convert payload to String
        String text = String((char *)payload);
        Serial.printf("[%u] Received Text: %s\n", num, text.c_str());
        
        // Check if this is a text message to be sent via ESP-NOW
        if (text.startsWith("text:")) {
          String message = text.substring(5); // Remove "text:" prefix
          sendLongMessage(message);
          
          // Echo message back to confirm receipt
          String response = "{\"type\":\"sent\",\"content\":\"" + message + "\"}";
          webSocket.sendTXT(num, response);
        }
        // Check if this is an image upload request
        else if (text.startsWith("image-start:")) {
          // Image upload will follow in binary frames
          String response = "{\"type\":\"image-ready\"}";
          webSocket.sendTXT(num, response);
        }
      }
      break;
    
    case WStype_BIN:
      {
        Serial.printf("[%u] Received Binary: %u bytes\n", num, length);
        
        // Process binary data as image
        if (length > 0) {
          // Send the binary data via ESP-NOW
          sendImageBase64(payload, length);
          
          // Confirm to the client
          String response = "{\"type\":\"image-sent\",\"size\":" + String(length) + "}";
          webSocket.sendTXT(num, response);
        }
      }
      break;
  }
}

// Callback when data is sent via ESP-NOW
void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Last Packet Send Status: ");
  Serial.println(sendStatus == 0 ? "Delivery Success" : "Delivery Fail");
}

// Callback when data is received via ESP-NOW
void onDataReceive(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));

  if (incomingMessage.messageType == 0) {
    // Handle text message
    reassembledText += incomingMessage.text;

    // If all chunks are received, process the full message
    if (incomingMessage.chunkID == incomingMessage.totalChunks - 1) {
      Serial.println("Received text: " + reassembledText);
      
      // Forward the message to all connected WebSocket clients
      String jsonResponse = "{\"type\":\"received\",\"content\":\"" + reassembledText + "\"}";
      webSocket.broadcastTXT(jsonResponse);
      
      reassembledText = ""; // Clear for next message
    }
  } else if (incomingMessage.messageType == 1) {
    // Handle image chunk
    Serial.print("Received Image Chunk ");
    Serial.print(incomingMessage.chunkID);
    Serial.print(" of ");
    Serial.println(incomingMessage.totalChunks);

    // Append base64 chunk to reassembledImageBase64
    reassembledImageBase64 += incomingMessage.text;

    // If all chunks are received, process the complete image
    if (incomingMessage.chunkID == incomingMessage.totalChunks - 1) {
      Serial.println("Complete Base64 Image Received");
      
      // Forward the complete image to all WebSocket clients
      String jsonResponse = "{\"type\":\"image-received\",\"data\":\"" + reassembledImageBase64 + "\"}";
      webSocket.broadcastTXT(jsonResponse);
      
      reassembledImageBase64 = ""; // Reset for next image
    }
  }
}

void sendLongMessage(String message) {
  int totalChunks = ceil((float)message.length() / CHUNK_SIZE);

  for (uint8_t i = 0; i < totalChunks; i++) {
    // Extract a chunk of the message
    String chunk = message.substring(i * CHUNK_SIZE, (i + 1) * CHUNK_SIZE);

    // Prepare outgoing message
    outgoingMessage.messageType = 0; // Text
    outgoingMessage.chunkID = i;
    outgoingMessage.totalChunks = totalChunks;
    chunk.toCharArray(outgoingMessage.text, CHUNK_SIZE);

    // Send the chunk
    esp_now_send(peerDevice, (uint8_t *)&outgoingMessage, sizeof(outgoingMessage));

    delay(50);  // Small delay to avoid overwhelming ESP-NOW
  }
}

void sendImageBase64(const uint8_t* rawData, size_t dataSize) {
  // Encode to base64
  String base64String = base64_encode_custom((uint8_t*)rawData, dataSize);
  
  // Now send the base64 string in chunks
  int totalChunks = ceil((float)base64String.length() / CHUNK_SIZE);
  
  Serial.println("Sending Base64 encoded image...");
  Serial.println("Total image size (Base64): " + String(base64String.length()) + " bytes");
  Serial.println("Splitting into " + String(totalChunks) + " chunks");
  
  for (uint8_t i = 0; i < totalChunks; i++) {
    // Extract a chunk of the base64 string
    String chunk = base64String.substring(i * CHUNK_SIZE, (i + 1) * CHUNK_SIZE);
    
    // Prepare outgoing message
    outgoingMessage.messageType = 1; // Image
    outgoingMessage.chunkID = i;
    outgoingMessage.totalChunks = totalChunks;
    chunk.toCharArray(outgoingMessage.text, CHUNK_SIZE);
    
    // Send the chunk
    esp_now_send(peerDevice, (uint8_t *)&outgoingMessage, sizeof(outgoingMessage));
    
    Serial.print("Sent Image Chunk ");
    Serial.print(i + 1);
    Serial.print(" of ");
    Serial.println(totalChunks);
    
    delay(50);  // Small delay to avoid overwhelming ESP-NOW
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  
  // Set up ESP8266 as Access Point
  WiFi.softAP(ssid, password);
  
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  
  // Start WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  // Set up ESP-NOW
  WiFi.mode(WIFI_AP_STA); // Set ESP8266 as both AP and station
  
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW initialization failed");
    return;
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceive);
  
  // Add peer device
  esp_now_add_peer(peerDevice, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  
  // Define Web server routes
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  webSocket.loop();
  server.handleClient();
}

void handleRoot() {
  server.send(200, "text/html", getHTML());
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not found");
}

String getHTML() {
  String html = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>ESP-NOW Communication</title>
  <style>
    /* Theme Variables */
    :root {
      --bg-color: #f0f2f5;
      --container-bg: #ffffff;
      --text-color: #333333;
      --header-bg: #128C7E;
      --header-color: #ffffff;
      --button-primary: #25D366;
      --button-hover: #128C7E;
      --button-disabled: #cccccc;
      --sent-message-bg: #dcf8c6;
      --received-message-bg: #ffffff;
      --input-bg: #ffffff;
      --divider-color: #e0e0e0;
      --shadow-color: rgba(0, 0, 0, 0.1);
    }

    .dark-theme {
      --bg-color: #202c33;
      --container-bg: #111b21;
      --text-color: #e4e6eb;
      --header-bg: #1f2c34;
      --header-color: #ffffff;
      --button-primary: #00a884;
      --button-hover: #008f72;
      --button-disabled: #393939;
      --sent-message-bg: #005c4b;
      --received-message-bg: #1f2c34;
      --input-bg: #2a3942;
      --divider-color: #222e35;
      --shadow-color: rgba(0, 0, 0, 0.3);
    }

    /* General Styles */
    * {
      box-sizing: border-box;
      -webkit-tap-highlight-color: transparent;
    }
    
    body {
      font-family: 'Segoe UI', 'Helvetica Neue', sans-serif;
      margin: 0;
      padding: 0;
      background-color: var(--bg-color);
      color: var(--text-color);
      transition: all 0.3s ease;
      overscroll-behavior-y: contain;
      height: 100vh;
      height: 100dvh;
      width: 100%;
      position: fixed;
      overflow: hidden;
    }

    .page-container {
      max-width: 100%;
      height: 100%;
      margin: 0 auto;
      display: flex;
      flex-direction: column;
    }
    
    @media (min-width: 768px) {
      .page-container {
        max-width: 768px;
      }
    }

    /* Header */
    header {
      background-color: var(--header-bg);
      color: var(--header-color);
      padding: 12px 16px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      border-radius: 10px 10px 0 0;
      box-shadow: 0 2px 5px var(--shadow-color);
      z-index: 10;
    }

    h1 {
      margin: 0;
      font-size: 1.3em;
      font-weight: 500;
    }
    
    /* Chat Area */
    .chat-container {
      flex-grow: 1;
      overflow-y: auto;
      padding: 16px;
      background-color: var(--bg-color);
      -webkit-overflow-scrolling: touch;
    }

    .message-wrapper {
      display: flex;
      flex-direction: column;
      gap: 10px;
    }

    .message-row {
      display: flex;
      width: 100%;
    }

    .message-row.sent {
      justify-content: flex-end;
    }

    .message-row.received {
      justify-content: flex-start;
    }

    .message {
      max-width: 75%;
      padding: 10px 15px;
      border-radius: 10px;
      position: relative;
      box-shadow: 0 1px 2px var(--shadow-color);
      word-break: break-word;
    }

    .message.sent {
      background-color: var(--sent-message-bg);
      border-top-right-radius: 0;
    }

    .message.received {
      background-color: var(--received-message-bg);
      border-top-left-radius: 0;
    }

    .message-time {
      font-size: 0.7em;
      opacity: 0.7;
      text-align: right;
      margin-top: 5px;
    }

    /* Bottom Bar */
    .bottom-bar {
      display: flex;
      padding: 10px;
      background-color: var(--container-bg);
      border-top: 1px solid var(--divider-color);
    }

    .input-area {
      display: flex;
      width: 100%;
      background-color: var(--input-bg);
      border-radius: 25px;
      padding: 0 10px;
      align-items: center;
    }

    .text-input {
      flex-grow: 1;
      border: none;
      outline: none;
      padding: 10px;
      background-color: transparent;
      color: var(--text-color);
      font-size: 15px;
      resize: none;
      max-height: 100px;
      min-height: 24px;
    }

    .send-button {
      background-color: var(--button-primary);
      color: white;
      border: none;
      border-radius: 50%;
      width: 40px;
      height: 40px;
      display: flex;
      align-items: center;
      justify-content: center;
      cursor: pointer;
      transition: background-color 0.3s;
      min-width: 40px;
    }

    .send-button:hover {
      background-color: var(--button-hover);
    }

    .send-button:disabled {
      background-color: var(--button-disabled);
      cursor: not-allowed;
    }

    /* File Upload */
    .attachment-button {
      background: none;
      border: none;
      color: var(--text-color);
      font-size: 20px;
      cursor: pointer;
      padding: 8px;
      min-width: 36px;
      min-height: 36px;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    .file-input {
      display: none;
    }

    /* Theme Toggle */
    .theme-toggle {
      background: none;
      border: none;
      color: var(--header-color);
      cursor: pointer;
      font-size: 24px;
      padding: 0;
      display: flex;
      align-items: center;
      min-width: 40px;
      min-height: 40px;
      justify-content: center;
    }

    /* Image panel */
    .features-panel {
      background-color: var(--container-bg);
      padding: 15px;
      border-top: 1px solid var(--divider-color);
      display: none;
    }

    .features-panel.active {
      display: block;
    }

    .panel-title {
      font-size: 14px;
      font-weight: 600;
      margin-bottom: 10px;
    }
    
    .image-preview-container {
      margin: 10px 0;
      text-align: center;
      max-height: 200px;
      overflow: hidden;
      border-radius: 8px;
      display: none;
    }
    
    .image-preview-container.active {
      display: block;
    }
    
    .image-preview {
      max-width: 100%;
      max-height: 200px;
      border-radius: 8px;
      box-shadow: 0 2px 5px var(--shadow-color);
    }
    
    .file-input-container {
      display: flex;
      gap: 10px;
      align-items: center;
      margin-bottom: 10px;
    }
    
    .file-input-label {
      background-color: var(--input-bg);
      color: var(--text-color);
      padding: 8px 15px;
      border-radius: 5px;
      cursor: pointer;
      font-size: 14px;
      text-align: center;
      flex-grow: 1;
      border: 1px solid var(--divider-color);
      min-height: 40px;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    
    /* Toast Notifications */
    .toast-container {
      position: fixed;
      bottom: 80px;
      left: 0;
      right: 0;
      display: flex;
      justify-content: center;
      pointer-events: none;
      z-index: 1000;
    }
    
    .toast {
      background-color: rgba(0, 0, 0, 0.8);
      color: white;
      padding: 8px 16px;
      border-radius: 20px;
      font-size: 14px;
      margin-bottom: 10px;
      opacity: 0;
      transform: translateY(20px);
      transition: opacity 0.3s, transform 0.3s;
    }
    
    .toast.show {
      opacity: 1;
      transform: translateY(0);
    }
    
    /* Connection status */
    .status-indicator {
      width: 10px;
      height: 10px;
      border-radius: 50%;
      margin-right: 8px;
      background-color: red;
      display: inline-block;
    }
    
    .status-indicator.connected {
      background-color: green;
    }
    
    /* Main Container */
    .main-container {
      background-color: var(--container-bg);
      flex-grow: 1;
      display: flex;
      flex-direction: column;
      box-shadow: 0 2px 10px var(--shadow-color);
      border-radius: 0 0 10px 10px;
      overflow: hidden;
    }
  </style>
</head>
<body>
  <div class="page-container">
    <header>
      <div style="display: flex; align-items: center;">
        <span class="status-indicator" id="statusIndicator"></span>
        <h1>ESP-NOW Communication</h1>
      </div>
      <button class="theme-toggle" id="themeToggle">🌙</button>
    </header>
    
    <div class="main-container">
      <div class="chat-container" id="chatContainer">
        <div class="message-wrapper" id="messageWrapper">
          <div class="message-row received">
            <div class="message received">
              <div>Welcome to ESP-NOW Communication. Your device is running at 192.168.4.1</div>
              <div class="message-time">Just now</div>
            </div>
          </div>
        </div>
      </div>
      
      <div class="features-panel" id="featuresPanel">
        <div class="panel-title">Send Image</div>
        <div class="file-input-container">
          <label for="imageInput" class="file-input-label">Browse Image</label>
          <input type="file" id="imageInput" class="file-input" accept="image/*" capture="environment">
        </div>
        <div id="imagePreviewContainer" class="image-preview-container">
          <img id="imagePreview" class="image-preview" src="#" alt="Preview">
        </div>
        <button id="sendImage" class="send-button" style="width: 100%; border-radius: 5px;" disabled>Send Image</button>
      </div>
      
      <div class="bottom-bar">
        <div class="input-area">
          <button class="attachment-button" id="attachmentBtn">📎</button>
          <textarea id="textInput" class="text-input" placeholder="Type a message" rows="1"></textarea>
          <button id="sendText" class="send-button" disabled>➤</button>
        </div>
      </div>
    </div>
  </div>
  
  <div class="toast-container" id="toastContainer"></div>

  <script>
    let socket;
    let darkMode = false;
    let isConnected = false;
    
    const connectButton = document.getElementById("themeToggle");
    const sendTextButton = document.getElementById("sendText");
    const sendImageButton = document.getElementById("sendImage");
    const textInput = document.getElementById("textInput");
    const imageInput = document.getElementById("imageInput");
    const messageWrapper = document.getElementById("messageWrapper");
    const statusIndicator = document.getElementById("statusIndicator");
    const attachmentBtn = document.getElementById("attachmentBtn");
    const featuresPanel = document.getElementById("featuresPanel");

    // Theme toggle functionality
    themeToggle.addEventListener("click", () => {
      darkMode = !darkMode;
      document.body.classList.toggle("dark-theme", darkMode);
      themeToggle.textContent = darkMode ? "☀️" : "🌙";
    });

    // Attachment button toggle
    attachmentBtn.addEventListener("click", () => {
      featuresPanel.classList.toggle("active");
    });

    // Auto-resize textarea
    textInput.addEventListener("input", function() {
      this.style.height = "auto";
      this.style.height = (this.scrollHeight) + "px";
    });

    // Show toast message function
    function showToast(message, duration = 3000) {
      const toastContainer = document.getElementById('toastContainer');
      const toast = document.createElement('div');
      toast.className = 'toast';
      toast.textContent = message;
      
      toastContainer.appendChild(toast);
      
      // Force reflow to enable transition
      toast.offsetHeight;
      toast.classList.add('show');
      
      setTimeout(() => {
        toast.classList.remove('show');
        setTimeout(() => {
          toastContainer.removeChild(toast);
        }, 300);
      }, duration);
    }

    function connectWebSocket() {
      // Close any existing connection
      if (socket) {
        socket.close();
      }
      
      // Create WebSocket connection
      let wsProtocol = window.location.protocol === "https:" ? "wss:" : "ws:";
      let wsUrl = wsProtocol + "//" + window.location.hostname + ":81";
      
      socket = new WebSocket(wsUrl);
      
      socket.onopen = function(e) {
        console.log("WebSocket connected");
        isConnected = true;
        statusIndicator.classList.add("connected");
        sendTextButton.disabled = false;
        showToast("Connected to ESP8266!");
      };
      
      socket.onmessage = function(event) {
        console.log("WebSocket message received:", event.data);
        
        try {
          const data = JSON.parse(event.data);
          
          if (data.type === "received") {
            // Text message received from ESP-NOW
            addMessage(data.content, "received");
          } 
          else if (data.type === "sent") {
            // Confirmation of sent message
            // Already handled by addMessage on send
          }
          else if (data.type === "image-received") {
            // Image received from ESP-NOW
            addImageMessage(data.data, "received");
          }
          else if (data.type === "image-ready") {
            // Server is ready to receive image data
            uploadSelectedImage();
          }
          else if (data.type === "image-sent") {
            // Image was sent via ESP-NOW
            showToast(`Image sent (${data.size} bytes)`);
          }
        } catch (error) {
          console.error("Error parsing WebSocket message:", error);
        }
      };
      
      socket.onclose = function(event) {
        if (event.wasClean) {
          console.log(`WebSocket closed cleanly, code=${event.code}, reason=${event.reason}`);
        } else {
          console.log('WebSocket connection died');
        }
        isConnected = false;
        statusIndicator.classList.remove("connected");
        sendTextButton.disabled = true;
        sendImageButton.disabled = true;
        
        // Try to reconnect after a delay
        setTimeout(connectWebSocket, 5000);
      };
      
      socket.onerror = function(error) {
        console.log(`WebSocket error: ${error.message}`);
        showToast("Connection error, trying again...");
      };
    }

    function addMessage(message, type = "sent", saveToStorage = true) {
      // Create a message row for every message
      const messageRow = document.createElement("div");
      messageRow.classList.add("message-row", type);
      
      const messageElement = document.createElement("div");
      messageElement.classList.add("message", type);
      
      const messageContent = document.createElement("div");
      messageContent.textContent = message;
      messageElement.appendChild(messageContent);
      
      const messageTime = document.createElement("div");
      messageTime.classList.add("message-time");
      const now = new Date();
      const timeString = now.getHours().toString().padStart(2, '0') + ":" + 
                        now.getMinutes().toString().padStart(2, '0');
      messageTime.textContent = timeString;
      messageElement.appendChild(messageTime);
      
      messageRow.appendChild(messageElement);
      messageWrapper.appendChild(messageRow);
      
      // Scroll to bottom
      chatContainer.scrollTop = chatContainer.scrollHeight;
      
      return messageRow;
    }
    
    function addImageMessage(base64Data, type = "sent") {
      // Create a message row
      const messageRow = document.createElement("div");
      messageRow.classList.add("message-row", type);
      
      const messageElement = document.createElement("div");
      messageElement.classList.add("message", type);
      
      // Create an image element
      const imageElement = document.createElement("img");
      imageElement.src = "data:image/jpeg;base64," + base64Data;
      imageElement.style.maxWidth = "100%";
      imageElement.style.borderRadius = "5px";
      messageElement.appendChild(imageElement);
      
      const messageTime = document.createElement("div");
      messageTime.classList.add("message-time");
      const now = new Date();
      const timeString = now.getHours().toString().padStart(2, '0') + ":" + 
                        now.getMinutes().toString().padStart(2, '0');
      messageTime.textContent = timeString;
      messageElement.appendChild(messageTime);
      
      messageRow.appendChild(messageElement);
      messageWrapper.appendChild(messageRow);
      
      // Scroll to bottom
      chatContainer.scrollTop = chatContainer.scrollHeight;
      
      return messageRow;
    }

    function sendTextMessage() {
      const message = textInput.value;
      if (!message.trim()) {
        return;
      }

      if (!isConnected) {
        showToast("Not connected to server!");
        return;
      }

      try {
        addMessage(message, "sent");
        socket.send("text:" + message);
        textInput.value = "";
        textInput.style.height = "auto";
      } catch (error) {
        showToast("Failed to send text: " + error.message);
      }
    }

    function prepareImageUpload() {
      if (!isConnected) {
        showToast("Not connected to server!");
        return;
      }
      
      socket.send("image-start:"); // Tell the server we want to upload an image
    }
    
    function uploadSelectedImage() {
      const file = imageInput.files[0];
      if (!file) {
        showToast("Please select an image file!");
        return;
      }

      try {
        const reader = new FileReader();
        reader.onload = function(event) {
          const arrayBuffer = event.target.result;
          addMessage("Sending image...", "sent");
          
          // Send the binary data
          socket.send(arrayBuffer);
        };
        reader.readAsArrayBuffer(file);
      } catch (error) {
        showToast("Failed to send image: " + error.message);
      }
    }

    // Enter key sends message
    textInput.addEventListener("keydown", function(event) {
      if (event.key === "Enter" && !event.shiftKey) {
        event.preventDefault();
        if (!sendTextButton.disabled) {
          sendTextMessage();
        }
      }
    });

    sendTextButton.addEventListener("click", sendTextMessage);
    sendImageButton.addEventListener("click", prepareImageUpload);

    // Setup image preview
    const imagePreview = document.getElementById("imagePreview");
    const imagePreviewContainer = document.getElementById("imagePreviewContainer");
    
    imageInput.addEventListener("change", function() {
      const file = this.files[0];
      if (file) {
        const reader = new FileReader();
        reader.onload = function(e) {
          imagePreview.src = e.target.result;
          imagePreviewContainer.classList.add("active");
          sendImageButton.disabled = !isConnected;
          
          // Update the file input label to show the filename
          const fileInputLabel = document.querySelector(".file-input-label");
          const fileName = file.name.length > 20 ? file.name.substring(0, 17) + "..." : file.name;
          fileInputLabel.textContent = fileName;
        };
        reader.readAsDataURL(file);
      } else {
        imagePreviewContainer.classList.remove("active");
        sendImageButton.disabled = true;
        
        // Reset the label text
        const fileInputLabel = document.querySelector(".file-input-label");
        fileInputLabel.textContent = "Browse Image";
      }
    });
    
    // Connect to WebSocket when page loads
    window.addEventListener('load', function() {
      connectWebSocket();
    });
  </script>
</body>
</html>
  )html";
  
  return html;
} 