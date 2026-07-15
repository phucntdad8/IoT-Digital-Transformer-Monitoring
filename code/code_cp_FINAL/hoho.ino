#include <OneWire.h>
#include <DallasTemperature.h>
#include <HardwareSerial.h>
#include <PZEM004Tv30.h>
#include <SD.h>
#include <SPI.h>
#include "BluetoothSerial.h"

// ====================== CẤU HÌNH ======================
String tb_token = "STN07LefRPt2LpU1lbdM";
String tb_server = "mqtt.thingsboard.cloud";

unsigned long lastActionTime = 0;
const long actionInterval = 5000;  // Tăng lên 5 giây để ổn định PZEM

bool isMQTTConnected = false;
bool isNetworkReady = false;
bool sdCardReady = false;

// --- MỚI THÊM --- Ngưỡng dòng điện test giả lập (Ampe). Vượt qua mức này mới tính là Quá Tải.
#define I_RATED_TEST 0.06  

// ====================== THUẬT TOÁN DỰ BÁO NHIỆT ĐỘ ======================
const int BUFFER_SIZE = 5;
float tempBuffer1[BUFFER_SIZE];
float tempBuffer2[BUFFER_SIZE];
float tempBuffer3[BUFFER_SIZE];
unsigned long timeBuffer[BUFFER_SIZE];
int sampleCount = 0;
unsigned long lastSampleTime = 0;
const long sampleInterval = 10000;

// ====================== BLUETOOTH ======================
BluetoothSerial SerialBT;
const char* btDeviceName = "ESP32_3PZEM_BT";

// ====================== PIN ======================
#define SD_CS_PIN 5
#define ONE_WIRE_BUS 4
#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17

// ====================== CẢM BIẾN ======================
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
HardwareSerial pzem_uart(1);
#define NUM_PZEMS 3
PZEM004Tv30 pzems[NUM_PZEMS];
HardwareSerial sim(2);
const char* logFileName = "/offline.log";

// ====================== PROTOTYPE ======================
String sendAT(String cmd, int timeout = 5000);
String getVietnamTime();
bool initSD();
void appendToSD(String data);
void uploadBacklog();
void checkNetworkAndReconnect();
void publishMQTT(String payload);
bool waitForPromptAndSend(String data, int timeout);
void printToAll(const char* format, ...);
void checkSDStatus();
void checkBluetoothStatus();
float predictValue(float *y, unsigned long *x, int n);

// ====================== HÀM TÍNH TOÁN DỰ BÁO ======================
float predictValue(float *y, unsigned long *x, int n) {
  float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
  unsigned long t0 = x[0]; 
  
  for(int i = 0; i < n; i++) {
    float normX = (float)(x[i] - t0) / 1000.0;
    sumX += normX;
    sumY += y[i];
    sumXY += normX * y[i];
    sumX2 += normX * normX;
  }
  
  float denominator = (n * sumX2 - sumX * sumX);
  if (denominator == 0) return y[n-1]; 
  
  float slope = (n * sumXY - sumX * sumY) / denominator;
  float intercept = (sumY - slope * sumX) / n;
  
  // Đã sửa thành 300.0 (5 phút)
  float futureX = (float)(millis() - t0) / 1000.0 + 300.0; 
  return (slope * futureX) + intercept;
}

