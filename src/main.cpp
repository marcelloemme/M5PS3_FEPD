#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include "config.h"

// Storage for last displayed image filename and data
RTC_DATA_ATTR char lastImageFilename[64] = {0};
RTC_DATA_ATTR bool hasValidImage = false;  // Track if we have a valid image displayed

/**
 * Get latest image filename from GitHub API
 * Returns the filename of the latest image in the image/ folder
 */
String getLatestImageFilename() {
    HTTPClient http;
    String latestFilename = "";

    Serial.println("\n=== Fetching image list from GitHub ===");
    Serial.printf("API URL: %s\n", GITHUB_API_URL);

    http.begin(GITHUB_API_URL);
    http.addHeader("Accept", "application/vnd.github.v3+json");

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("GitHub API request failed: %d\n", httpCode);
        http.end();
        return "";
    }

    String payload = http.getString();
    http.end();

    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.printf("JSON parsing failed: %s\n", error.c_str());
        return "";
    }

    // Find all .jpg files and get the latest one (alphabetically, which matches timestamp)
    for (JsonObject item : doc.as<JsonArray>()) {
        const char* name = item["name"];
        const char* type = item["type"];

        if (type && strcmp(type, "file") == 0 && name) {
            String filename = String(name);
            if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) {
                // Keep the latest (max alphabetically = newest timestamp)
                if (filename > latestFilename) {
                    latestFilename = filename;
                }
            }
        }
    }

    if (latestFilename.length() > 0) {
        Serial.printf("Latest image found: %s\n", latestFilename.c_str());
    } else {
        Serial.println("No images found in repository");
    }

    return latestFilename;
}

/**
 * Connect to WiFi
 */
bool connectWiFi() {
    Serial.println("\n=== Connecting to WiFi ===");
    Serial.printf("SSID: %s\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    } else {
        Serial.println("\nWiFi connection failed!");
        return false;
    }
}

/**
 * Download image from URL and return data
 */
uint8_t* downloadImage(const String& filename, size_t* imageSize) {
    HTTPClient http;

    // Construct GitHub raw URL
    String imageUrl = "https://raw.githubusercontent.com/";
    imageUrl += GITHUB_USER;
    imageUrl += "/";
    imageUrl += GITHUB_REPO;
    imageUrl += "/";
    imageUrl += GITHUB_BRANCH;
    imageUrl += "/image/";
    imageUrl += filename;

    Serial.println("\n=== Downloading Image ===");
    Serial.printf("URL: %s\n", imageUrl.c_str());

    http.begin(imageUrl);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("HTTP GET failed: %d\n", httpCode);
        http.end();
        return nullptr;
    }

    *imageSize = http.getSize();
    Serial.printf("Image size: %d bytes\n", *imageSize);

    if (*imageSize <= 0 || *imageSize > 1024 * 1024) {  // Max 1MB
        Serial.println("Invalid image size");
        http.end();
        return nullptr;
    }

    // Allocate memory for image
    uint8_t* imageData = (uint8_t*)malloc(*imageSize);
    if (!imageData) {
        Serial.println("Failed to allocate memory for image");
        http.end();
        return nullptr;
    }

    // Read image data
    WiFiClient* stream = http.getStreamPtr();
    size_t bytesRead = 0;
    while (http.connected() && bytesRead < *imageSize) {
        size_t available = stream->available();
        if (available) {
            size_t toRead = min(available, *imageSize - bytesRead);
            stream->readBytes(imageData + bytesRead, toRead);
            bytesRead += toRead;
        } else {
            delay(10);
        }
    }

    http.end();

    if (bytesRead != *imageSize) {
        Serial.printf("Download incomplete: %d/%d bytes\n", bytesRead, *imageSize);
        free(imageData);
        return nullptr;
    }

    Serial.println("Download complete!");
    return imageData;
}

/**
 * Display image on M5PaperS3 (only refresh if changed)
 */
