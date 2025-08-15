#include <Arduino.h>
#include "hardware_pins.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "driver/i2s.h"
#include <base64.h>

// --- WiFi Configuration ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// --- OpenAI Configuration ---
const char* openai_api_key = "YOUR_OPENAI_API_KEY";
const char* openai_whisper_url = "https://api.openai.com/v1/audio/transcriptions";
const char* openai_chat_url = "https://api.openai.com/v1/chat/completions";
const char* openai_tts_url = "https://api.openai.com/v1/audio/speech";

// --- Audio Configuration ---
#define I2S_MIC_PORT I2S_NUM_0
#define I2S_SPEAKER_PORT I2S_NUM_1
const int SAMPLE_RATE = 16000;
const int BITS_PER_SAMPLE = 16;
const int AUDIO_BUFFER_SIZE = 1024;

// --- State Management ---
bool is_recording = false;
bool is_processing = false;
std::vector<int16_t> audio_recording;

void setup() {
  Serial.begin(115200);
  Serial.println("Booting up AI Glasses...");

  // Configure pins
  pinMode(LED_BLUE_PIN, OUTPUT);
  pinMode(AMP_SHUTDOWN_PIN, OUTPUT);
  pinMode(SWITCH_BUTTON_PIN, INPUT_PULLUP);
  
  digitalWrite(AMP_SHUTDOWN_PIN, LOW);
  digitalWrite(LED_BLUE_PIN, LOW);

  // Connect to WiFi
  setup_wifi();
  
  // Setup audio
  setup_i2s_microphone();
  setup_i2s_speaker();

  Serial.println("Ready for voice commands!");
}

void loop() {
  handle_voice_command();
  delay(10);
}

void handle_voice_command() {
  // Start recording when button pressed
  if (digitalRead(SWITCH_BUTTON_PIN) == LOW && !is_recording && !is_processing) {
    start_recording();
  }
  
  // Stop recording when button released
  if (digitalRead(SWITCH_BUTTON_PIN) == HIGH && is_recording) {
    stop_recording_and_process();
  }
  
  // Continue recording while button held
  if (is_recording) {
    record_audio_chunk();
  }
}

void start_recording() {
  Serial.println("🎤 Started recording...");
  digitalWrite(LED_BLUE_PIN, HIGH);
  is_recording = true;
  audio_recording.clear();
}

void record_audio_chunk() {
  int32_t raw_buffer[AUDIO_BUFFER_SIZE];
  size_t bytes_read = 0;
  
  i2s_read(I2S_MIC_PORT, raw_buffer, sizeof(raw_buffer), &bytes_read, 0);
  
  if (bytes_read > 0) {
    for (int i = 0; i < bytes_read / 4; i++) {
      audio_recording.push_back((int16_t)(raw_buffer[i] >> 16));
    }
  }
}

void stop_recording_and_process() {
  Serial.println("🛑 Stopped recording, processing...");
  digitalWrite(LED_BLUE_PIN, LOW);
  is_recording = false;
  is_processing = true;
  
  if (audio_recording.size() > 0) {
    process_voice_command();
  }
  
  is_processing = false;
}

void process_voice_command() {
  // Step 1: Convert speech to text using Whisper API
  String user_text = speech_to_text();
  if (user_text.length() == 0) return;
  
  // Step 2: Get AI response using ChatGPT API
  String ai_response = get_ai_response(user_text);
  if (ai_response.length() == 0) return;
  
  Serial.println(ai_response);
  
  // Step 3: Convert response to speech and play
  text_to_speech_and_play(ai_response);
}

String speech_to_text() {
  HTTPClient http;
  http.begin(openai_whisper_url);
  http.addHeader("Authorization", "Bearer " + String(openai_api_key));
  http.addHeader("Content-Type", "multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW");
  
  // Create WAV file in memory
  String wav_data = create_wav_from_audio();
  String base64_audio = base64::encode((uint8_t*)wav_data.c_str(), wav_data.length());
  
  // Create multipart form data
  String body = "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n";
  body += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
  body += "Content-Type: audio/wav\r\n\r\n";
  body += wav_data + "\r\n";
  body += "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n";
  body += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
  body += "whisper-1\r\n";
  body += "------WebKitFormBoundary7MA4YWxkTrZu0gW--\r\n";
  
  int httpResponseCode = http.POST(body);
  String response = "";
  
  if (httpResponseCode == 200) {
    response = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    response = doc["text"].as<String>();
  }
  
  http.end();
  return response;
}