// ====================== HÀM IN RA SERIAL + BLUETOOTH ======================
void printToAll(const char* format, ...) {
  char buf[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  
  Serial.print(buf);
  if (SerialBT.hasClient()) {
    SerialBT.print(buf);
    delay(5);
  }
}

// ====================== TRẠNG THÁI ======================
void checkBluetoothStatus() {
  static bool lastState = false;
  bool current = SerialBT.hasClient();
  if (current != lastState) {
    printToAll(current ? "📱 BLUETOOTH: ĐÃ KẾT NỐI\n" : "📱 BLUETOOTH: Chưa có thiết bị kết nối\n");
    lastState = current;
  }
}

void checkSDStatus() {
  if (!sdCardReady) {
    printToAll("🟥 SD Card: Chưa khởi tạo hoặc LỖI\n");
    return;
  }
  if (SD.begin(SD_CS_PIN)) {
    printToAll("🟢 SD Card: Sẵn sàng\n");
  } else {
    printToAll("🟥 SD Card: ĐÃ RÚT THẺ hoặc mất kết nối!\n");
    sdCardReady = false;
  }
}

// ====================== AT COMMAND ======================
String sendAT(String cmd, int timeout) {
  sim.println(cmd);
  String resp = "";
  long t = millis();
  while (millis() - t < timeout) {
    if (sim.available()) resp += (char)sim.read();
  }
  return resp;
}

String getVietnamTime() {
  String resp = sendAT("AT+CCLK?", 2000);
  int start = resp.indexOf("\"");
  int end = resp.lastIndexOf("\"");
  if (start != -1 && end > start) return resp.substring(start + 1, end);
  return "NoTime";
}

// ====================== SD CARD ======================
bool initSD() {
  printToAll("🔄 Khởi tạo SD Card...\n");
  if (!SD.begin(SD_CS_PIN)) {
    printToAll("❌ SD Card KHÔNG HOẠT ĐỘNG!\n");
    sdCardReady = false;
    return false;
  }
  sdCardReady = true;
  printToAll("✅ SD Card HOẠT ĐỘNG! - Dung lượng: %d MB\n", SD.cardSize() / (1024 * 1024));
  return true;
}

void appendToSD(String data) {
  if (!sdCardReady) return;
  if (!SD.begin(SD_CS_PIN)) {
    sdCardReady = false;
    return;
  }
  File logFile = SD.open(logFileName, FILE_APPEND);
  if (logFile) {
    String ts = getVietnamTime();
    logFile.println("[" + ts + "] " + data);
    logFile.close();
    printToAll("💾 ĐÃ LƯU DỮ LIỆU MỚI VÀO SD CARD\n");
  }
}

void uploadBacklog() {
  if (!sdCardReady || !SD.exists(logFileName)) return;
  printToAll("\n🔄 === BẮT ĐẦU ĐẨY DỮ LIỆU CŨ TỪ SD CARD ===\n");
  File logFile = SD.open(logFileName, FILE_READ);
  int count = 0;
  while (logFile.available()) {
    String line = logFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int pos = line.indexOf('{');
    if (pos != -1) {
      publishMQTT(line.substring(pos));
      count++;
      delay(1200);
    }
  }
  logFile.close();
  if (count > 0) {
    SD.remove(logFileName);
    printToAll("✅ HOÀN TẤT! ĐÃ ĐẨY %d bản ghi DỮ LIỆU CŨ lên ThingsBoard\n", count);
  }
}

// ====================== NETWORK & MQTT ======================
void checkNetworkAndReconnect() {
  String creg = sendAT("AT+CREG?", 1500);
  isNetworkReady = (creg.indexOf("0,1") != -1 || creg.indexOf("0,5") != -1);
  if (!isNetworkReady) {
    isMQTTConnected = false;
    return;
  }
  if (!isMQTTConnected) {
    printToAll("\n🔄 Đang kết nối MQTT...\n");
    sendAT("AT+CMQTTSTART", 5000);
    sendAT("AT+CMQTTACCQ=0,\"ESP32_3PZEM\"", 3000);
    String cmd = "AT+CMQTTCONNECT=0,\"tcp://" + tb_server + ":1883\",90,1,\"" + tb_token + "\",\"" + tb_token + "\"";
    String res = sendAT(cmd, 35000);
    if (res.indexOf("+CMQTTCONNECT: 0,0") != -1) {
      isMQTTConnected = true;
      printToAll("✅ MQTT Connected thành công!\n");
      uploadBacklog();
    }
  }
}

// ====================== PUBLISH MQTT ======================
bool waitForPromptAndSend(String data, int timeout) {
  long t = millis();
  while (millis() - t < timeout) {
    if (sim.available()) {
      if (sim.read() == '>') {
        delay(300); sim.print(data); delay(400);
        return true;
      }
    }
  }
  return false;
}

void publishMQTT(String payload) {
  printToAll("[MQTT] 📤 Đang gửi lên ThingsBoard...\n");
  String topic = "v1/devices/me/telemetry";
  sim.print("AT+CMQTTTOPIC=0,"); sim.println(topic.length());
  delay(200); waitForPromptAndSend(topic, 8000);
  sim.print("AT+CMQTTPAYLOAD=0,"); sim.println(payload.length());
  delay(200); waitForPromptAndSend(payload, 8000);
  sim.println("AT+CMQTTPUB=0,1,60");
  delay(1500);
  String res = "";
  long t = millis();
  while (millis() - t < 5000 && sim.available()) res += (char)sim.read();
  if (res.indexOf("+CMQTTPUB: 0,0") != -1) printToAll("✅ GỬI THÀNH CÔNG LÊN THINGSBOARD\n");
  else { printToAll("❌ Publish thất bại\n"); isMQTTConnected = false; }
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 - 3 PZEM + 3 DS18B20 + DỰ BÁO NHIỆT 3 NHÁNH ===\n");

  SerialBT.begin(btDeviceName);
  printToAll("✅ Bluetooth Started! Tên: %s\n", btDeviceName);

  sensors.begin();
  printToAll("→ Tìm thấy %d DS18B20\n", sensors.getDeviceCount());

  pzem_uart.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);
  for (int i = 0; i < NUM_PZEMS; i++) {
    pzems[i] = PZEM004Tv30(pzem_uart, PZEM_RX_PIN, PZEM_TX_PIN, 0x01 + i);
  }

  sim.begin(115200, SERIAL_8N1, 26, 27);
  initSD();

  sendAT("AT", 2000); sendAT("ATE0", 2000); sendAT("AT+CMEE=2", 2000);
  sendAT("AT+CFUN=1", 4000); sendAT("AT+CGDCONT=1,\"IP\",\"v-internet\"", 2000);
  sendAT("AT+CGACT=1,1", 8000);

  delay(5000);
  checkNetworkAndReconnect();
}

