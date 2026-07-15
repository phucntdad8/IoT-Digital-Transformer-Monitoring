// ======================
// TEST ESP32 CÒN SỐNG
// ======================

void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT);        // LED built-in trên hầu hết ESP32 DevKit
  
  Serial.println("\n=====================");
  Serial.println("ESP32 ĐÃ KHỞI ĐỘNG!");
  Serial.println("=====================");
}

void loop() {
  // Nháy LED
  digitalWrite(2, HIGH);
  Serial.println("ESP32 alive - LED ON");
  delay(5000);
  
  digitalWrite(2, LOW);
  Serial.println("ESP32 alive - LED OFF");
  delay(5000);
}