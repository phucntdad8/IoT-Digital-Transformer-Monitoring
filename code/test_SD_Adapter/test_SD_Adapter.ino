#include "FS.h"
#include "SD.h"
#include "SPI.h"

// Định nghĩa chân kết nối (có thể thay đổi CS nếu cần)
#define SD_CS   5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK  18

// Khởi tạo SPI (nếu muốn chỉ định rõ)
SPIClass spi = SPIClass(VSPI);   // VSPI là bus mặc định cho các chân trên

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Khởi động ESP32 + microSD Card");

  // Khởi tạo SPI với các chân tùy chỉnh
  spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  // Khởi tạo thẻ SD
  if (!SD.begin(SD_CS, spi)) {
    Serial.println("Lỗi: Không thể khởi tạo thẻ SD!");
    Serial.println("Kiểm tra lại kết nối và thẻ SD.");
    return;
  }
  Serial.println("Thẻ SD đã khởi tạo thành công.");

  // Tạo file test
  writeFile("/test.txt", "Xin chào! ESP32 đang ghi dữ liệu vào microSD.\n");
  
  // Đọc file test
  readFile("/test.txt");

  // Thêm nội dung vào file
  appendFile("/test.txt", "Dòng này được thêm sau.\n");

  // Liệt kê các file trong thư mục gốc
  listDir("/", 0);
}

void loop() {
  // Có thể thêm code ghi dữ liệu theo thời gian thực ở đây
  delay(10000);
}

// ====================== Các hàm hỗ trợ ======================

void listDir(const char * dirname, uint8_t levels) {
  Serial.printf("Liệt kê thư mục: %s\n", dirname);
  File root = SD.open(dirname);
  if (!root) {
    Serial.println("Không mở được thư mục");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Đây không phải là thư mục");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void readFile(const char * path) {
  Serial.printf("Đọc file: %s\n", path);
  File file = SD.open(path);
  if (!file) {
    Serial.println("Không mở được file để đọc");
    return;
  }
  Serial.println("Nội dung file:");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(const char * path, const char * message) {
  Serial.printf("Ghi file: %s\n", path);
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Không mở được file để ghi");
    return;
  }
  if (file.print(message)) {
    Serial.println("Ghi thành công");
  } else {
    Serial.println("Ghi thất bại");
  }
  file.close();
}

void appendFile(const char * path, const char * message) {
  Serial.printf("Thêm nội dung vào file: %s\n", path);
  File file = SD.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Không mở được file để thêm");
    return;
  }
  if (file.print(message)) {
    Serial.println("Thêm thành công");
  } else {
    Serial.println("Thêm thất bại");
  }
  file.close();
}
