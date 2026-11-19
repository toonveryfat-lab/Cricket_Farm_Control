#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <toon_68056999-project-1_inferencing.h>

// ==========================================
// 1. ตั้งค่า Wi-Fi (ต้องเป็นคลื่น 2.4GHz เท่านั้น)
// ==========================================
const char* WIFI_SSID     = "ใส่ชื่อ_WIFI_ของที่นี่";
const char* WIFI_PASSWORD = "ใส่รหัสผ่าน_WIFI_ที่นี่";

// ==========================================
// 2. ตั้งค่า ThingsBoard Cloud (MQTT)
// ==========================================
const char* TB_SERVER        = "thingsboard.cloud";
const int   TB_PORT          = 1883;
const char* TB_TOKEN         = "hV24gm6aGOuyfOi1yPaM";
const char* TOPIC_TELEMETRY  = "v2/t";
const char* TOPIC_ATTRIBUTES = "v2/a";

// ==========================================
// 3. ตั้งค่าขาพินฮาร์ดแวร์ตาม Schematic
// ==========================================
#define SHT30_ADDR 0x44 // หากไม่เจอเปลี่ยนเป็น 0x45
#define SHT30_SDA  1
#define SHT30_SCL  42

#define RELAY_TEMP 17  // Relay 1: ควบคุมอุณหภูมิ
#define RELAY_HUM  16  // Relay 2: ควบคุมความชื้น
#define RELAY_3    2   // Relay 3: สำรอง
#define RELAY_4    41  // Relay 4: สำรอง

#define LED_R 4
#define LED_G 5
#define LED_B 6

// ค่ามาตรฐานการเลี้ยงจิ้งหรีด (อ้างอิงตารางมาตรฐาน)
const float TEMP_MIN = 28.0;
const float TEMP_MAX = 32.0;
const float HUM_MIN  = 60.0;
const float HUM_MAX  = 75.0;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ตัวแปร Buffer สำหรับส่งข้อมูลเข้า Edge Impulse
float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, features + offset, length * sizeof(float));
    return 0;
}

// ฟังก์ชันควบคุมสี RGB LED (Active LOW)
void setStatusLED(bool r, bool g, bool b) {
    digitalWrite(LED_R, r ? LOW : HIGH);
    digitalWrite(LED_G, g ? LOW : HIGH);
    digitalWrite(LED_B, b ? LOW : HIGH);
}

// ฟังก์ชันอ่านค่าจากเซ็นเซอร์ SHT30 โดยตรง
bool readSHT30(float &temperature, float &humidity) {
    Wire.beginTransmission(SHT30_ADDR);
    Wire.write(0x2C);
    Wire.write(0x06);
    if (Wire.endTransmission() != 0) return false;

    delay(20);

    Wire.requestFrom(SHT30_ADDR, 6);
    if (Wire.available() != 6) return false;

    uint16_t rawTemp = (Wire.read() << 8) | Wire.read();
    Wire.read();
    uint16_t rawHum  = (Wire.read() << 8) | Wire.read();
    Wire.read();

    temperature = -45.0 + 175.0 * ((float)rawTemp / 65535.0);
    humidity    = 100.0 * ((float)rawHum / 65535.0);
    return true;
}

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.print("กำลังเชื่อมต่อ Wi-Fi: ");
    Serial.println(WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nเชื่อมต่อ Wi-Fi สำเร็จ! IP: " + WiFi.localIP().toString());
    } else {
        Serial.println("\nเชื่อมต่อ Wi-Fi ไม่สำเร็จ (ทำงานแบบ Offline ต่อ)");
    }
}

void connectMQTT() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqttClient.connected()) return;

    Serial.print("กำลังเชื่อมต่อ ThingsBoard Cloud...");
    if (mqttClient.connect("ESP32-S3-Cricket", TB_TOKEN, "")) {
        Serial.println(" สำเร็จ!");
        String attrPayload = "{\"device\":\"ESP32-S3-WROOM\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
        mqttClient.publish(TOPIC_ATTRIBUTES, attrPayload.c_str());
    } else {
        Serial.printf(" ล้มเหลว (rc=%d)\n", mqttClient.state());
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    // ตั้งค่าขา Relay และปิดเริ่มต้นทั้งหมด
    pinMode(RELAY_TEMP, OUTPUT);
    pinMode(RELAY_HUM, OUTPUT);
    pinMode(RELAY_3, OUTPUT);
    pinMode(RELAY_4, OUTPUT);
    digitalWrite(RELAY_TEMP, LOW);
    digitalWrite(RELAY_HUM, LOW);
    digitalWrite(RELAY_3, LOW);
    digitalWrite(RELAY_4, LOW);

    // ตั้งค่าขา RGB LED
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(LED_B, OUTPUT);

    // เริ่มต้น I2C และตรวจสอบ SHT30
    Wire.begin(SHT30_SDA, SHT30_SCL);
    float testT, testH;
    if (!readSHT30(testT, testH)) {
        Serial.println("ไม่พบเซ็นเซอร์ SHT30! กรุณาตรวจสอบสายไฟหรือ Address");
        setStatusLED(true, false, false); // สีแดง = Error
        while (1) delay(100);
    }

    // เชื่อมต่อ Wi-Fi และ ThingsBoard
    connectWiFi();
    mqttClient.setServer(TB_SERVER, TB_PORT);
    connectMQTT();

    setStatusLED(false, true, false); // สีเขียว = ระบบพร้อมทำงาน
    Serial.println("ระบบควบคุมตู้เลี้ยงจิ้งหรีด (SHT30 + Edge Impulse + ThingsBoard) พร้อมทำงาน");
}

