/*
 * ╔══════════════════════════════════════════════════════╗
 * ESP32 핀볼 [P1 보드] - UDP 양방향 제어 (컴파일 에러 수정 버전)
 * (고정 IP: 192.168.0.207 / Python PC: 192.168.0.138)
 * ╚══════════════════════════════════════════════════════╝
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ────────────────────────────────────────────
//  네트워크 및 고정 IP 설정
// ────────────────────────────────────────────
const char* ssid     = "fusion";
const char* password = "12345678";

IPAddress local_IP(192, 168, 0, 200);   // P1 고정 IP
IPAddress gateway(192, 168, 0, 1);     
IPAddress subnet(255, 255, 255, 0);    

IPAddress serverIP(192, 168, 0, 138);   // 파이썬 PC IP

const int SEND_PORT    = 10001;         // P1 조이스틱/상태 송신 포트
const int RECEIVE_PORT = 10003;         // P1 LCD/상태 수신 포트

WiFiUDP udp;
char incomingPacket[255];

// ────────────────────────────────────────────
//  하드웨어 핀 설정
// ────────────────────────────────────────────
const int VRX = 32;
const int VRY = 35;
const int SW  = 25;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ────────────────────────────────────────────
//  조이스틱 임계값 및 타이밍 규칙
// ────────────────────────────────────────────
#define JOY_MID       2048   
#define JOY_THRESH     700   
#define BTN_DEBOUNCE    60   
#define SEND_INTERVAL_MS 30   // 30ms 주기 실시간 전송

// ────────────────────────────────────────────
//  상태값 정의
// ────────────────────────────────────────────
enum GameState {
  STATE_END,  // 메인메뉴 (END)
  STATE_SET,  // 설정창 (SET)
  STATE_SRT   // 게임 중 (SRT)
};
GameState gState = STATE_END; 

bool btnLastState  = HIGH;
unsigned long btnLastTime   = 0;
unsigned long tLastSend = 0;

// ════════════════════════════════════════════
//  LCD 출력 유틸 함수
// ════════════════════════════════════════════
void lcdRow(int row, const char* text) {
  lcd.setCursor(0, row);
  char padded[17];
  snprintf(padded, 17, "%-16s", text);
  lcd.print(padded);
}

// ════════════════════════════════════════════
//  UDP 패킷 송신 유틸 함수 (★에러 수정 완료★)
// ════════════════════════════════════════════
void sendUDP(String msg) {
  udp.beginPacket(serverIP, SEND_PORT);
  udp.print(msg);
  udp.endPacket();
  
  // 좌표 데이터가 아닐 때만 시리얼 모니터에 로그를 출력합니다.
  if (msg.indexOf(':') == -1) {
    String logMsg = msg;
    logMsg.trim(); // 수정을 먼저 거친 후
    Serial.print("[송신] "); 
    Serial.println(logMsg); // 출력 처리를 분리하여 에러 해결
  }
}

// ════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  lcdRow(0, "PINBALL P1 START");
  lcdRow(1, "Connecting...");

  pinMode(SW, INPUT_PULLUP);
  analogReadResolution(12); 

  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);

  Serial.print("WiFi 연결 중: ");
  Serial.println(ssid);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi 연결 완료");
  Serial.print("ESP32 IP: "); Serial.println(WiFi.localIP());

  udp.begin(RECEIVE_PORT);
  Serial.print("UDP 수신 포트 오픈: "); Serial.println(RECEIVE_PORT);

  lcdRow(0, "PINBALL P1 Ready");
  lcdRow(1, "192.168.0.207");
}

// ════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════
void loop() {
  // 1. 파이썬으로부터 UDP 데이터 수신 (Port 10003)
  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(incomingPacket, sizeof(incomingPacket) - 1);
    if (len > 0) {
      incomingPacket[len] = '\0';
    }

    String response = String(incomingPacket);
    response.trim();

    Serial.print("[수신] "); Serial.println(response);

    if (response == "END") {
      gState = STATE_END;
    } 
    else if (response == "SET") {
      gState = STATE_SET;
    } 
    else if (response == "SRT") {
      gState = STATE_SRT;
    }
    else if (response.length() > 3 && response[0] == 'R' && response[2] == ':') {
      int row = response[1] - '0';
      if (row == 0 || row == 1) {
        lcdRow(row, response.substring(3).c_str());
      }
    }
  }

  // 2. 조이스틱 및 버튼 입력 처리 후 송신 (Port 10001)
  int x = analogRead(VRX);
  int y = analogRead(VRY);
  bool swState = (digitalRead(SW) == LOW);
  unsigned long now = millis();

  // 규칙 A: P1 CLK(버튼) 누르면 파이썬에 SET 전송
  if (swState && !btnLastState) {
    if (now - btnLastTime > BTN_DEBOUNCE) {
      if (gState == STATE_END || gState == STATE_SRT) {
        sendUDP("SET\n");
      }
      btnLastTime = now;
    }
  }
  btnLastState = swState;

  // 규칙 B: 메인메뉴(END) 상태에서 조이스틱 UP 인식 시 파이썬에 SRT 전송
  if (gState == STATE_END) {
    bool isUp = (y < JOY_MID - JOY_THRESH);
    static bool lastUpState = false;
    
    if (isUp && !lastUpState) {
      sendUDP("SRT\n");
    }
    lastUpState = isUp;
  } 
  // 규칙 C: SRT(게임중) 및 SET(설정창) 상태일 때만 30ms 주기로 좌표 실시간 전송
  else if (gState == STATE_SRT || gState == STATE_SET) {
    if (now - tLastSend >= SEND_INTERVAL_MS) {
      String msg = String(x) + ":" + String(y) + "\n";
      sendUDP(msg);
      tLastSend = now;
    }
  }

  delay(1); 
}