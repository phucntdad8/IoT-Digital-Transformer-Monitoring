#include <HardwareSerial.h>
#include <PZEM004Tv30.h>
#include <SD.h>
#include <SPI.h>

// ====================== CẤU HÌNH ======================
String tb_token = "STN07LefRPt2LpU1lbdM";
String tb_server = "mqtt.thingsboard.cloud";

unsigned long lastActionTime = 0;
const long actionInterval = 2000;        // ← Đọc + Gửi mỗi 2 giây
bool isMQTTConnected = false;
bool isNetworkReady = false;
bool sdCardReady = false;

// ====================== SD CARD ======================
#define SD_CS_PIN 5
const char* logFileName = "/offline.log";

// ====================== PZEM ======================
#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
#define NUM_PZEMS 3
HardwareSerial pzem_uart(1);
PZEM004Tv30 pzems[NUM_PZEMS];

// ====================== SIM ======================
HardwareSerial sim(2);

// ====================== PROTOTYPE ======================
String sendAT(String cmd, int timeout = 5000);
String getSIMTime();
bool initSD();
void appendToSD(String data);
void uploadBacklog();
void checkNetworkAndReconnect();
void publishMQTT(String payload);
bool waitForPromptAndSend(String data, int timeout);

// ====================== HÀM AT ======================
String sendAT(String cmd, int timeout) {
  sim.println(cmd);
  String resp = "";
  long t = millis();
  while (millis() - t < timeout) {
    if (sim.available()) resp += (char)sim.read();
  }
  Serial.print("-> "); Serial.println(cmd);
  Serial.print("<- "); Serial.println(resp);
  return resp;
}

String getSIMTime() {
  String resp = sendAT("AT+CCLK?", 2000);
  int start = resp.indexOf("\"");
  int end = resp.lastIndexOf("\"");
  if (start != -1 && end > start) return resp.substring(start + 1, end);
  return "NoTime";
}

// ====================== SD CARD ======================
bool initSD() {
  Serial.println("🔄 Đang khởi tạo SD Card...");
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("❌ SD Card KHÔNG HOẠT ĐỘNG!");
    sdCardReady = false;
    return false;
  }
  Serial.println("✅ SD Card HOẠT ĐỘNG tốt!");
  sdCardReady = true;
  Serial.printf("📊 Dung lượng SD: %d MB\n", SD.cardSize() / (1024 * 1024));
  return true;
}

void appendToSD(String data) {
  if (!sdCardReady) return;
  File logFile = SD.open(logFileName, FILE_APPEND);
  if (logFile) {
    String ts = getSIMTime();
    logFile.println("[" + ts + "] " + data);
    logFile.close();
    Serial.println("✅ ĐÃ LƯU VÀO SD CARD [" + ts + "]");
  }
}

void uploadBacklog() {
  if (!sdCardReady || !SD.exists(logFileName)) return;
  Serial.println("\n📤 Đang đẩy backlog từ SD...");
  File logFile = SD.open(logFileName, FILE_READ);
  while (logFile.available()) {
    String line = logFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int pos = line.indexOf('{');
    if (pos != -1) {
      publishMQTT(line.substring(pos));
      delay(1000);
    }
  }
  logFile.close();
  SD.remove(logFileName);
  Serial.println("✅ Đẩy backlog xong.");
}

// ====================== RECONNECT ======================
void checkNetworkAndReconnect() {
  String creg = sendAT("AT+CREG?", 1500);
  isNetworkReady = (creg.indexOf("0,1") != -1 || creg.indexOf("0,5") != -1);

  if (!isNetworkReady) {
    isMQTTConnected = false;
    return;
  }

  if (!isMQTTConnected) {
    Serial.println("\n🔄 Reconnecting MQTT...");
    sendAT("AT+CMQTTSTART", 5000);
    sendAT("AT+CMQTTACCQ=0,\"ESP32_A7680C_3PZEM\"", 3000);
    String cmd = "AT+CMQTTCONNECT=0,\"tcp://" + tb_server + ":1883\",60,1,\"" + tb_token + "\"";
    String res = sendAT(cmd, 25000);
    if (res.indexOf("+CMQTTCONNECT: 0,0") != -1) {
      isMQTTConnected = true;
      Serial.println("✅ MQTT Connected!");
      uploadBacklog();
    }
  }
}

