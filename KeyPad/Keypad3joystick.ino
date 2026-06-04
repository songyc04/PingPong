/*
 * ESP32 통합 코드 — 키패드 + 조이스틱 P1 (전체 UDP)
 *
 * [키패드 매핑]
 *  A → SRT (게임 시작)
 *  B → STP (일시 정지)
 *  C → SET (설정 모드 진입)
 *  D → END (종료)
 *  # → END (종료)
 *
 * [상태 머신]
 *  SRT: 게임플레이 — P1 조이스틱 XY 좌표를 UDP로 전송
 *  SET: 설정 모드  — P1 조이스틱 UP/DN/CLK 이벤트를 UDP로 전송
 *  END: 정지       — UDP 데이터 전송 중단
 *
 * [SET 모드 이벤트]
 *  Y < 1000  → "UP"
 *  Y > 3000  → "DN"
 *  버튼 클릭 → "CLK"
 *  상태 변화 시 1회만 전송 (연속 전송 방지)
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Keypad.h>
#include <Ticker.h>

// ─────────────────────────────────────────
//  네트워크 설정
// ─────────────────────────────────────────
const char*    ssid     = "fusion";
const char*    password = "12345678";
const char*    serverIP = "192.168.0.102";  // PC의 IPv4 주소
const uint16_t UDP_PORT = 10001;

// ─────────────────────────────────────────
//  키패드 설정 (4×4)
// ─────────────────────────────────────────
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

// ─────────────────────────────────────────
//  조이스틱 P1 핀 설정
// ─────────────────────────────────────────
const int P1_X  = 34;
const int P1_Y  = 35;
const int P1_SW = 32;

// ─────────────────────────────────────────
//  SET 모드 임계값 (ADC 0~4095, 중앙 ~2048)
// ─────────────────────────────────────────
const int JOY_UP_THRESHOLD = 1000;
const int JOY_DN_THRESHOLD = 3000;

// ─────────────────────────────────────────
//  통신 객체
// ─────────────────────────────────────────
WiFiUDP udp;

// ─────────────────────────────────────────
//  전역 변수
// ─────────────────────────────────────────
Ticker        keypadTimer;
volatile char pressedKey = 0;      // 인터럽트에서 채워지는 키값
String        state      = "SRT";  // 현재 상태 (SRT / SET / END)

// SET 모드 이전 상태 (연속 전송 방지용)
// 0=중립, 1=UP, -1=DN
int  prevDir = 0;
bool prevBtn = false;

// ─────────────────────────────────────────
//  인터럽트 핸들러 — 키패드 스캔 (30ms마다)
// ─────────────────────────────────────────
void IRAM_ATTR onKeypadScan() {
  char key = customKeypad.getKey();
  if (key) {
    pressedKey = key;
  }
}

// ─────────────────────────────────────────
//  UDP 전송 헬퍼
// ─────────────────────────────────────────
void udpSend(const String& msg) {
  udp.beginPacket(serverIP, UDP_PORT);
  udp.print(msg + "\n");
  udp.endPacket();
}

// ─────────────────────────────────────────
//  Wi-Fi 연결 (블로킹)
// ─────────────────────────────────────────
void connectWiFi() {
  Serial.print("Wi-Fi 연결 중: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi 연결 완료!");
  Serial.print("IP 주소: ");
  Serial.println(WiFi.localIP());
}

// ─────────────────────────────────────────
//  setup
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(10);

  pinMode(P1_SW, INPUT_PULLUP);

  connectWiFi();

  udp.begin(UDP_PORT);
  Serial.println("UDP 소켓 시작 완료.");

  keypadTimer.attach(0.03, onKeypadScan);
  Serial.println("키패드 타이머 인터럽트 가동.");
}

// ─────────────────────────────────────────
//  loop
// ─────────────────────────────────────────
void loop() {

  // ── 1. Wi-Fi 끊김 감지 및 자동 재연결 ──────────────────
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[경고] Wi-Fi 끊김. 재연결 시도...");
    connectWiFi();
    udp.begin(UDP_PORT);
    state = "SRT";
  }

  // ── 2. UDP 상태 변경 명령 수신 (PC → ESP32) ───────────
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char buf[64] = {0};
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len > 0) buf[len] = '\0';

    String cmd = String(buf);
    cmd.trim();
    Serial.print("[UDP 수신] 상태 명령: ");
    Serial.println(cmd);

    if      (cmd == "SRT") state = "SRT";
    else if (cmd == "SET") state = "SET";
    else if (cmd == "STP" || cmd == "END") state = "END";
  }

  // ── 3. 키패드 인터럽트 처리 → UDP 명령 전송 ──────────
  if (pressedKey != 0) {
    char key = pressedKey;
    pressedKey = 0;

    Serial.print("[키패드] 입력 감지: ");
    Serial.println(key);

    String command = "";
    switch (key) {
      case 'A': command = "SRT"; break;
      case 'B': command = "STP"; break;
      case 'C': command = "SET"; break;
      case 'D': command = "END"; break;
      case '#': command = "END"; break;
    }

    if (command != "") {
      if      (command == "SRT") state = "SRT";
      else if (command == "SET") {
        state = "SET";
        prevDir = 0;       // SET 진입 시 이전 상태 초기화
        prevBtn = false;
      }
      else if (command == "STP" || command == "END") state = "END";

      udpSend(command);
      Serial.print("[UDP 송신] ESP32 → PC: ");
      Serial.println(command);
    }
  }

  // ── 4. 상태가 END면 조이스틱 전송 건너뜀 ─────────────
  if (state == "END") {
    delay(50);
    return;
  }

  // ── 5. P1 조이스틱 데이터 읽기 (10회 평균) ───────────
  long sumX = 0, sumY = 0;
  for (int i = 0; i < 10; i++) {
    sumX += analogRead(P1_X);
    sumY += analogRead(P1_Y);
  }
  int avgX = sumX / 10;
  int avgY = sumY / 10;

  // ── 6. 상태에 따른 UDP 데이터 전송 ───────────────────
  if (state == "SRT") {
    // 게임플레이: XY 좌표 전송
    String sendData = String(avgX) + "," + String(avgY);
    udpSend(sendData);
    Serial.print("[UDP 송신-SRT] ");
    Serial.println(sendData);
    delay(20);
  }

  else if (state == "SET") {
    // UP / DN 방향 감지 (상태 변화 시 1회 전송)
    int curDir = 0;
    if      (avgY < JOY_UP_THRESHOLD) curDir =  1;  // UP
    else if (avgY > JOY_DN_THRESHOLD) curDir = -1;  // DN

    if (curDir != prevDir) {
      if      (curDir ==  1) { udpSend("UP");  Serial.println("[UDP 송신-SET] UP"); }
      else if (curDir == -1) { udpSend("DN");  Serial.println("[UDP 송신-SET] DN"); }
      prevDir = curDir;
    }

    // 버튼 클릭 감지 (눌리는 순간 1회 전송)
    bool curBtn = (digitalRead(P1_SW) == LOW);
    if (curBtn && !prevBtn) {
      udpSend("CLK");
      Serial.println("[UDP 송신-SET] CLK");
    }
    prevBtn = curBtn;

    delay(50);
  }
}
