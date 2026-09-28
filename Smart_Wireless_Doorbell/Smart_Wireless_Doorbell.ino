
// SPDX-License-Identifier: MIT
// Outdoor ESP32-CAM Smart Doorbell: 4 Cloud Triggers + GPIO 12 Active Buzzer

#include <CircuitDigestCloud.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "driver/i2s.h"
#include "audio_data.h" // Holds voice_doorbell, voice_switch1..4
#include "time.h"       // NTP time sync (needed for TLS handshake to MQTT broker)

// ── Select Board Model ───────────────────────────────────────────────────────
#define CAMERA_MODEL_AI_THINKER
#include "camera.h"
// ────────────────────────────────────────────────────────────────────────────

// ── Cloud Credentials ───────────────────────────────────────────────────────
#define WIFI_SSID       "Bad"
#define WIFI_PASS       "123456789"
#define DEVICE_ID       "564843c4-1164-42d6-b1b2-3311a7a3f08c"
#define CONNECTION_KEY "c35d6731ba605cad03a0783982419745"
#define API_KEY        "cd_moh_240626_11WynF"

// ── WhatsApp Alert (Circuit Digest Cloud) ───────────────────────────────────
#define WHATSAPP_PHONE_NUMBER "917539912128"
#define WHATSAPP_TEMPLATE_ID  "image_capture_alert"
#define WHATSAPP_HOST         "www.circuitdigest.cloud"

// Dashboard Keys
#define REMOTE_CAPTURE "capture-1"
#define SLOT_SWITCH_1  "analog-input-1"
#define SLOT_SWITCH_2  "analog-input-2"
#define SLOT_SWITCH_3  "analog-input-3"
#define SLOT_SWITCH_4  "analog-input-4"
// ────────────────────────────────────────────────────────────────────────────

// Pins & Durations
#define DOORBELL_BTN_PIN   13
#define BUZZER_PIN         12
#define DEBOUNCE_MS        50
#define BUZZER_DURATION_MS 3000 // Buzzer sound time (3 seconds)

// Set to 1 if your buzzer module turns ON when the pin goes HIGH.
// Set to 0 if it's wired active-low (turns ON when the pin goes LOW).
#define BUZZER_ACTIVE_HIGH 0
#define BUZZER_ON  (BUZZER_ACTIVE_HIGH ? HIGH : LOW)
#define BUZZER_OFF (BUZZER_ACTIVE_HIGH ? LOW  : HIGH)

#define I2S_LRC  14
#define I2S_BCLK 2
#define I2S_DOUT 15

// I2S1 is used for audio so camera on I2S0 remains untouched
#define I2S_NUM  I2S_NUM_1

CircuitDigestCloud CDcloud;

// State tracking
volatile uint32_t lastEdgeMs = 0;
bool remoteCapture = false;
volatile bool captureBusy = false;

// Non-blocking Buzzer tracking
uint32_t buzzerStartTime = 0;
bool buzzerActive = false;

// Buzzer is scheduled to sound BUZZER_DELAY_MS after the doorbell trigger
#define BUZZER_DELAY_MS 3000
bool buzzerPending = false;
uint32_t buzzerPendingSince = 0;

// Pending Audio Voice Prompt Triggers
volatile int playResponseIndex = 0; 

void IRAM_ATTR onDoorbellButton() {
  lastEdgeMs = millis();
}

// Trigger Active Buzzer
void triggerBuzzer() {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  buzzerStartTime = millis();
  buzzerActive = true;
}

bool startI2SAudio() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = AUDIO_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err = i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("i2s_driver_install failed: %d\n", err);
    return false;
  }
  err = i2s_set_pin(I2S_NUM, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("i2s_set_pin failed: %d\n", err);
    i2s_driver_uninstall(I2S_NUM);
    return false;
  }
  return true;
}

void stopI2SAudio() {
  i2s_driver_uninstall(I2S_NUM);
}

