#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <UniversalTelegramBot.h>

#include <Wire.h>
#include <MPU6050.h>

#include "MAX30105.h"
#include "heartRate.h"

// ==================== CONFIG ====================
const char* ssid     = "AJAX_NEXT";
const char* password = "24575961";

#define BOTtoken  "8730390836:AAGUlz7ofIw5LtSZvNMswgcQoaqjZRwC8G4"
#define CHAT_ID   "-1003740017692"

#define WIT_TOKEN "7Y2GYMNK7Q5KU5R7F27AQG7VSKRK5FLD"

#define MIC_PIN    32
#define BUTTON_PIN  4
#define BUZZER_PIN 13

#define SAMPLE_RATE    8000
#define RECORD_SECONDS 2
#define SAMPLE_COUNT   (SAMPLE_RATE * RECORD_SECONDS)

// ==================== ОБ'ЄКТИ ====================
WiFiClientSecure secureClient;
UniversalTelegramBot bot(BOTtoken, secureClient);

MPU6050 mpu;
MAX30105 particleSensor;

// ==================== MUTEX ДЛЯ I2C ====================
SemaphoreHandle_t i2cMutex;

// ==================== СЕРЦЕВИЙ РИТМ ====================
const byte RATE_SIZE = 4;
volatile byte  rates[4]       = {0, 0, 0, 0};
volatile byte  rateSpot       = 0;
volatile long  lastBeat       = 0;
volatile float beatsPerMinute = 0;
volatile int   beatAvg        = 0;
volatile long  irValueGlobal  = 0;

// ==================== MPU глобально ====================
volatile int16_t gAx = 0, gAy = 0, gAz = 0;

// ==================== ТАЙМЕРИ ====================
unsigned long lastWitCheck   = 0;
unsigned long lastSOSTime    = 0;
unsigned long lastPulseDebug = 0;
unsigned long lastBotCheck   = 0;

// ==================== АУДІО БУФЕР ====================
int16_t audioBuffer[SAMPLE_COUNT];

// ==================== ЗУМЕР ====================
void buzzerTone(int freq, int duration) {
  tone(BUZZER_PIN, freq, duration);
  delay(duration + 20);
}

void playWifiConnected() {
  buzzerTone(523, 100);
  buzzerTone(659, 100);
  buzzerTone(784, 100);
  buzzerTone(1047, 200);
  delay(50);
  buzzerTone(1047, 100);
  buzzerTone(1175, 300);
}

void playSOSBeep() {
  for (int i = 0; i < 3; i++) { buzzerTone(1000, 100); delay(50); }
  delay(150);
  for (int i = 0; i < 3; i++) { buzzerTone(1000, 300); delay(100); }
  delay(150);
  for (int i = 0; i < 3; i++) { buzzerTone(1000, 100); delay(50); }
}

// ==================== ПОЗИЦІЯ ====================
String getPosition(int16_t ax, int16_t ay, int16_t az) {
  float gx = ax / 16384.0;
  float gy = ay / 16384.0;
  float gz = az / 16384.0;

  float absX = abs(gx);
  float absY = abs(gy);
  float absZ = abs(gz);

  if (absZ > absX && absZ > absY) {
    if (gz > 0) return "горизонтально (лежить на спині) 🛏";
    else        return "горизонтально (лежить на животі) 🛏";
  } else if (absY > absX) {
    if (gy > 0) return "вертикально (стоїть/сидить) 🧍";
    else        return "вертикально (догори ногами) 🙃";
  } else {
    if (gx > 0) return "нахилено вправо ↗";
    else        return "нахилено вліво ↙";
  }
}

// ==================== HEALTH REPORT ====================
String buildHealthReport() {
  String position = getPosition(gAx, gAy, gAz);

  String msg = "📋 *HEALTH REPORT*\n\n";
  msg += "💓 Пульс: " + String(beatAvg) + " BPM\n";
  msg += "📡 IR сигнал: " + String(irValueGlobal) + "\n";
  msg += "🧍 Позиція: " + position + "\n";

  if (beatAvg > 120) msg += "⚠️ ВИСОКИЙ ПУЛЬС!\n";
  if (beatAvg == 0)  msg += "👆 Пульс не виявлено\n";
  if (irValueGlobal < 50000) msg += "👆 Палець не виявлено\n";

  return msg;
}

// ==================== SOS ====================
void triggerSOS(const char* source) {
  unsigned long now = millis();
  if (now - lastSOSTime < 10000) {
    Serial.println("SOS cooldown, ignoring.");
    return;
  }
  lastSOSTime = now;

  Serial.print(">>> SOS triggered by: ");
  Serial.println(source);

  playSOSBeep();

  String msg = "🆘 *SOS СИГНАЛ!*\n\n";
  msg += "Джерело: ";
  msg += source;
  msg += "\n";
  msg += "💓 Пульс: " + String(beatAvg) + " BPM\n";

  bot.sendMessage(CHAT_ID, msg, "Markdown");
  delay(500);
  bot.sendMessage(CHAT_ID, msg, "Markdown");
  delay(500);
  bot.sendMessage(CHAT_ID, msg, "Markdown");
}

