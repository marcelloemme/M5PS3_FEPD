#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <mbedtls/md.h>
#include "config.h"

// Storage for image hash
RTC_DATA_ATTR char lastImageHash[65] = {0};  // SHA256 hash (64 chars + null)

/**
 * Calculate SHA256 hash of data
 */
void calculateSHA256(const uint8_t* data, size_t len, char* outputBuffer) {
    byte shaResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, data, len);
    mbedtls_md_finish(&ctx, shaResult);
    mbedtls_md_free(&ctx);

    // Convert to hex string
    for (int i = 0; i < 32; i++) {
        sprintf(&outputBuffer[i * 2], "%02x", shaResult[i]);
    }
    outputBuffer[64] = 0;
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
uint8_t* downloadImage(size_t* imageSize) {
    HTTPClient http;

    Serial.println("\n=== Downloading Image ===");
    Serial.printf("URL: %s\n", IMAGE_URL);

    http.begin(IMAGE_URL);
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
 * Display image on M5PaperS3
 */
bool displayImage(const uint8_t* imageData, size_t imageSize) {
    Serial.println("\n=== Displaying Image ===");

    M5.Display.setRotation(DISPLAY_ROTATION);
    M5.Display.clear();

    // Draw JPEG directly to display
    bool success = M5.Display.drawJpg(imageData, imageSize, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT);

    if (success) {
        Serial.println("Image displayed successfully!");
    } else {
        Serial.println("Failed to display image");
    }

    return success;
}

/**
 * Enter deep sleep
 */
void enterDeepSleep() {
    Serial.println("\n=== Entering Deep Sleep ===");
    Serial.printf("Sleep duration: %llu minutes\n", SLEEP_DURATION_US / 60000000ULL);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_deep_sleep_start();
}

/**
 * Show error message on display
 */
void showError(const char* message) {
    M5.Display.clear();
    M5.Display.setRotation(DISPLAY_ROTATION);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.drawString("Error:", 20, 100);
    M5.Display.setTextSize(2);
    M5.Display.drawString(message, 20, 150);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== M5PaperS3 Image Display ===");
    Serial.printf("Display: %dx%d\n", M5.Display.width(), M5.Display.height());
    Serial.printf("Last image hash: %s\n", lastImageHash[0] ? lastImageHash : "none");

    // Connect to WiFi
    if (!connectWiFi()) {
        showError("WiFi connection failed");
        delay(5000);
        enterDeepSleep();
        return;
    }

    // Download image
    size_t imageSize = 0;
    uint8_t* imageData = downloadImage(&imageSize);

    if (!imageData) {
        showError("Image download failed");
        delay(5000);
        enterDeepSleep();
        return;
    }

    // Calculate hash of downloaded image
    char currentHash[65];
    calculateSHA256(imageData, imageSize, currentHash);
    Serial.printf("Current image hash: %s\n", currentHash);

    // Check if image has changed
    if (strcmp(currentHash, lastImageHash) == 0) {
        Serial.println("Image unchanged, skipping display update");
        free(imageData);
        enterDeepSleep();
        return;
    }

    // Display new image
    if (displayImage(imageData, imageSize)) {
        // Update stored hash
        strcpy(lastImageHash, currentHash);
        Serial.println("Image hash updated");
    } else {
        showError("Failed to display image");
    }

    // Cleanup
    free(imageData);

    // Wait a bit to see the image
    delay(2000);

    // Enter deep sleep
    enterDeepSleep();
}

void loop() {
    // This should never be reached due to deep sleep
    delay(1000);
}