// Send a WhatsApp alert with the captured photo via multipart/form-data
void sendWhatsAppAlertWithImage(const uint8_t* imageBuf, size_t imageLen, const char* eventLabel) {
  if (!imageBuf || imageLen == 0) return;

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(WHATSAPP_HOST, 443)) {
    Serial.println("WhatsApp: connection failed");
    return;
  }

  String boundary = "ESP32CAMBound7MA4YWxkTrZu0gW";
  String crlf = "\r\n";
  String dash = "--";

  // Force "captured_time" directly to "Just now" without NTP formatting
  String variablesJson = String("{\"event_type\":\"") + eventLabel + "\","
                       + "\"location\":\"Front Door\","
                       + "\"device_name\":\"Outdoor Doorbell\","
                       + "\"captured_time\":\"Just now\"}";

  String bodyHead = dash + boundary + crlf +
                    "Content-Disposition: form-data; name=\"phone_number\"" + crlf + crlf +
                    WHATSAPP_PHONE_NUMBER + crlf +
                    dash + boundary + crlf +
                    "Content-Disposition: form-data; name=\"template_id\"" + crlf + crlf +
                    WHATSAPP_TEMPLATE_ID + crlf +
                    dash + boundary + crlf +
                    "Content-Disposition: form-data; name=\"variables\"" + crlf + crlf +
                    variablesJson + crlf +
                    dash + boundary + crlf +
                    "Content-Disposition: form-data; name=\"image\"; filename=\"p.jpg\"" + crlf +
                    "Content-Type: image/jpeg" + crlf + crlf;

  String bodyTail = crlf + dash + boundary + dash + crlf;

  size_t totalContentLength = bodyHead.length() + imageLen + bodyTail.length();

  client.println("POST /api/v1/whatsapp/send-with-image HTTP/1.1");
  client.print("Host: "); client.println(WHATSAPP_HOST);
  client.print("Authorization: "); client.println(API_KEY);
  client.print("Content-Type: multipart/form-data; boundary="); client.println(boundary);
  client.print("Content-Length: "); client.println(totalContentLength);
  client.println();

  client.print(bodyHead);
  client.write(imageBuf, imageLen);
  client.print(bodyTail);

  uint32_t startMs = millis();
  while ((client.connected() || client.available()) && millis() - startMs < 8000) {
    if (client.available()) {
      Serial.println(client.readStringUntil('\n'));
    }
  }
  client.stop();
}

// Cleanly deinit + init the camera sensor
bool resetCamera() {
  esp_camera_deinit();
  delay(50);

  #ifdef PWDN_GPIO_NUM
  if (PWDN_GPIO_NUM != -1) {
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH); // Power down camera sensor
    delay(100);
    digitalWrite(PWDN_GPIO_NUM, LOW);  // Power back up
    delay(100);
  }
  #endif

  return initCamera();
}

// Play specified WAV array buffer over I2S1
void playAudioBuffer(const unsigned char* audio_arr, size_t arr_len) {
  if (arr_len <= 44) return;

  if (!startI2SAudio()) {
    Serial.println("Audio playback aborted (I2S1 init failed).");
    return;
  }

  size_t bytesWritten = 0;
  const uint8_t* audio_data = audio_arr + 44; // Skip WAV header
  size_t audio_len = arr_len - 44;

  esp_err_t werr = i2s_write(I2S_NUM, audio_data, audio_len, &bytesWritten, portMAX_DELAY);
  Serial.printf("i2s_write: err=%d, wrote %u of %u bytes\n", werr, (unsigned)bytesWritten, (unsigned)audio_len);
  delay(100);

  stopI2SAudio();
}

// Cloud Callbacks
void handleCapture(float value) {
  if ((bool)value && !captureBusy) remoteCapture = true;
  CDcloud.publish(REMOTE_CAPTURE, 0.0f);
}