// ==================== ОБРОБКА КОМАНД БОТА ====================
void handleBotMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text    = bot.messages[i].text;
    String from    = bot.messages[i].from_name;

    Serial.println("Bot msg from " + from + ": " + text);

    if (text == "/status") {
      bot.sendMessage(chat_id, buildHealthReport(), "Markdown");
    }
    else if (text == "/pulse") {
      String msg = "💓 Пульс: " + String(beatAvg) + " BPM\n";
      msg += "📡 IR: " + String(irValueGlobal) + "\n";
      msg += irValueGlobal > 50000 ? "👆 Палець є ✓" : "👆 Палець не виявлено";
      bot.sendMessage(chat_id, msg, "");
    }
    else if (text == "/pos") {
      String msg = "🧍 Позиція: " + getPosition(gAx, gAy, gAz);
      bot.sendMessage(chat_id, msg, "");
    }
    else if (text == "/sos") {
      triggerSOS("команда в боті 📱");
    }
    else if (text == "/help" || text == "/start") {
      String msg = "🤖 *Доступні команди:*\n\n";
      msg += "/status — повний звіт\n";
      msg += "/pulse — пульс\n";
      msg += "/pos — позиція\n";
      msg += "/sos — тестовий SOS\n";
      bot.sendMessage(chat_id, msg, "Markdown");
    }
    else {
      bot.sendMessage(chat_id, "❓ Невідома команда. Напиши /help", "");
    }
  }
}

// ==================== WAV ====================
void buildWavHeader(uint8_t* header, uint32_t dataSize) {
  uint32_t fileSize = dataSize + 36;
  uint32_t byteRate = SAMPLE_RATE * 1 * 2;

  header[0]='R'; header[1]='I'; header[2]='F'; header[3]='F';
  header[4]=(fileSize)&0xFF;     header[5]=(fileSize>>8)&0xFF;
  header[6]=(fileSize>>16)&0xFF; header[7]=(fileSize>>24)&0xFF;
  header[8]='W'; header[9]='A'; header[10]='V'; header[11]='E';
  header[12]='f'; header[13]='m'; header[14]='t'; header[15]=' ';
  header[16]=16; header[17]=0; header[18]=0; header[19]=0;
  header[20]=1;  header[21]=0;
  header[22]=1;  header[23]=0;
  header[24]=(SAMPLE_RATE)&0xFF;     header[25]=(SAMPLE_RATE>>8)&0xFF;
  header[26]=(SAMPLE_RATE>>16)&0xFF; header[27]=(SAMPLE_RATE>>24)&0xFF;
  header[28]=(byteRate)&0xFF;     header[29]=(byteRate>>8)&0xFF;
  header[30]=(byteRate>>16)&0xFF; header[31]=(byteRate>>24)&0xFF;
  header[32]=2; header[33]=0;
  header[34]=16; header[35]=0;
  header[36]='d'; header[37]='a'; header[38]='t'; header[39]='a';
  header[40]=(dataSize)&0xFF;     header[41]=(dataSize>>8)&0xFF;
  header[42]=(dataSize>>16)&0xFF; header[43]=(dataSize>>24)&0xFF;
}

void recordAudio() {
  Serial.println("Recording...");
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    int raw = analogRead(MIC_PIN);
    audioBuffer[i] = (int16_t)((raw - 2048) * 16);
    delayMicroseconds(1000000 / SAMPLE_RATE);
  }
  Serial.println("Recording done.");
}

// ==================== WIT.AI ====================
String sendToWitAI() {
  uint32_t dataSize  = SAMPLE_COUNT * 2;
  uint32_t totalSize = 44 + dataSize;

  uint8_t wavHeader[44];
  buildWavHeader(wavHeader, dataSize);

  uint8_t* wavBuf = (uint8_t*)malloc(totalSize);
  if (!wavBuf) { Serial.println("malloc failed!"); return ""; }

  memcpy(wavBuf, wavHeader, 44);
  memcpy(wavBuf + 44, (uint8_t*)audioBuffer, dataSize);

  HTTPClient http;
  http.begin("https://api.wit.ai/speech?v=20250101");
  http.addHeader("Authorization", "Bearer " WIT_TOKEN);
  http.addHeader("Content-Type",  "audio/wav");
  http.addHeader("Accept",        "application/json");
  http.setTimeout(10000);

  int httpCode = http.POST(wavBuf, totalSize);
  free(wavBuf);

  String intent = "";

  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("Wit.ai: " + payload);

    String lower = payload;
    lower.toLowerCase();

    if (lower.indexOf("help")      != -1 ||
        lower.indexOf("sos")       != -1 ||
        lower.indexOf("emergency") != -1) {
      Serial.println(">>> Keyword detected!");
      intent = "help";
    }
  } else {
    Serial.println("Wit.ai error: " + String(httpCode));
    Serial.println(http.getString());
  }

  http.end();
  return intent;
}

