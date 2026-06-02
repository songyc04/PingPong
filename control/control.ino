#include <WiFi.h>

// Wi-Fi 및 서버 환경 설정
const char* ssid     = "Metabus2.4";     
const char* password = "12345678"; 
const char* serverIP = "192.168.0.188";        // PC의 IPv4 주소 입력
const uint16_t port  = 10001;                // 포트 번호 변경

WiFiClient client;

void setup() {
  Serial.begin(115200);
  delay(10);

  Serial.println();
  Serial.print("Wi-Fi 연결 중: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi 연결 완료!");
  Serial.print("ESP32 IP 주소: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (!client.connected()) {
    Serial.println("서버 연결 시도 중...");
    if (!client.connect(serverIP, port)) {
      Serial.println("연결 실패. 5초 후 재시도합니다.");
      delay(5000);
      return;
    }
    Serial.println("서버 연결 성공!");
  }

  // 1. 5초마다 "TCP/IP" 메시지 송신
  Serial.println("[송신] ESP32 -> PC: TCP/IP");
  client.print("TCP/IP\n"); 

  // 2. PC로부터 응답 대기 (최대 1초 타임아웃)
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 1000) {
      Serial.println(">>> 수신 타임아웃 (응답 없음)");
      return;
    }
  }

  // 3. PC의 응답 데이터 읽기
  String response = client.readStringUntil('\n');
  response.trim(); 
  Serial.print("[수신] PC -> ESP32: ");
  Serial.println(response);

  // 4. 다음 송신까지 5초 대기
  delay(5000);
}