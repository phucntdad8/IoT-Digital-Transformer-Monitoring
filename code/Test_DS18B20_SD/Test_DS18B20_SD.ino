#include <OneWire.h>
#include <DallasTemperature.h>
#include <SD.h>
#include <SPI.h>

// ================== PIN ==================
#define ONE_WIRE_BUS  4    // DS18B20
#define SD_CS         5    // SD Card

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

File dataFile;
const char* filename = "/nhietdo.csv";

unsigned long lastTime = 0;
const unsigned long interval = 5000;  // Lấy mẫu mỗi 5 giây

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n=== ESP32 - 3 DS18B20 + SD Card ===");

  // Khởi tạo DS18B20
  sensors.begin();
  Serial.printf("→ Tìm thấy %d cảm biến DS18B20\n", sensors.getDeviceCount());

  // Khởi tạo SD Card
  if (!SD.begin(SD_CS)) {
    Serial.println("❌ Lỗi khởi tạo SD Card! Kiểm tra dây và thẻ SD.");
    while (1);        // Dừng nếu SD lỗi
  }
  Serial.println("✅ SD Card OK");

  createCSVHeader();
  Serial.println("=== Hệ thống sẵn sàng ghi dữ liệu ===\n");
}

void loop() {
  if (millis() - lastTime >= interval) {
    lastTime = millis();

    sensors.requestTemperatures();
    
    float t1 = sensors.getTempCByIndex(0);
    float t2 = sensors.getTempCByIndex(1);
    float t3 = sensors.getTempCByIndex(2);

    // Xử lý lỗi cảm biến
    if (t1 == DEVICE_DISCONNECTED_C) t1 = -999.0;
    if (t2 == DEVICE_DISCONNECTED_C) t2 = -999.0;
    if (t3 == DEVICE_DISCONNECTED_C) t3 = -999.0;

    // Hiển thị Serial
    Serial.printf("Nhiệt độ: %.2f°C  |  %.2f°C  |  %.2f°C\n", t1, t2, t3);

    // Ghi vào SD Card
    dataFile = SD.open(filename, FILE_APPEND);
    if (dataFile) {
      dataFile.printf("%.2f,%.2f,%.2f\n", t1, t2, t3);
      dataFile.close();
      Serial.println("💾 Đã lưu vào SD Card");
    } else {
      Serial.println("❌ Lỗi ghi SD Card");
    }
  }
}

// ================== TẠO HEADER CSV ==================
void createCSVHeader() {
  if (!SD.exists(filename)) {
    dataFile = SD.open(filename, FILE_WRITE);
    if (dataFile) {
      dataFile.println("Thoi_gian,Varap1_C,Varap2_C,Varap3_C");
      dataFile.close();
      Serial.println("✅ Đã tạo file nhietdo.csv với tiêu đề");
    }
  }
}