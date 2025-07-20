#include <Arduino.h>
#include "hardware_pins.h"
#include <WiFi.h>
#include "driver/i2s.h"

// --- WiFi & Server Configuration ---
// Replace with your network credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Replace with your server's IP address and port
const char* server_ip = "YOUR_SERVER_IP";
const int server_port = 1234;

WiFiClient client;

// --- Audio Configuration ---
// I2S is the audio protocol we use
#define I2S_MIC_PORT I2S_NUM_0
#define I2S_SPEAKER_PORT I2S_NUM_1

// Audio sample rate and buffer size
const int SAMPLE_RATE = 16000; // 16kHz is good for voice
const int BITS_PER_SAMPLE = 16;
const int AUDIO_BUFFER_SIZE = 1024; // How many audio samples to process at a time

// --- Amplifier Power Management ---
unsigned long last_audio_received_time = 0;
const int AMP_POWER_DOWN_DELAY_MS = 200; // Turn amp off after 200ms of silence
bool is_amp_on = false;

// --- Function Prototypes ---
void setup_wifi();
void setup_i2s_microphone();
void setup_i2s_speaker();
void handle_microphone();
void handle_speaker();
void manage_amplifier();

// =================================================================
// SETUP: This runs once when you turn the power on
// =================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("Booting up AR Glasses Firmware...");

  // Configure hardware pins
  pinMode(LED_BLUE_PIN, OUTPUT);
  pinMode(AMP_SHUTDOWN_PIN, OUTPUT);
  pinMode(SWITCH_BUTTON_PIN, INPUT_PULLUP);

  // Keep amplifier and LED off initially to save power
  digitalWrite(AMP_SHUTDOWN_PIN, LOW);
  digitalWrite(LED_BLUE_PIN, LOW);

  // Connect to WiFi
  setup_wifi();

  // Configure audio hardware
  setup_i2s_microphone();
  setup_i2s_speaker();

  Serial.println("Setup complete. Ready.");
}

// =================================================================
// LOOP: This runs continuously after setup
// =================================================================
void loop() {
  // Ensure we are always connected to the server
  if (!client.connected()) {
    Serial.println("Reconnecting to server...");
    if (!client.connect(server_ip, server_port)) {
      Serial.println("Connection failed. Retrying...");
      delay(1000);
      return; // Try again on the next loop
    }
    Serial.println("Connected to server.");
  }

  // Handle microphone based on button press
  handle_microphone();

  // Handle speaker based on incoming data
  handle_speaker();

  // Manage amplifier power based on speaker activity
  manage_amplifier();
}

// =================================================================
// HELPER FUNCTIONS
// =================================================================

void handle_microphone() {
  // Check if the button is currently being held down
  if (digitalRead(SWITCH_BUTTON_PIN) == LOW) {
    digitalWrite(LED_BLUE_PIN, HIGH); // Turn on LED to indicate listening

    // --- Read from Microphone and Send to Server ---
    int32_t raw_audio_buffer[AUDIO_BUFFER_SIZE];
    int16_t processed_audio_buffer[AUDIO_BUFFER_SIZE];
    size_t bytes_read = 0;

    // Read audio data from the I2S microphone
    i2s_read(I2S_MIC_PORT, &raw_audio_buffer, sizeof(raw_audio_buffer), &bytes_read, portMAX_DELAY);

    if (bytes_read > 0) {
      // Convert 32-bit samples to 16-bit
      for (int i = 0; i < bytes_read / 4; i++) {
        processed_audio_buffer[i] = (int16_t)(raw_audio_buffer[i] >> 16);
      }
      // Send the processed audio data to the server
      client.write((const uint8_t*)processed_audio_buffer, bytes_read / 2);
    }
  } else {
    // If button is not pressed, make sure LED is off
    digitalWrite(LED_BLUE_PIN, LOW);
  }
}

void handle_speaker() {
  // Check if the server has sent any audio data
  if (client.available()) {
    // If yes, turn on the amplifier if it's not already on
    if (!is_amp_on) {
      Serial.println("Audio received, turning amplifier ON.");
      digitalWrite(AMP_SHUTDOWN_PIN, HIGH);
      is_amp_on = true;
    }
    
    // Update the timer to show we just received audio
    last_audio_received_time = millis();

    // --- Play Audio on Speaker ---
    uint8_t server_audio_buffer[AUDIO_BUFFER_SIZE * 2];
    size_t bytes_written = 0;
    int bytes_received = client.read(server_audio_buffer, sizeof(server_audio_buffer));

    if (bytes_received > 0) {
      // Write the received audio data directly to the I2S speaker
      i2s_write(I2S_SPEAKER_PORT, server_audio_buffer, bytes_received, &bytes_written, portMAX_DELAY);
    }
  }
}

void manage_amplifier() {
  // If the amplifier is on, check if it's been silent for too long
  if (is_amp_on) {
    if (millis() - last_audio_received_time > AMP_POWER_DOWN_DELAY_MS) {
      Serial.println("Silence detected, turning amplifier OFF.");
      digitalWrite(AMP_SHUTDOWN_PIN, LOW);
      is_amp_on = false;
    }
  }
}


void setup_wifi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup_i2s_microphone() {
  i2s_config_t i2s_mic_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t i2s_mic_pins = {
    .bck_io_num = I2S_MIC_SCK_PIN,
    .ws_io_num = I2S_MIC_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD_PIN
  };

  i2s_driver_install(I2S_MIC_PORT, &i2s_mic_config, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &i2s_mic_pins);
  i2s_set_clk(I2S_MIC_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}

void setup_i2s_speaker() {
  i2s_config_t i2s_speaker_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = (i2s_bits_per_sample_t)BITS_PER_SAMPLE,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t i2s_speaker_pins = {
    .bck_io_num = I2S_AMP_BCLK_PIN,
    .ws_io_num = I2S_AMP_LRC_PIN,
    .data_out_num = I2S_AMP_DIN_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_SPEAKER_PORT, &i2s_speaker_config, 0, NULL);
  i2s_set_pin(I2S_SPEAKER_PORT, &i2s_speaker_pins);
}
