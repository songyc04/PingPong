#include <WiFi.h>
#include <WiFiUdp.h> 
#include <Keypad.h>
#include <Ticker.h> 

// Wi-Fi 및 서버 환경 설정
const char* ssid     = "fusion";     
const char* password = "12345678"; 
const char* serverIP = "192.168.0.122"; 
const uint16_t port  = 10001;           

// [Player 1] 조이스틱 핀 설정
const int p1X = 34; const int p1Y = 35; const int p1SW = 32; 

// [Player 2] 조이스틱 핀 설정 (VP, VN 복원)
const int p2X = 36; // VP
const int p2Y = 39; // VN
const int p2SW = 33; 

WiFiClient client; // TCP (명령 제어용)
WiFiUDP udp;       // UDP (데이터 전송용)

String state = "END"; // 초기 상태 END로 시작

// 키패드 설정
const byte ROWS = 4; 
const byte COLS = 4; 
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'}, 
  {'4','5','6','B'}, 
  {'7','8','9','C'}, 
  {'*','0','#','D'}  
};
byte rowPins[ROWS] = {18, 5, 17, 16}; 
byte colPins[COLS] = {23, 22, 21, 19}; 

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

Ticker keypadTimer;
volatile char pressedKey = 0; 

unsigned long lastConnectAttempt = 0;
const unsigned long connectInterval = 5000; 

void IRAM_ATTR onKeypadScan() {
  char key = customKeypad.getKey();
  if (key) {
    pressedKey = key; 
  }
}

void setup() {
  Serial.begin(115200);
  delay(10);

  analogSetAttenuation(ADC_11db); // 4095 방지 설정

  pinMode(p1SW, INPUT_PULLUP);
  pinMode(p2SW, INPUT_PULLUP);

  Serial.println();
  Serial.print("Wi-Fi 연결 중: ");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi 연결 완료!");

  udp.begin(port);
  keypadTimer.attach(0.03, onKeypadScan);
}

void loop() {
  unsigned long currentMillis = millis();

  if (!client.connected()) {
    if (currentMillis - lastConnectAttempt >= connectInterval) {
      lastConnectAttempt = currentMillis; 
      Serial.println("서버 연결 시도 중...");
      if (client.connect(serverIP, port)) {
        Serial.println("서버 연결 성공!");
        state = "END"; 
      }
    }
  }

  // [단계 1] 키패드 입력 처리
  if (pressedKey != 0) {
    char key = pressedKey;
    pressedKey = 0; 
    
    String command = "";
    switch (key) {
      case 'A': command = "SRT"; break;
      case 'B': command = "STP"; break;
      case 'C': command = "SET"; break;
      case '#': command = "END"; break;
    }

    if (command != "" && client.connected()) {
      client.print(command + "\n"); 
    }
  }

  // [단계 2] PC 확인 신호 수신 후 상태 변경
  if (client.connected() && client.available() > 0) {
    String response = client.readStringUntil('\n');
    response.trim();

    if (response == "SRT")       state = "SRT";
    else if (response == "SET")  state = "SET";
    else if (response == "STP" || response == "END") state = "END";
  }

  if (state == "END") {
    delay(10); 
    return; 
  }

  // 조이스틱 데이터 읽기 (10번 평균)
  long sumP1X = 0, sumP1Y = 0;
  long sumP2X = 0, sumP2Y = 0;
  for (int i = 0; i < 10; i++) {
    sumP1X += analogRead(p1X); sumP1Y += analogRead(p1Y);
    sumP2X += analogRead(p2X); sumP2Y += analogRead(p2Y);
  }
  int avgP1X = sumP1X / 10; int avgP1Y = sumP1Y / 10;
  int avgP2X = sumP2X / 10; int avgP2Y = sumP2Y / 10;

  String sendData = "";

  // 1. 게임플레이 모드 (SRT) 포맷
  if (state == "SRT") {
    sendData = String(avgP1X) + "," + String(avgP1Y) + "," +
               String(avgP2X) + "," + String(avgP2Y);
  }
  // 2. ★ 설정모드 (SET) 포맷 수정 (up, dn을 위한 Y축 추가 및 clk 결합)
  else if (state == "SET") {
    String p1Clk = (digitalRead(p1SW) == LOW) ? "O" : "";
    String p2Clk = (digitalRead(p2SW) == LOW) ? "O" : "";
    
    // 포맷 예시 -> P1Y:2048,CLK:O|P2Y:2011,CLK:
    sendData = "P1Y:" + String(avgP1Y) + ",CLK:" + p1Clk + "|" +
               "P2Y:" + String(avgP2Y) + ",CLK:" + p2Clk;
  }

  // UDP로 PC 전송
  udp.beginPacket(serverIP, port); 
  udp.println(sendData);           
  udp.endPacket();                 

  Serial.print("[UDP 송신-" + state + "] ");
  Serial.println(sendData);

  delay(20);
}