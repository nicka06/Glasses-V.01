#pragma once // Prevents the file from being included multiple times

// --- LED Pins ---
const int LED_RED_PIN = 38;
const int LED_BLUE_PIN = 15;

// --- Switch & Button Pins ---
const int SWITCH_STATE_PIN = 4;   // Manual slide switch
const int SWITCH_BUTTON_PIN = 39;  // Momentary push button

// --- Amplifier (Speaker) Pins ---
const int AMP_SHUTDOWN_PIN = 23;
// I2S is a protocol for audio
const int I2S_AMP_BCLK_PIN = 7;  // Bit Clock
const int I2S_AMP_LRC_PIN = 5;   // Left/Right Clock (Word Select)
const int I2S_AMP_DIN_PIN = 6;   // Data In

// --- Microphone Pins ---
const int I2S_MIC_SD_PIN = 20;   // Serial Data
const int I2S_MIC_SCK_PIN = 21;  // Serial Clock
const int I2S_MIC_WS_PIN = 22;   // Word Select