void handleSwitch1(float value) {
  if ((bool)value) { playResponseIndex = 1; CDcloud.publish(SLOT_SWITCH_1, 0.0f); }
}
void handleSwitch2(float value) {
  if ((bool)value) { playResponseIndex = 2; CDcloud.publish(SLOT_SWITCH_2, 0.0f); }
}
void handleSwitch3(float value) {
  if ((bool)value) { playResponseIndex = 3; CDcloud.publish(SLOT_SWITCH_3, 0.0f); }
}
void handleSwitch4(float value) {
  if ((bool)value) { playResponseIndex = 4; CDcloud.publish(SLOT_SWITCH_4, 0.0f); }
}

static void captureAndUpload() {
  if (captureBusy) return;
  captureBusy = true;

  // Schedule buzzer to sound BUZZER_DELAY_MS after this press
  buzzerPending = true;
  buzzerPendingSince = millis();

  // 1. Play Doorbell Audio Prompt
  playAudioBuffer(voice_doorbell, voice_doorbell_len);

  // 2. Full sensor tuning
  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_whitebal(s, 1);       
    s->set_awb_gain(s, 1);       
    s->set_wb_mode(s, 0);        

    s->set_exposure_ctrl(s, 1);  
    s->set_aec2(s, 1);           
    s->set_gain_ctrl(s, 1);      

    s->set_bpc(s, 1);            
    s->set_wpc(s, 1);            
    s->set_raw_gma(s, 1);        
    s->set_lenc(s, 1);           

    s->set_special_effect(s, 0); 
    s->set_saturation(s, 0);     
  }

  // 3. Flush Stale Frame Buffers
  camera_fb_t* dummy_fb = esp_camera_fb_get();
  if (dummy_fb) esp_camera_fb_return(dummy_fb);
  dummy_fb = esp_camera_fb_get();
  if (dummy_fb) esp_camera_fb_return(dummy_fb);

  delay(200);

  // 4. Capture Photo
  Serial.println("Capturing photo...");
  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Capture failed! Doing a full camera reset and retrying...");
    bool recovered = false;

    for (int attempt = 1; attempt <= 2 && !recovered; attempt++) {
      if (resetCamera()) {
        delay(300);
        fb = esp_camera_fb_get();
        if (fb) recovered = true;
      }
      if (!recovered) {
        Serial.printf("Camera reset attempt %d did not recover the sensor.\n", attempt);
        delay(300);
      }
    }

    if (!recovered) {
      Serial.println("Camera unrecoverable via soft reset - restarting device...");
      Serial.flush();
      delay(200);
      ESP.restart();
    }
  }

  if (!fb) {
    Serial.println("Capture failed permanently!");
    captureBusy = false;
    return;
  }

  // 5. Upload to Cloud
  Serial.printf("Captured %u bytes — uploading... (RSSI=%d dBm, free heap=%u, largest block=%u)\n",
                fb->len, WiFi.RSSI(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  bool ok = CDcloud.sendImage(fb->buf, fb->len, "image/jpeg");

  Serial.printf("Upload Result: %s (err=%d)\n", ok ? "SUCCESS" : "FAILED", CDcloud.lastError());

  // 6. Send WhatsApp Notification with Captured Image Attached
  Serial.println("Sending WhatsApp alert with image...");
  sendWhatsAppAlertWithImage(fb->buf, fb->len, "Doorbell Pressed");

  esp_camera_fb_return(fb);

  captureBusy = false;
}

// Block until RTC has a valid timestamp for TLS/MQTT handshake
void waitForNTPSync() {
  Serial.print("Syncing time via NTP");
  
  configTime(0, 0, "time.google.com", "pool.ntp.org", "1.1.1.1");

  time_t now = time(nullptr);
  uint32_t startMs = millis();
  while (now < 8 * 3600 * 2 && millis() - startMs < 15000) {
    delay(250);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();

  if (now < 8 * 3600 * 2) {
    Serial.println("WARNING: NTP sync failed (no internet reply within 15s).");
  } else {
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    Serial.printf("Time synced: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }
}

void setup() {
  Serial.begin(115200);

  // Setup Push Button
  pinMode(DOORBELL_BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(DOORBELL_BTN_PIN), onDoorbellButton, FALLING);

  // Setup Active Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);

  delay(300);

  bool camOk = initCamera();
  for (int attempt = 1; !camOk && attempt <= 3; attempt++) {
    Serial.printf("Camera init failed, retrying (%d/3)...\n", attempt);
    delay(500);
    camOk = initCamera();
  }
  if (!camOk) {
    Serial.println("Camera init failed after retries - restarting device...");
    Serial.flush();
    delay(200);
    ESP.restart();
  }
  Serial.println("Camera initialized successfully.");

  WiFi.mode(WIFI_STA);

  // Connect to Wi-Fi & Cloud
  if (!CDcloud.begin(WIFI_SSID, WIFI_PASS, DEVICE_ID, CONNECTION_KEY, API_KEY)) {
    Serial.println("Cloud connection failed");
    while (true) delay(1000);
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);
  }

  waitForNTPSync();

  // Subscriptions bound to analog-input keys
  CDcloud.subscribe(REMOTE_CAPTURE, handleCapture);
  CDcloud.subscribe(SLOT_SWITCH_1, handleSwitch1);
  CDcloud.subscribe(SLOT_SWITCH_2, handleSwitch2);
  CDcloud.subscribe(SLOT_SWITCH_3, handleSwitch3);
  CDcloud.subscribe(SLOT_SWITCH_4, handleSwitch4);

  Serial.println("Doorbell Ready!");
}

void loop() {
  // Handle Wi-Fi Disconnections and Force Periodic Reconnects
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastReconnectAttempt = 0;
    uint32_t currentMs = millis();

    if (currentMs - lastReconnectAttempt >= 5000) {
      lastReconnectAttempt = currentMs;
      Serial.println("Wi-Fi link lost. Triggering active reconnect...");
      WiFi.disconnect();
      WiFi.reconnect();
    }

    delay(100);
    return;
  }

  // Normal Cloud Execution when connected
  CDcloud.loop();

  // Fire the buzzer once its scheduled delay has elapsed
  if (buzzerPending && (millis() - buzzerPendingSince >= BUZZER_DELAY_MS)) {
    buzzerPending = false;
    triggerBuzzer();
  }

  // Non-blocking timer: Auto turn off buzzer after duration
  if (buzzerActive && (millis() - buzzerStartTime >= BUZZER_DURATION_MS)) {
    digitalWrite(BUZZER_PIN, BUZZER_OFF);
    buzzerActive = false;
  }

  // Physical Button Handler
  if (lastEdgeMs && (millis() - lastEdgeMs >= DEBOUNCE_MS)) {
    lastEdgeMs = 0;
    if (digitalRead(DOORBELL_BTN_PIN) == LOW) {
      Serial.println("Doorbell Button Pressed!");
      captureAndUpload();
    }
  }

  // Dashboard Capture Command Handler
  if (remoteCapture) {
    remoteCapture = false;
    captureAndUpload();
  }

  // Quick Response Voice Triggers from Dashboard
  if (playResponseIndex > 0) {
    int choice = playResponseIndex;
    playResponseIndex = 0;

    switch (choice) {
      case 1:
        Serial.println("Triggered analog-input-1: Playing 'Leave package...'");
        playAudioBuffer(voice_switch1, voice_switch1_len);
        break;
      case 2:
        Serial.println("Triggered analog-input-2: Playing 'Check back later...'");
        playAudioBuffer(voice_switch2, voice_switch2_len);
        break;
      case 3:
        Serial.println("Triggered analog-input-3: Playing 'Be right out...'");
        playAudioBuffer(voice_switch3, voice_switch3_len);
        break;
      case 4:
        Serial.println("Triggered analog-input-4: Playing 'Call on mobile...'");
        playAudioBuffer(voice_switch4, voice_switch4_len);
        break;
    }
  }
}