// ====================== LOOP ======================
void loop() {
  if (millis() - lastActionTime >= actionInterval) {
    lastActionTime = millis();
    
    // Đọc cảm biến nhiệt độ
    sensors.requestTemperatures();
    float t1 = sensors.getTempCByIndex(0);
    float t2 = sensors.getTempCByIndex(1);
    float t3 = sensors.getTempCByIndex(2);
    if (t1 == DEVICE_DISCONNECTED_C) t1 = 0.0;
    if (t2 == DEVICE_DISCONNECTED_C) t2 = 0.0;
    if (t3 == DEVICE_DISCONNECTED_C) t3 = 0.0;

    String currentTime = getVietnamTime();
    printToAll("\n=================== DỮ LIỆU MỚI (Realtime) ===================\n");
    checkBluetoothStatus();
    checkSDStatus();

    String jsonPayload = "{";
    bool hasAnyPZEMError = false;

    // --- MỚI THÊM --- Mảng lưu trữ dòng điện của 3 nhánh để kiểm tra quá tải
    float safe_currents[NUM_PZEMS] = {0.0, 0.0, 0.0}; 

    // --- ĐỌC VÀ IN DỮ LIỆU 3 PZEM ---
    for (int i = 0; i < NUM_PZEMS; i++) {
      float v = pzems[i].voltage();
      float curr = pzems[i].current();
      float p = pzems[i].power();
      float e_kwh = pzems[i].energy();
      float f = pzems[i].frequency();
      float pf = pzems[i].pf();
      float e_wh = e_kwh * 1000.0;

      float safe_curr = isnan(curr) ? 0.0 : curr;
      
      // --- MỚI THÊM --- Lưu lại dòng điện vào mảng để xài ở dưới
      safe_currents[i] = safe_curr;

      // Kiểm tra lỗi
      if (isnan(v) || isnan(curr) || isnan(p) || v < 50) {
        hasAnyPZEMError = true;
        printToAll("⚠️ PZEM%d có lỗi (NaN hoặc giá trị bất thường)\n", i + 1);
      }

      printToAll("PZEM%d → V=%.1fV | I=%.2fA | P=%.0fW | F=%.1fHz\n", i + 1, v, safe_curr, p, f);
      
      if (i > 0) jsonPayload += ",";
      String pz = String(i + 1);
      jsonPayload += "\"voltage" + pz + "\":" + String(v, 1) +
                     ",\"current" + pz + "\":" + String(safe_curr, 2) +
                     ",\"activePower" + pz + "\":" + String(p, 0) +
                     ",\"powerFactor" + pz + "\":" + String(pf, 2) +
                     ",\"energy" + pz + "\":" + String(e_wh, 0) +
                     ",\"frequency" + pz + "\":" + String(f, 1);

      delay(200);  // ← Delay quan trọng để PZEM ổn định
    }

    printToAll("🌡️ Nhiệt độ THỰC TẾ: T1=%.2f | T2=%.2f | T3=%.2f °C\n", t1, t2, t3);

    // --- LOGIC LƯU MẪU NHIỆT ĐỘ (giữ nguyên) ---
    if (millis() - lastSampleTime >= sampleInterval) {
      lastSampleTime = millis();
      if (sampleCount < BUFFER_SIZE) {
        tempBuffer1[sampleCount] = t1; tempBuffer2[sampleCount] = t2; tempBuffer3[sampleCount] = t3;
        timeBuffer[sampleCount] = millis(); sampleCount++;
      } else {
        for (int i = 0; i < BUFFER_SIZE - 1; i++) {
          tempBuffer1[i] = tempBuffer1[i + 1]; tempBuffer2[i] = tempBuffer2[i + 1];
          tempBuffer3[i] = tempBuffer3[i + 1]; timeBuffer[i] = timeBuffer[i + 1];
        }
        tempBuffer1[BUFFER_SIZE - 1] = t1; tempBuffer2[BUFFER_SIZE - 1] = t2;
        tempBuffer3[BUFFER_SIZE - 1] = t3; timeBuffer[BUFFER_SIZE - 1] = millis();
      }
    }
  // --- TÍNH TOÁN DỰ BÁO ---
    float pred1 = t1, pred2 = t2, pred3 = t3;
    if (sampleCount == BUFFER_SIZE) {
      pred1 = predictValue(tempBuffer1, timeBuffer, BUFFER_SIZE);
      pred2 = predictValue(tempBuffer2, timeBuffer, BUFFER_SIZE);
      pred3 = predictValue(tempBuffer3, timeBuffer, BUFFER_SIZE);
    }

    // --- ĐÁNH GIÁ LOGIC (CHECK ĐỦ 4 TRƯỜNG HỢP VẬT LÝ) ---
    bool predict_overload_1 = false; String print_status_1 = "";
    bool predict_overload_2 = false; String print_status_2 = "";
    bool predict_overload_3 = false; String print_status_3 = "";

    // NHÁNH 1
    if (pred1 >= 40.0 && safe_currents[0] > I_RATED_TEST) {
        predict_overload_1 = true;  // CHỈ DUY NHẤT TRƯỜNG HỢP NÀY MỚI GỬI TRUE LÊN CLOUD
        print_status_1 = "[TH4: QUÁ TẢI THỰC SỰ - Dòng cao & Nhiệt cao]";
    } else if (pred1 >= 40.0 && safe_currents[0] <= I_RATED_TEST) {
        predict_overload_1 = false;
        print_status_1 = "[TH2: MÔI TRƯỜNG NÓNG - Dòng điện an toàn]";
    } else if (pred1 < 40.0 && safe_currents[0] > I_RATED_TEST) {
        predict_overload_1 = false;
        print_status_1 = "[TH3: QUÁ DÒNG TỨC THỜI - Chờ nhiệt độ tăng...]";
    } else {
        predict_overload_1 = false;
        print_status_1 = "[TH1: BÌNH THƯỜNG - An toàn]";
    }

    // NHÁNH 2
    if (pred2 >= 40.0 && safe_currents[1] > I_RATED_TEST) {
        predict_overload_2 = true;
        print_status_2 = "[TH4: QUÁ TẢI THỰC SỰ - Dòng cao & Nhiệt cao]";
    } else if (pred2 >= 40.0 && safe_currents[1] <= I_RATED_TEST) {
        predict_overload_2 = false;
        print_status_2 = "[TH2: MÔI TRƯỜNG NÓNG - Dòng điện an toàn]";
    } else if (pred2 < 40.0 && safe_currents[1] > I_RATED_TEST) {
        predict_overload_2 = false;
        print_status_2 = "[TH3: QUÁ DÒNG TỨC THỜI - Chờ nhiệt độ tăng...]";
    } else {
        predict_overload_2 = false;
        print_status_2 = "[TH1: BÌNH THƯỜNG - An toàn]";
    }

    // NHÁNH 3
    if (pred3 >= 40.0 && safe_currents[2] > I_RATED_TEST) {
        predict_overload_3 = true;
        print_status_3 = "[TH4: QUÁ TẢI THỰC SỰ - Dòng cao & Nhiệt cao]";
    } else if (pred3 >= 40.0 && safe_currents[2] <= I_RATED_TEST) {
        predict_overload_3 = false;
        print_status_3 = "[TH2: MÔI TRƯỜNG NÓNG - Dòng điện an toàn]";
    } else if (pred3 < 40.0 && safe_currents[2] > I_RATED_TEST) {
        predict_overload_3 = false;
        print_status_3 = "[TH3: QUÁ DÒNG TỨC THỜI - Chờ nhiệt độ tăng...]";
    } else {
        predict_overload_3 = false;
        print_status_3 = "[TH1: BÌNH THƯỜNG - An toàn]";
    }

    // In ra Serial Monitor cực kỳ rõ ràng để quay video/chụp ảnh báo cáo
    printToAll("🔥 DỰ BÁO 5 PHÚT TỚI (Ngưỡng I_rated = %.2fA):\n", I_RATED_TEST);
    printToAll("   Nhánh 1: Nhiệt=%.1f°C | Dòng=%.2fA -> %s\n", pred1, safe_currents[0], print_status_1.c_str());
    printToAll("   Nhánh 2: Nhiệt=%.1f°C | Dòng=%.2fA -> %s\n", pred2, safe_currents[1], print_status_2.c_str());
    printToAll("   Nhánh 3: Nhiệt=%.1f°C | Dòng=%.2fA -> %s\n", pred3, safe_currents[2], print_status_3.c_str());

    // --- ĐÓNG GÓI JSON GỬI LÊN THINGSBOARD ---
    // Chỉ gửi kết quả cuối cùng (predict_overload) lên Cloud, không gửi cờ thừa thãi nào khác
    jsonPayload += ",\"datetime\":\"" + currentTime + "\"" +
                   ",\"temp1\":" + String(t1, 2) +
                   ",\"temp2\":" + String(t2, 2) +
                   ",\"temp3\":" + String(t3, 2) +
                   ",\"pred_t1\":" + String(pred1, 2) +
                   ",\"pred_t2\":" + String(pred2, 2) +
                   ",\"pred_t3\":" + String(pred3, 2) +
                   ",\"predict_overload_1\":" + String(predict_overload_1 ? "true" : "false") +
                   ",\"predict_overload_2\":" + String(predict_overload_2 ? "true" : "false") +
                   ",\"predict_overload_3\":" + String(predict_overload_3 ? "true" : "false") +
                   ",\"has_pzem_error\":" + String(hasAnyPZEMError ? "true" : "false") + "}";

    printToAll("JSON: %s\n", jsonPayload.c_str());

    checkNetworkAndReconnect();

    // === GỬI DỮ LIỆU ===
    if (isMQTTConnected) {
      publishMQTT(jsonPayload);           
    } else {
      printToAll("🌐 OFFLINE → Lưu dữ liệu vào SD Card\n");
      appendToSD(jsonPayload);
    }
  }

  // Debug AT command
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n'); cmd.trim();
    if (cmd.length() > 0) sim.println(cmd);
  }
  if (SerialBT.available()) {
    String cmd = SerialBT.readStringUntil('\n'); cmd.trim();
    if (cmd.length() > 0) sim.println(cmd);
  }
  delay(50);
}