String get_ai_response(String user_text) {
  HTTPClient http;
  http.begin(openai_chat_url);
  http.addHeader("Authorization", "Bearer " + String(openai_api_key));
  http.addHeader("Content-Type", "application/json");
  
  DynamicJsonDocument request(1024);
  request["model"] = "gpt-3.5-turbo";
  request["messages"][0]["role"] = "user";
  request["messages"][0]["content"] = user_text;
  request["max_tokens"] = 150;
  
  String requestBody;
  serializeJson(request, requestBody);
  
  int httpResponseCode = http.POST(requestBody);
  String response = "";
  
  if (httpResponseCode == 200) {
    String jsonResponse = http.getString();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, jsonResponse);
    response = doc["choices"][0]["message"]["content"].as<String>();
  }
  
  http.end();
  return response;
}

void text_to_speech_and_play(String text) {
  HTTPClient http;
  http.begin(openai_tts_url);
  http.addHeader("Authorization", "Bearer " + String(openai_api_key));
  http.addHeader("Content-Type", "application/json");
  
  DynamicJsonDocument request(512);
  request["model"] = "tts-1";
  request["input"] = text;
  request["voice"] = "alloy";
  request["response_format"] = "wav";
  
  String requestBody;
  serializeJson(request, requestBody);
  
  int httpResponseCode = http.POST(requestBody);
  
  if (httpResponseCode == 200) {
    // Get audio data and play it
    WiFiClient* stream = http.getStreamPtr();
    play_audio_stream(stream);
  }
  
  http.end();
}

void play_audio_stream(WiFiClient* stream) {
  digitalWrite(AMP_SHUTDOWN_PIN, HIGH); // Turn on amplifier
  
  uint8_t buffer[1024];
  size_t bytes_written;
  
  while (stream->available()) {
    int bytes_read = stream->readBytes(buffer, sizeof(buffer));
    if (bytes_read > 0) {
      i2s_write(I2S_SPEAKER_PORT, buffer, bytes_read, &bytes_written, portMAX_DELAY);
    }
  }
  
  delay(100);
  digitalWrite(AMP_SHUTDOWN_PIN, LOW); // Turn off amplifier
}

String create_wav_from_audio() {
  // Simple WAV header creation
  int data_size = audio_recording.size() * 2;
  int file_size = data_size + 36;
  
  String wav = "";
  wav += "RIFF";
  wav += (char)(file_size & 0xFF);
  wav += (char)((file_size >> 8) & 0xFF);
  wav += (char)((file_size >> 16) & 0xFF);
  wav += (char)((file_size >> 24) & 0xFF);
  wav += "WAVE";
  wav += "fmt ";
  wav += (char)16; wav += (char)0; wav += (char)0; wav += (char)0; // fmt chunk size
  wav += (char)1; wav += (char)0; // PCM format
  wav += (char)1; wav += (char)0; // mono
  wav += (char)(SAMPLE_RATE & 0xFF); wav += (char)((SAMPLE_RATE >> 8) & 0xFF);
  wav += (char)((SAMPLE_RATE >> 16) & 0xFF); wav += (char)((SAMPLE_RATE >> 24) & 0xFF);
  wav += (char)((SAMPLE_RATE * 2) & 0xFF); wav += (char)(((SAMPLE_RATE * 2) >> 8) & 0xFF);
  wav += (char)(((SAMPLE_RATE * 2) >> 16) & 0xFF); wav += (char)(((SAMPLE_RATE * 2) >> 24) & 0xFF);
  wav += (char)2; wav += (char)0; // block align
  wav += (char)16; wav += (char)0; // bits per sample
  wav += "data";
  wav += (char)(data_size & 0xFF);
  wav += (char)((data_size >> 8) & 0xFF);
  wav += (char)((data_size >> 16) & 0xFF);
  wav += (char)((data_size >> 24) & 0xFF);
  
  // Add audio data
  for (int16_t sample : audio_recording) {
    wav += (char)(sample & 0xFF);
    wav += (char)((sample >> 8) & 0xFF);
  }
  
  return wav;
}

void setup_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
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
