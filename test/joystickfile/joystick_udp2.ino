#include <WiFi.h>
#include <WiFiUdp.h> // ★ UDP 통신을 위한 라이브러리 추가

// Wi-Fi 및 서버 환경 설정
const char* ssid     = "fusion";     
const char* password = "12345678"; 
const char* serverIP = "192.168.0.122"; // PC의 IPv4 주소
const uint16_t port  = 10001;           // PC의 UDP 포트 번호

// [Player 1] 조이스틱 핀 설정
const int p1X = 34; const int p1Y = 35; const int p1SW = 32; 

// [Player 2] 조이스틱 핀 설정
const int p2X = 36; const int p2Y = 39; const int p2SW = 33; 

// ★ UDP 객체 생성 (기존 WiFiClient client; 제거)
WiFiUDP udp;

// 시작하자마자 쏠 수 있도록 SRT 상태로 시작
String state = "SRT"; 

void setup() {
  Serial.begin(115200);
  delay(10);

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

  // ★ UDP 로컬 포트 열기 (명령 수신용)
  udp.begin(port);
  Serial.println("UDP 리스너 시작 완료");
}

void loop() {
  // Wi-Fi 끊김 체크 및 자동 재연결
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi 끊김. 재연결 시도 중...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }
    Serial.println("Wi-Fi 재연결 성공!");
    state = "SRT";
  }

  // ==========================================
  // [단계 1] PC(서버)로부터 UDP 상태 변경 명령 수신 (SRT / SET / END)
  // ==========================================
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    // UDP 패킷 읽기
    char packetBuffer[255];
    int len = udp.read(packetBuffer, 255);
    if (len > 0) {
      packetBuffer[len] = 0; // 문자열 끝 처리
    }
    
    String cmd = String(packetBuffer);
    cmd.trim(); 

    Serial.print("[UDP 수신] 상태 변경 명령: ");
    Serial.println(cmd);

    if (cmd == "SRT") {
      state = "SRT";
    }
    else if (cmd == "SET") {
      state = "SET";
    }
    else if (cmd == "STP" || cmd == "END") {
      state = "END";
    }
  }

  // 기본 상태(END)일 때는 데이터를 송신하지 않고 루프를 넘김 (통신 차단)
  if (state == "END") {
    delay(50); 
    return; 
  }

  // ==========================================
  // [단계 2] 조이스틱 데이터 읽기 및 노이즈 제거 (10번 평균)
  // ==========================================
  long sumP1X = 0, sumP1Y = 0;
  long sumP2X = 0, sumP2Y = 0;
  for (int i = 0; i < 10; i++) {
    sumP1X += analogRead(p1X); sumP1Y += analogRead(p1Y);
    sumP2X += analogRead(p2X); sumP2Y += analogRead(p2Y);
  }
  int avgP1X = sumP1X / 10; int avgP1Y = sumP1Y / 10;
  int avgP2X = sumP2X / 10; int avgP2Y = sumP2Y / 10;

  // ==========================================
  // [단계 3] 상태(state)에 맞는 통신값 생성 및 UDP 전송
  // ==========================================
  String sendData = "";

  // --- 1. 게임플레이 모드 (SRT) 일 때 전송 포맷 (정수,정수,정수,정수) ---
  if (state == "SRT") {
    sendData = String(avgP1X) + "," + String(avgP1Y) + "," +
               String(avgP2X) + "," + String(avgP2Y);
  }
  // --- 2. 설정창 모드 (SET) 일 때 전송 포맷 ---
  else if (state == "SET") {
    String p1Clk = (digitalRead(p1SW) == LOW) ? "O" : "";
    String p2Clk = (digitalRead(p2SW) == LOW) ? "O" : "";
    sendData = "P1CLK:" + p1Clk + "|P2CLK:" + p2Clk;
  }

  // ★ [UDP 패킷 전송 로직] 
  udp.beginPacket(serverIP, port); // 목적지 IP와 포트 지정
  udp.println(sendData);           // 데이터 밀어넣기
  udp.endPacket();                 // 최종 전송 (기다림 없이 즉시 발사)

  // 시리얼 모니터 확인용
  Serial.print("[UDP 송신-" + state + "] ");
  Serial.println(sendData);

  // UDP는 렉이 전혀 없으므로 20ms(0.02초) 주기로 쏴도 완벽하게 받아냅니다.
  delay(20);
}