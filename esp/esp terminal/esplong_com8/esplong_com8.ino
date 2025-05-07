#include <ESP8266WiFi.h>
#include <espnow.h>
// Remove external Base64 library and use our own implementation
// #include <Base64.h>

// MAC addresses of both devices
uint8_t allowedDevices[][6] = {
  {0xFC, 0xF5, 0xC4, 0xA6, 0x40, 0x80}, // Device 1 MAC
  {0xEC, 0xFA, 0xBC, 0xCB, 0x4A, 0x94}  // Device 2 MAC
};

// 16-byte encryption key (same on both devices; optional)
uint8_t encryptionKey[16] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0xA7, 0xB8, 
                             0xC9, 0xDA, 0xEB, 0xFC, 0xAB, 0xBC, 0xCD, 0xDE};

// Maximum chunk size for ESP-NOW messages
#define CHUNK_SIZE 240  // 240 bytes for payload (max 250 bytes for ESP-NOW)

// Message structure
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
String reassembledImageBase64 = ""; // For storing received base64 image data

// Base64 encoding table
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

// Calculate size needed for a Base64-encoded buffer
size_t base64_encoded_size(size_t input_length) {
  return 4 * ((input_length + 2) / 3);
}

// Helper function: Check if a MAC address is in the whitelist
bool isAllowedDevice(uint8_t *mac_addr) {
  for (int i = 0; i < sizeof(allowedDevices) / sizeof(allowedDevices[0]); i++) {
    if (memcmp(mac_addr, allowedDevices[i], 6) == 0) {
      return true; // MAC address matches
    }
  }
  return false; // MAC address not in whitelist
}

// Helper function: Log traffic
void logTraffic(uint8_t *mac_addr, bool allowed) {
  String macStr = "";
  for (int i = 0; i < 6; i++) {
    macStr += String(mac_addr[i], HEX);
    if (i < 5) macStr += ":";
  }
  // if (allowed) {
  //   Serial.println("Allowed communication from: " + macStr);
  // } else {
  //   Serial.println("Blocked communication from: " + macStr);
  // }
}

// Callback when data is sent
void onSent(uint8_t *mac_addr, uint8_t sendStatus) {
  // Serial.print("Last Packet Send Status: ");
  // Serial.println(sendStatus == 0 ? "Delivery Success" : "Delivery Fail");
}

// Callback when data is received
void onReceive(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  // Check if the device is allowed
  bool allowed = isAllowedDevice(mac_addr);
  logTraffic(mac_addr, allowed);

  if (!allowed) {
    Serial.println("Unauthorized device tried to connect!");
    return; // Ignore the packet
  }

  memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));

  if (incomingMessage.messageType == 0) {
    // Handle text message
    // Serial.print("Received Text Chunk ");
    // Serial.print(incomingMessage.chunkID);
    // Serial.print(" of ");
    // Serial.println(incomingMessage.totalChunks);

    // Append text chunk to reassembledText
    reassembledText += incomingMessage.text;

    // If all chunks are received, print the full message
    if (incomingMessage.chunkID == incomingMessage.totalChunks - 1) {
      // Serial.println("Reassembled Text Message: ");
      Serial.println(reassembledText);
      reassembledText = ""; // Clear for next message
    }
  } else if (incomingMessage.messageType == 1) {
    // Handle image chunk (base64 encoded)
    Serial.print("Received Image Chunk ");
    Serial.print(incomingMessage.chunkID);
    Serial.print(" of ");
    Serial.println(incomingMessage.totalChunks);

    // Append base64 chunk to reassembledImageBase64
    reassembledImageBase64 += incomingMessage.text;

    // If all chunks are received, the image is complete
    if (incomingMessage.chunkID == incomingMessage.totalChunks - 1) {
      Serial.println("Complete Base64 Image Received");
      Serial.println("Image size (Base64): " + String(reassembledImageBase64.length()) + " bytes");
      
      // The base64 image data is now in reassembledImageBase64
      // You can print a portion of it to verify
      Serial.println("Image data preview: " + reassembledImageBase64.substring(0, 64) + "...");
      
      // To use the image, you would decode it from Base64 back to binary
      // (Decoding function can be implemented if needed)
      
      // Reset for next image
      reassembledImageBase64 = "";
    }
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW initialization failed");
    return;
  }

  // Set device role as both sender and receiver
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);

  // Register callback functions
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  // Add peer device with specified MAC address and set encryption key (optional)
  esp_now_add_peer(allowedDevices[0], ESP_NOW_ROLE_COMBO, 1, encryptionKey, 16);
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
    esp_now_send(allowedDevices[0], (uint8_t *)&outgoingMessage, sizeof(outgoingMessage));

    // Serial.print("Sent Text Chunk ");
    // Serial.print(i);
    // Serial.print(" of ");
    // Serial.println(totalChunks);

    delay(100);  // Small delay to avoid overwhelming ESP-NOW
  }
}