// ====================== PUBLISH ======================
bool waitForPromptAndSend(String data, int timeout) {
  long t = millis();
  while (millis() - t < timeout) {
    if (sim.available()) {
      char c = sim.read();
      Serial.write(c);
      if (c == '>') {
        delay(300);
        sim.print(data);
        delay(400);
        return true;
      }
    }
  }
  return false;
}

void publishMQTT(String payload) {
  String topic = "v1/devices/me/telemetry";
  Serial.println("[MQTT] 📤 Đang gửi...");

  while (sim.available()) sim.read();

  sim.print("AT+CMQTTTOPIC=0,"); sim.println(topic.length());
  delay(200);
  waitForPromptAndSend(topic, 8000);

  sim.print("AT+CMQTTPAYLOAD=0,"); sim.println(payload.length());
  delay(200);
  waitForPromptAndSend(payload, 8000);

  sim.println("AT+CMQTTPUB=0,1,60");
  delay(1200);

  String res = "";
  long t = millis();
  while (millis() - t < 5000 && sim.available()) res += (char)sim.read();

  if (res.indexOf("+CMQTTPUB: 0,0") != -1) {
    Serial.println("✅✅ GỬI THÀNH CÔNG LÊN THINGSBOARD ✅✅");
  } else {
    Serial.println("❌ Publish failed");
    isMQTTConnected = false;
  }
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== 3 PZEM + A7680C + SD BACKUP (2s/lần) ===");

  pzem_uart.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);
  sim.begin(115200, SERIAL_8N1, 26, 27);

  for (int i = 0; i < NUM_PZEMS; i++) {
    pzems[i] = PZEM004Tv30(pzem_uart, PZEM_RX_PIN, PZEM_TX_PIN, 0x01 + i);
  }

  initSD();

  sendAT("AT", 2000);
  sendAT("ATE0", 2000);
  sendAT("AT+CMEE=2", 2000);
  sendAT("AT+CFUN=1", 4000);
  sendAT("AT+CGDCONT=1,\"IP\",\"v-internet\"", 2000);
  sendAT("AT+CGACT=1,1", 8000);

  delay(5000);
  checkNetworkAndReconnect();
}

// ====================== LOOP ======================
void loop() {
  if (millis() - lastActionTime >= actionInterval) {

    Serial.println("\n=================== ĐỌC DỮ LIỆU PZEM (2s) ===================");
    
    String jsonPayload = "{";
    bool hasError = false;

    for (int i = 0; i < NUM_PZEMS; i++) {
      float v = pzems[i].voltage();
      float curr = pzems[i].current();
      float p = pzems[i].power();
      float e_kwh = pzems[i].energy();
      float f = pzems[i].frequency();
      float pf = pzems[i].pf();
      float e_wh = e_kwh * 1000.0;

      Serial.printf("PZEM%d → V=%.1fV | I=%.2fA | P=%.0fW | PF=%.2f | E=%.0fWh | F=%.1fHz\n",
                    i + 1, v, curr, p, pf, e_wh, f);

      if (isnan(v) || isnan(curr) || isnan(p) || v < 50) hasError = true;

      if (i > 0) jsonPayload += ",";
      String pz = String(i + 1);
      jsonPayload += "\"voltage" + pz + "\":" + String(v, 1) +
                     ",\"current" + pz + "\":" + String(curr, 2) +
                     ",\"activePower" + pz + "\":" + String(p, 0) +
                     ",\"powerFactor" + pz + "\":" + String(pf, 2) +
                     ",\"energy" + pz + "\":" + String(e_wh, 0) +
                     ",\"frequency" + pz + "\":" + String(f, 1);

      delay(180);
    }
    jsonPayload += "}";

    Serial.println("JSON: " + jsonPayload);

    // Kiểm tra kết nối và gửi
    checkNetworkAndReconnect();

    if (isMQTTConnected && !hasError) {
      publishMQTT(jsonPayload);
    } else {
      Serial.println("🌐 Mất kết nối → Lưu vào SD Card");
      appendToSD(jsonPayload);
    }

    lastActionTime = millis();
  }

  // Debug AT command
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) sim.println(cmd);
  }

  delay(50);
}
