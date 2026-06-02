#include <WiFi.h>
#include <Keypad.h>

// 1. Wi-Fi 및 서버 환경 설정
const char* ssid     = "fusion";     
const char* password = "12345678"; 
const char* serverIP = "192.168.0.114"; // PC의 IPv4 주소
const uint16_t port  = 10001;          // 파이썬 서버 포트 번호

WiFiClient client;

// 2. 키패드 설정 (정방향 구조)
const byte ROWS = 4; 
const byte COLS = 4; 

char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'}, // 'A' -> SRT
  {'4','5','6','B'}, // 'B' -> STP
  {'7','8','9','C'}, // 'C' -> SET
  {'*','0','#','D'}  // 'D' -> CUS | '#' -> END
};

// ESP32 전용 안전한 GPIO 핀 배정
// 기존 코드에서 행(row)과 열(col)의 핀 설정을 서로 바꿉니다!
byte rowPins[ROWS] = {18, 5, 17, 16}; // 기존 colPins 숫자를 여기로!
byte colPins[COLS] = {23, 22, 21, 19}; // 기존 rowPins 숫자를 여기로!

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

// 무멈춤 타이머를 위한 변수 추가
unsigned long lastConnectAttempt = 0;
const unsigned long connectInterval = 5000; // 재연결 시도 간격 (5초)

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
  // 현재 시간 체크
  unsigned long currentMillis = millis();

  // 3. [개선됨] delay() 없이 5초마다 한 번씩만 서버 연결을 시도하는 로직
  if (!client.connected()) {
    if (currentMillis - lastConnectAttempt >= connectInterval) {
      lastConnectAttempt = currentMillis; // 타이머 리셋
      
      Serial.println("서버 연결 시도 중...");
      if (client.connect(serverIP, port)) {
        Serial.println("서버 연결 성공!");
      } else {
        Serial.println("연결 실패. (보드는 멈추지 않고 키패드를 계속 감시합니다.)");
      }
    }
  }

  // 4. 상시 키패드 입력 감시 (이제 서버 연결 여부와 상관없이 무조건 초고속 실행됨)
  char key = customKeypad.getKey();
  
  if (key) {
    Serial.print("키 입력 확인: ");
    Serial.println(key);
    
    String command = "";
    switch (key) {
      case 'A': command = "SRT"; break;
      case 'B': command = "STP"; break;
      case 'C': command = "SET"; break;
      case 'D': command = "CUS"; break;
      case '#': command = "END"; break; 
    }

    // 서버가 실제로 연결되어 있을 때만 전송
    if (command != "" && client.connected()) {
      Serial.print("[송신] ESP32 -> PC: ");
      Serial.println(command);
      client.print(command + "\n"); 
    } else if (command != "" && !client.connected()) {
      Serial.println("[경고] 키는 눌렸으나 서버가 연결되어 있지 않아 전송할 수 없습니다.");
    }
  }

  // 5. PC로부터 들어오는 데이터 읽기
  if (client.connected() && client.available() > 0) {
    String response = client.readStringUntil('\n');
    response.trim();
    Serial.print("[수신] PC -> ESP32: ");
    Serial.println(response);
  }
}