void handleIntent(const String& intent) {
  if (intent == "help") triggerSOS("голос 🎤");
}

// ==================== ЗАДАЧА ПУЛЬСУ (ЯДРО 0) ====================
void pulseTask(void* parameter) {
  for (;;) {
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      long ir = particleSensor.getIR();
      xSemaphoreGive(i2cMutex);

      irValueGlobal = ir;

      if (ir > 50000) {
        if (checkForBeat(ir)) {
          long delta = millis() - lastBeat;
          lastBeat = millis();
          float bpm = 60.0 / (delta / 1000.0);

          if (bpm > 20 && bpm < 255) {
            rates[rateSpot++] = (byte)bpm;
            rateSpot %= RATE_SIZE;
            beatAvg = 0;
            for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
            beatAvg /= RATE_SIZE;
            beatsPerMinute = bpm;
            Serial.print("Beat! AVG BPM: ");
            Serial.println(beatAvg);
          }
        }
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  i2cMutex = xSemaphoreCreateMutex();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    tone(BUZZER_PIN, 500, 100);
  }
  Serial.println("\nWiFi OK!");
  playWifiConnected();

  secureClient.setInsecure();
  bot.sendMessage(CHAT_ID, "ESP32 Online ✅\n\nНапиши /help для списку команд", "");

  mpu.initialize();
  if (mpu.testConnection()) {
    Serial.println("MPU6050 OK");
    bot.sendMessage(CHAT_ID, "MPU6050 OK ✅", "");
  } else {
    Serial.println("MPU6050 ERROR");
    bot.sendMessage(CHAT_ID, "MPU6050 ERROR ❌", "");
  }

  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 ERROR");
    bot.sendMessage(CHAT_ID, "MAX30102 ERROR ❌", "");
  } else {
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x1F);
    particleSensor.setPulseAmplitudeIR(0x1F);
    Serial.println("MAX30102 OK");
    bot.sendMessage(CHAT_ID, "MAX30102 OK ✅", "");
  }

  xTaskCreatePinnedToCore(
    pulseTask,
    "PulseTask",
    2048,
    NULL,
    1,
    NULL,
    0
  );

  Serial.println("=== Ready. Core 0: pulse | Core 1: WiFi/Bot/Wit.ai ===");
}

// ==================== LOOP (ЯДРО 1) ====================
void loop() {
  // ===== КНОПКА SOS =====
  if (digitalRead(BUTTON_PIN) == LOW) {
    triggerSOS("кнопка 🔴");
    while (digitalRead(BUTTON_PIN) == LOW) delay(50);
  }

  // ===== MPU6050 з mutex =====
  int16_t ax, ay, az, gx, gy, gz;
  if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    xSemaphoreGive(i2cMutex);

    gAx = ax; gAy = ay; gAz = az;

    float totalG = sqrt((float)ax*ax + (float)ay*ay + (float)az*az) / 16384.0;
    if (totalG > 2.5) {
      Serial.println(">>> FALL DETECTED! G = " + String(totalG));
      triggerSOS("падіння 📉");
    }
  }

  // ===== ПУЛЬС ДЕБАГ кожну секунду =====
  if (millis() - lastPulseDebug > 1000) {
    lastPulseDebug = millis();
    Serial.println("------ PULSE ------");
    Serial.print("IR:      "); Serial.println(irValueGlobal);
    Serial.print("BPM:     "); Serial.println(beatsPerMinute);
    Serial.print("AVG BPM: "); Serial.println(beatAvg);
    Serial.print("Finger:  "); Serial.println(irValueGlobal > 50000 ? "YES ✓" : "NO — прикладіть палець!");
    Serial.print("Позиція: "); Serial.println(getPosition(gAx, gAy, gAz));
    Serial.println("-------------------");
  }

  // ===== ПЕРЕВІРКА БОТА кожні 2 секунди =====
  if (millis() - lastBotCheck > 2000) {
    lastBotCheck = millis();
    handleBotMessages();
  }

  // ===== WIT.AI кожні 5 секунд =====
  if (millis() - lastWitCheck > 5000) {
    lastWitCheck = millis();
    recordAudio();
    String intent = sendToWitAI();
    handleIntent(intent);
  }

  delay(20);
}