// New function to encode and send an image file as base64
void sendImageBase64(const uint8_t* rawData, size_t dataSize) {
  // Calculate base64 length and allocate memory
  size_t base64Length = base64_encoded_size(dataSize);
  
  // Encode the raw data to base64
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
    esp_now_send(allowedDevices[0], (uint8_t *)&outgoingMessage, sizeof(outgoingMessage));
    
    Serial.print("Sent Image Chunk ");
    Serial.print(i + 1);
    Serial.print(" of ");
    Serial.println(totalChunks);
    
    delay(100);  // Small delay to avoid overwhelming ESP-NOW
  }
}

void loop() {
  // Check if data is available in the Serial Monitor
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');  // Read the input until newline
    input.trim();                                // Remove any leading/trailing whitespace

    if (input.startsWith("text:")) {
      String message = input.substring(5);  // Extract text after "text:"
      // Serial.println("Sending Text Message...");
      sendLongMessage(message);
    } else if (input.startsWith("image:")) {
      String param = input.substring(6);
      if (param == "upload") {
        // Handle binary image upload from terminal
        Serial.println("Ready to receive binary image. Send size (2 bytes) followed by data...");
        
        // Wait for size data
        unsigned long timeout = millis() + 10000; // 10 second timeout
        while (Serial.available() < 2) {
          if (millis() > timeout) {
            Serial.println("Timeout waiting for image size");
            return;
          }
          yield(); // Allow ESP8266 to handle background tasks
        }
        
        // Read image size (2 bytes, little endian)
        uint16_t imageSize = Serial.read() | (Serial.read() << 8);
        Serial.println("Expecting " + String(imageSize) + " bytes of image data...");
        
        if (imageSize > 0 && imageSize < 20000) { // Reasonable size limit
          // Allocate buffer for image data
          uint8_t* imageBuffer = new uint8_t[imageSize];
          if (!imageBuffer) {
            Serial.println("Not enough memory for image");
            return;
          }
          
          // Read image data
          size_t bytesRead = 0;
          timeout = millis() + 30000; // 30 second timeout
          while (bytesRead < imageSize) {
            if (Serial.available() > 0) {
              imageBuffer[bytesRead++] = Serial.read();
              if (bytesRead % 100 == 0) {
                Serial.println("Received " + String(bytesRead) + " of " + String(imageSize) + " bytes");
              }
            }
            
            if (millis() > timeout) {
              Serial.println("Timeout receiving image data");
              delete[] imageBuffer;
              return;
            }
            yield(); // Allow ESP8266 to handle background tasks
          }
          
          Serial.println("Image received, sending via ESP-NOW...");
          sendImageBase64(imageBuffer, imageSize);
          
          // Free the buffer
          delete[] imageBuffer;
        } else {
          Serial.println("Invalid image size");
        }
      } else {
        // For test image
        Serial.println("Sending test image...");
        uint8_t testImageData[100];
        for (int i = 0; i < 100; i++) {
          testImageData[i] = i % 256; // Sample data
        }
        sendImageBase64(testImageData, 100);
      }
    } else if (input == "help") {
      // Add help command
      Serial.println("Available commands:");
      Serial.println("  text:<message>    - Send a text message");
      Serial.println("  image:test        - Send a test image");
      Serial.println("  image:upload      - Upload an image via serial");
      Serial.println("  help              - Show this help");
    }
  }
}
