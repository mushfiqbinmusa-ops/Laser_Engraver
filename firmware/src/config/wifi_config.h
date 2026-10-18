#pragma once

// =============================================================================
//  Wi-Fi Network Configuration for ESP32-S3
// =============================================================================

// 1. Station (STA) Mode: Connect to your home or workshop Wi-Fi network
// Set these to your local Wi-Fi router's credentials:
#define WIFI_SSID         "403"
#define WIFI_PASSWORD     "mks1998!"

// 2. Access Point (SoftAP) Fallback Mode:
// If the ESP32 cannot connect to your Wi-Fi within 10 seconds,
// or if WIFI_SSID is unchanged, it automatically broadcasts its own Wi-Fi:
#define WIFI_AP_SSID      "Laser-XY-Scanner"
#define WIFI_AP_PASS      "laser1234"      // Minimum 8 characters

// Control TCP Socket Server Port (Default: 8888)
#define WIFI_TCP_PORT     8888