bool displayImage(const uint8_t* imageData, size_t imageSize) {
    Serial.println("\n=== Displaying Image ===");

    M5.Display.setRotation(DISPLAY_ROTATION);

    // Clear only once before drawing new image
    M5.Display.clearDisplay();

    // Draw JPEG directly to display buffer (no refresh yet)
    bool success = M5.Display.drawJpg(imageData, imageSize, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT);

    if (success) {
        // Single refresh after image is drawn
        M5.Display.display();
        Serial.println("Image displayed successfully!");
    } else {
        Serial.println("Failed to display image");
    }

    return success;
}

/**
 * Enter deep sleep with maximum power savings
 */
void enterDeepSleep() {
    Serial.println("\n=== Entering Deep Sleep ===");
    Serial.printf("Sleep duration: %llu minutes\n", SLEEP_DURATION_US / 60000000ULL);
    Serial.flush();  // Ensure serial output completes

    // Shutdown WiFi completely
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();

    // Disable Bluetooth
    esp_bt_controller_disable();

    // Power down peripherals
    M5.Display.sleep();  // E-paper display to sleep mode

    // Disable all wakeup sources except timer
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    // Configure timer wakeup
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);

    // Enter deep sleep (RTC memory preserved, minimal power)
    esp_deep_sleep_start();
}

/**
 * Show temporary error message (2 seconds) then restore previous image
 */
void showTemporaryError(const char* message) {
    Serial.printf("Error: %s\n", message);

    // If we have a valid image, don't touch the display at all
    if (hasValidImage) {
        Serial.println("Keeping previous image on display (no refresh)");
        // E-paper retains the last displayed image without power
        // No need to do anything - just return
    } else {
        // First boot, no previous image - show full error screen
        M5.Display.clearDisplay();
        M5.Display.setRotation(DISPLAY_ROTATION);
        M5.Display.setTextSize(3);
        M5.Display.setTextColor(TFT_BLACK);
        M5.Display.drawString("Error:", 20, 100);
        M5.Display.setTextSize(2);
        M5.Display.drawString(message, 20, 150);
        M5.Display.display();
    }
}

void setup() {
    // Configure M5 with minimal power consumption
    auto cfg = M5.config();
    cfg.output_power = false;  // Disable external power output
    M5.begin(cfg);

    // Disable unnecessary peripherals
    M5.In_I2C.release();  // Release I2C if not needed

    Serial.begin(115200);
    delay(500);  // Reduced delay
    Serial.println("\n=== M5PaperS3 Image Display ===");
    Serial.printf("Display: %dx%d\n", M5.Display.width(), M5.Display.height());
    Serial.printf("Last displayed image: %s\n", lastImageFilename[0] ? lastImageFilename : "none");
    Serial.printf("Has valid image: %s\n", hasValidImage ? "yes" : "no");

    // Connect to WiFi
    if (!connectWiFi()) {
        showTemporaryError("WiFi failed");
        enterDeepSleep();
        return;
    }

    // Get latest image filename from GitHub
    String latestFilename = getLatestImageFilename();

    if (latestFilename.length() == 0) {
        showTemporaryError("No images in repo");
        enterDeepSleep();
        return;
    }

    // Check if image has changed
    if (strcmp(latestFilename.c_str(), lastImageFilename) == 0) {
        Serial.println("Image unchanged, keeping current display");
        enterDeepSleep();
        return;
    }

    // Download new image
    size_t imageSize = 0;
    uint8_t* imageData = downloadImage(latestFilename, &imageSize);

    if (!imageData) {
        showTemporaryError("Download failed");
        enterDeepSleep();
        return;
    }

    // Display new image
    if (displayImage(imageData, imageSize)) {
        // Update stored filename and mark as valid
        strncpy(lastImageFilename, latestFilename.c_str(), sizeof(lastImageFilename) - 1);
        lastImageFilename[sizeof(lastImageFilename) - 1] = '\0';
        hasValidImage = true;
        Serial.printf("Successfully updated to: %s\n", lastImageFilename);

        // Wait to admire the new image
        delay(2000);
    } else {
        showTemporaryError("Display failed");
    }

    // Cleanup
    free(imageData);

    // Enter deep sleep
    enterDeepSleep();
}

void loop() {
    // This should never be reached due to deep sleep
    delay(1000);
}
