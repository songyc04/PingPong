/*
 * ESP32 통합 코드 — 키패드 + 조이스틱 P1/P2 + LCD (전체 UDP)
 *
 * [키패드 매핑]
 *  A → SRT (게임 시작)
 *  B → STP (일시 정지)
 *  C → SET (설정 모드 진입)
 *  D → END (종료)
 *  # → END (종료)
 *
 * [상태 머신]
 *  SRT: 게임플레이 — P1 + P2 조이스틱 XY 좌표를 UDP 10001로 전송
 *  SET: 설정 모드  — P1 조이스틱 UP/DN/CLK 이벤트만 UDP 10001로 전송
 *  END: 정지       — UDP 데이터 전송 중단
 *
 * [SRT 전송 포맷]
 *  "P1:X,Y"  — P1 조이스틱 좌표
 *  "P2:X,Y"  — P2 조이스틱 좌표
 *
 * [SET 모드 이벤트 — P1만 동작]
 *  Y < 1000  → "UP"
 *  Y > 3000  → "DN"
 *  버튼 클릭 → "CLK"
 *  상태 변화 시 1회만 전송 (연속 전송 방지)
 *
 * [LCD 수신 명령 — UDP 10002]
 *  "SRT"              → PLAYING
 *  "STP"              → STOP
 *  "DRAW,<score>"     → DRAW / SCORE:<score>
 *  "PLAYER<n>,<score>"→ WINNER:PLAYER<n> / SCORE:<score>
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Keypad.h>
#include <Ticker.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ─────────────────────────────────────────
//  LCD
// ─────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─────────────────────────────────────────
//  네트워크 설정
// ─────────────────────────────────────────
const char* ssid     = "fusion";
const char* password = "12345678";

IPAddress local_IP(192, 168, 0, 207);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

const char*    serverIP      = "192.168.0.102";
const uint16_t UDP_SEND_PORT = 10001;  // 조이스틱/키패드 → PC
const uint16_t UDP_RECV_PORT = 10002;  // PC → LCD 수신

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
//  조이스틱 핀 설정
//  SRT: P1 + P2 모두 동작
//  SET: P1만 동작
// ─────────────────────────────────────────
const int P1_X  = 34;  const int P1_Y  = 35;  const int P1_SW = 32;
const int P2_X  = 36;  const int P2_Y  = 39;  const int P2_SW = 33;

// ─────────────────────────────────────────
//  SET 모드 임계값 (ADC 0~4095, 중앙 ~2048)
// ─────────────────────────────────────────
const int JOY_UP_THRESHOLD = 1000;
const int JOY_DN_THRESHOLD = 3000;

// ─────────────────────────────────────────
//  통신 객체
// ─────────────────────────────────────────
WiFiUDP udpSend;  // 조이스틱·키패드 송신 (→ PC :10001)
WiFiUDP udpRecv;  // LCD 명령 수신      (← PC :10002)

// ─────────────────────────────────────────
//  전역 변수
// ─────────────────────────────────────────
Ticker        keypadTimer;
volatile char pressedKey = 0;
String        state      = "END";

// SET 모드 P1 이전 상태 (연속 전송 방지)
int  prevDir = 0;    // 0=중립, 1=UP, -1=DN
bool prevBtn = false;

// ─────────────────────────────────────────
//  LCD 표시 함수
// ─────────────────────────────────────────
void showReady() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("PING PONG");
  lcd.setCursor(0, 1); lcd.print("READY");
}
void showRunning() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("PLAYING");
}
void showPaused() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("STOP");
}
void showResult(int score, String winner) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("WINNER:"); lcd.print(winner);
  lcd.setCursor(0, 1); lcd.print("SCORE:");  lcd.print(score);
}
void showDraw(int score) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("DRAW");
  lcd.setCursor(0, 1); lcd.print("SCORE:"); lcd.print(score);
}
void showSet() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("SETTING...");
}

// ─────────────────────────────────────────
//  UDP 송신 헬퍼
// ─────────────────────────────────────────
void sendUDP(const String& msg) {
  udpSend.beginPacket(serverIP, UDP_SEND_PORT);
  udpSend.print(msg + "\n");
  udpSend.endPacket();
}

// ─────────────────────────────────────────
//  조이스틱 평균값 읽기 헬퍼 (10회 평균)
// ─────────────────────────────────────────
int readAvg(int pin) {
  long sum = 0;
  for (int i = 0; i < 10; i++) sum += analogRead(pin);
  return sum / 10;
}

// ─────────────────────────────────────────
//  인터럽트 핸들러 — 키패드 스캔 (30ms마다)
// ─────────────────────────────────────────
void IRAM_ATTR onKeypadScan() {
  char key = customKeypad.getKey();
  if (key) pressedKey = key;
}

// ─────────────────────────────────────────
//  Wi-Fi 연결 (블로킹)
// ─────────────────────────────────────────
void connectWiFi() {
  if (!WiFi.config(local_IP, gateway, subnet))
    Serial.println("고정 IP 설정 실패");
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

  // LCD 초기화 — SDA: GPIO25, SCL: GPIO26
  Wire.begin(25, 26);
  lcd.init();
  lcd.backlight();
  showReady();

  // 조이스틱 버튼 핀
  pinMode(P1_SW, INPUT_PULLUP);
  pinMode(P2_SW, INPUT_PULLUP);

  connectWiFi();

  udpSend.begin(UDP_SEND_PORT);
  udpRecv.begin(UDP_RECV_PORT);
  Serial.println("UDP 송신 소켓 시작 (포트 " + String(UDP_SEND_PORT) + ")");
  Serial.println("UDP 수신 소켓 시작 (포트 " + String(UDP_RECV_PORT) + ")");

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
    udpSend.begin(UDP_SEND_PORT);
    udpRecv.begin(UDP_RECV_PORT);
    state = "END";
  }

  // ── 2. PC → ESP32 LCD 명령 수신 (포트 10002) ──────────
  int recvSize = udpRecv.parsePacket();
  if (recvSize > 0) {
    char buf[255] = {0};
    int len = udpRecv.read(buf, 254);
    if (len > 0) buf[len] = '\0';

    String response = String(buf);
    response.trim();
    Serial.print("[LCD 수신] ");
    Serial.println(response);

    if (response == "SRT") {
      state = "SRT";
      showRunning();
    }
    else if (response == "STP") {
      state = "END";
      showPaused();
    }
    else if (response == "END") {
      state = "END";
      showReady();
    }
    else if (response.startsWith("DRAW")) {
      state = "END";
      int commaIndex = response.indexOf(',');
      int score = response.substring(commaIndex + 1).toInt();
      showDraw(score);
    }
    else if (response.startsWith("PLAYER")) {
      state = "END";
      int commaIndex = response.indexOf(',');
      String winner = response.substring(0, commaIndex);
      int    score  = response.substring(commaIndex + 1).toInt();
      showResult(score, winner);
    }
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
      if (command == "SRT") {
        state = "SRT";
        showRunning();
      }
      else if (command == "SET") {
        state = "SET";
        prevDir = 0;
        prevBtn = false;
        showSet();
      }
      else if (command == "STP") {
        state = "END";
        showPaused();
      }
      else if (command == "END") {
        state = "END";
        showReady();
      }

      sendUDP(command);
      Serial.print("[UDP 송신] ESP32 → PC: ");
      Serial.println(command);
    }
  }

  // ── 4. 상태가 END면 조이스틱 전송 건너뜀 ─────────────
  if (state == "END") {
    delay(50);
    return;
  }

  // ── 5. 상태에 따른 조이스틱 처리 ─────────────────────

  // ── SRT: P1 + P2 모두 좌표 전송 ──────────────────────
  if (state == "SRT") {
    int p1X = readAvg(P1_X);
    int p1Y = readAvg(P1_Y);
    int p2X = readAvg(P2_X);
    int p2Y = readAvg(P2_Y);

    String data = String(p1X) + "," + String(p1Y) + "," + String(p2X) + "," + String(p2Y);
    sendUDP(data);
    Serial.print("[UDP 송신-SRT] ");
    Serial.println(data);

    delay(20);
  }

  // ── SET: P1만 UP/DN/CLK 이벤트 전송 ──────────────────
  else if (state == "SET") {
    int p1Y = readAvg(P1_Y);

    // UP / DN 방향 감지 (상태 변화 시 1회 전송)
    int curDir = 0;
    if      (p1Y < JOY_UP_THRESHOLD) curDir =  1;  // UP
    else if (p1Y > JOY_DN_THRESHOLD) curDir = -1;  // DN

    if (curDir != prevDir) {
      if      (curDir ==  1) { sendUDP("UP");  Serial.println("[UDP 송신-SET] UP"); }
      else if (curDir == -1) { sendUDP("DN");  Serial.println("[UDP 송신-SET] DN"); }
      prevDir = curDir;
    }

    // P1 버튼 클릭 감지 (눌리는 순간 1회 전송)
    bool curBtn = (digitalRead(P1_SW) == LOW);
    if (curBtn && !prevBtn) {
      sendUDP("CLK");
      Serial.println("[UDP 송신-SET] CLK");
    }
    prevBtn = curBtn;

    delay(50);
  }
}
