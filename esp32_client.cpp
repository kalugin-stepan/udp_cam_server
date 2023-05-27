#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_camera.h"

#define DEBUG

#define SSID "Izobretay_Luxury"
#define PASSWORD "SkazhiteI"

#define SERVER_HOST "192.168.0.105"

uint16_t server_port = 5001;

#define ID "000000000000000000000000000000000000"

#define PACKAGE_SIZE 1024

// CAMERA_MODEL_AI_THINKER
#define CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define DEBUG

WiFiUDP udp;

void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
#ifdef DEBUG
    Serial.print("Connecting to ");
    Serial.println(SSID);
#endif
    WiFi.begin(SSID, PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
    #ifdef DEBUG
        Serial.print(".");
    #endif
        delay(500);
    }
#ifdef DEBUG
    Serial.print("\nESP32-CAM IP Address: ");
    Serial.println(WiFi.localIP());
#endif

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_CIF;  // 400x296
    config.jpeg_quality = 10;
    config.fb_count = 2;

    // if (psramFound()) {
    //     config.frame_size = FRAMESIZE_SVGA;
    //     config.jpeg_quality = 15; // 0-63 lower number means higher quality
    //     config.fb_count = 2;
    // }
    // else {
    //     config.frame_size = FRAMESIZE_CIF;
    //     config.jpeg_quality = 17; // 0-63 lower number means higher quality
    //     config.fb_count = 1;
    // }

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
    #ifdef DEBUG
        Serial.printf("Camera init failed with error 0x%x", err);
    #endif
        delay(1000);
        ESP.restart();
    }

    udp.begin(WiFi.localIP(), 0);

    start:
    udp.beginPacket(SERVER_HOST, server_port);
    udp.write((const uint8_t*)ID, 36);
    udp.endPacket();
    uint8_t failed = 1;
    int rec;
    while (true) {
        rec = udp.parsePacket();
        Serial.println(rec);
        if (rec != -1 && rec != 0) break;
    }
    udp.read((char*)&failed, 1);
    if (failed == 1) {
        Serial.println("Failed");
        delay(5000);
        goto start;
    }
    server_port = udp.remotePort();
}

void loop() {
    camera_fb_t* fb = NULL;
    fb = esp_camera_fb_get();
    if (!fb) {
    #ifdef DEBUG
        Serial.println("Camera capture failed");
    #endif
        delay(1000);
        ESP.restart();
        return;
    }

    for (size_t i = 0; i < fb->len; i += PACKAGE_SIZE) {
        udp.beginPacket(SERVER_HOST, server_port);
        size_t cur_package_size = fb->len - i >= PACKAGE_SIZE ? PACKAGE_SIZE : fb->len - i;
        udp.write(fb->buf + i, cur_package_size);
        udp.endPacket();
        udp.flush();
    }
    
    esp_camera_fb_return(fb);
}