void loop() {
    // ดูแลการเชื่อมต่อ Wi-Fi และ MQTT
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) connectMQTT();
    mqttClient.loop();

    // 1. เก็บข้อมูลจาก SHT30 ลง Buffer ตามขนาด Window ของโมเดล Edge Impulse
    // (ลำดับ Feature: index คู่ = Temperature, index คี่ = Humidity)
    for (size_t i = 0; i < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE; i += 2) {
        float t = 0.0, h = 0.0;
        if (!readSHT30(t, h) || t < -10.0 || t > 80.0 || h <= 0.0 || h >= 100.0) {
            Serial.println("[Sensor Fault] ค่าเซ็นเซอร์ SHT30 ผิดปกติ! ตัดการทำงานรีเลย์");
            digitalWrite(RELAY_TEMP, LOW);
            digitalWrite(RELAY_HUM, LOW);
            setStatusLED(true, false, false); // สีแดง
            delay(2000);
            return;
        }

        features[i]     = t;
        features[i + 1] = h;

        if (EI_CLASSIFIER_INTERVAL_MS > 0) {
            delay(EI_CLASSIFIER_INTERVAL_MS);
        }
    }

    // ดึงค่าล่าสุดจาก Buffer มาใช้เช็กขอบเขตมาตรฐาน
    float currentTemp = features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - 2];
    float currentHum  = features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - 1];

    // 2. รันการทำนายผลด้วย Edge Impulse
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data = &raw_feature_get_data;

    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

    if (res != EI_IMPULSE_OK) {
        Serial.printf("Edge Impulse Error (%d)\n", res);
        return;
    }

    // หา Label ที่โมเดลทำนายได้ความมั่นใจสูงสุด
    String aiLabel = "";
    float maxScore = 0.0;
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > maxScore) {
            maxScore = result.classification[ix].value;
            aiLabel = String(result.classification[ix].label);
        }
    }

    // 3. ตรวจสอบเงื่อนไขและสั่งงาน Relay 1 และ Relay 2
    // กรณี AI ทำนายว่าเป็น sensor_fault ให้ตัดรีเลย์ทันทีเพื่อความปลอดภัย
    if (aiLabel == "sensor_fault" && maxScore > 0.70) {
        Serial.printf("[AI Sensor Fault] (%.2f) ตัดการทำงานรีเลย์ทั้งหมด!\n", maxScore);
        digitalWrite(RELAY_TEMP, LOW);
        digitalWrite(RELAY_HUM, LOW);
        setStatusLED(true, false, false); // สีแดง
        return;
    }

    // ไฟสีเขียวติดตลอดเวลาที่กำลังทำงานปกติ
    setStatusLED(false, true, false);

    bool tempOutOfRange = (currentTemp < TEMP_MIN || currentTemp > TEMP_MAX);
    digitalWrite(RELAY_TEMP, tempOutOfRange ? HIGH : LOW); // Relay 1: อุณหภูมิ

    bool humOutOfRange = (currentHum < HUM_MIN || currentHum > HUM_MAX);
    digitalWrite(RELAY_HUM, humOutOfRange ? HIGH : LOW);   // Relay 2: ความชื้น

    Serial.printf("Temp: %.2f °C (R1: %s) | Hum: %.2f %%RH (R2: %s) | AI: %s (%.2f)\n",
                  currentTemp, tempOutOfRange ? "ON" : "OFF",
                  currentHum,  humOutOfRange  ? "ON" : "OFF",
                  aiLabel.c_str(), maxScore);

    // 4. ส่งข้อมูลขึ้น ThingsBoard Cloud (ทั้ง Telemetry v2/t และ Attributes v2/a)
    if (mqttClient.connected()) {
        String payload = "{";
        payload += "\"temperature\":" + String(currentTemp, 2) + ",";
        payload += "\"humidity\":" + String(currentHum, 2) + ",";
        payload += "\"relay1_temp\":" + String(tempOutOfRange ? 1 : 0) + ",";
        payload += "\"relay2_hum\":" + String(humOutOfRange ? 1 : 0) + ",";
        payload += "\"ai_label\":\"" + aiLabel + "\",";
        payload += "\"ai_confidence\":" + String(maxScore, 2);
        payload += "}";

        mqttClient.publish(TOPIC_TELEMETRY, payload.c_str());
        mqttClient.publish(TOPIC_ATTRIBUTES, payload.c_str());
    }

    // หากโมเดลใช้ Window Size = 1 (ไม่หน่วงเวลาในลูป for) ให้หน่วงเวลา 2 วินาทีก่อนรอบถัดไป
    if (EI_CLASSIFIER_RAW_SAMPLE_COUNT <= 1) {
        delay(2000);
    }
}