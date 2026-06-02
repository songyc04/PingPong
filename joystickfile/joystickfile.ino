#include <WiFi.h>

// Wi-Fi 및 서버 환경 설정
const char* ssid     = "fusion";     
const char* password = "12345678"; 
const char* serverIP = "192.168.0.114"; // PC의 IPv4 주소
const uint16_t port  = 10001;           // 포트 번호

// 조이스틱 핀 설정 (버튼 제외)
const int joystickX = 34;  // VRx (좌우)
const int joystickY = 35;  // VRy (상하)

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
  // 서버 연결 확인 및 재연결
  if (!client.connected()) {
    Serial.println("서버 연결 시도 중...");
    if (!client.connect(serverIP, port)) {
      Serial.println("연결 실패. 5초 후 재시도합니다.");
      delay(5000);
      return;
    }
    Serial.println("서버 연결 성공!");
  }

  // 1. 조이스틱 값 10번 읽어서 평균 내기 (노이즈 제거)
  long sumX = 0;
  long sumY = 0;
  for (int i = 0; i < 10; i++) {
    sumX += analogRead(joystickX);
    sumY += analogRead(joystickY);
    delay(1); 
  }
  int avgX = sumX / 10;
  int avgY = sumY / 10;

  // 2. 요청하신 포맷으로 데이터 문자열 만들기 -> "X:정수,Y:정수"
  String data = "X:" + String(avgX) + ",Y:" + String(avgY);

  // 3. PC 서버로 데이터 송신 (줄바꿈 \n 포함)
  Serial.print("[송신] ESP32 -> PC: ");
  Serial.println(data);
  client.print(data + "\n"); 

  // 4. PC로부터 응답 대기 (최대 1초 타임아웃)
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 1000) {
      Serial.println(">>> 수신 타임아웃 (응답 없음)");
      delay(500); // 다음 송신 전 잠깐 대기
      return;
    }
  }

  // 5. PC의 응답 데이터 읽기
  String response = client.readStringUntil('\n');
  response.trim(); 
  Serial.print("[수신] PC -> ESP32: ");
  Serial.println(response);

  // 6. 다음 전송까지 대기 (실시간 전송용)
  delay(10);
}