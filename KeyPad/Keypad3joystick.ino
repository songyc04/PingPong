/*
 * ESP32 통합 코드 — 키패드(TCP) + 조이스틱(UDP)
 *
 * [구조]
 *  - 키패드: Ticker 인터럽트로 백그라운드 스캔, 특수키 → TCP로 명령 전송
 *  - 조이스틱: loop()에서 주기적으로 읽기, 상태(state)에 따라 UDP로 데이터 전송
 *  - 상태 수신: UDP 패킷으로 PC → ESP32 상태 변경 명령 수신
 *
 * [통신 포트]
 *  - TCP: PC IP:10001  (키패드 명령 전송 / 단방향)
 *  - UDP: PC IP:10001  (조이스틱 데이터 송신 + 상태 수신 / 양방향)
 *
 * [상태 머신]
 *  SRT: 게임플레이 — 조이스틱 좌표를 UDP로 전송
 *  SET: 설정 모드  — 조이스틱 클릭 상태를 UDP로 전송
 *  END: 정지       — UDP 데이터 전송 중단
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Keypad.h>
#include <Ticker.h>

// ─────────────────────────────────────────
//  네트워크 설정
// ─────────────────────────────────────────
const char*    ssid      = "fusion";
const char*    password  = "12345678";
const char*    serverIP  = "192.168.0.114";  // PC의 IPv4 주소
const uint16_t TCP_PORT  = 10001;
const uint16_t UDP_PORT  = 10001;

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
// ─────────────────────────────────────────
const int P1_X  = 34;  const int P1_Y  = 35;  const int P1_SW = 32;
const int P2_X  = 36;  const int P2_Y  = 39;  const int P2_SW = 33;

// ─────────────────────────────────────────
//  통신 객체
// ─────────────────────────────────────────
WiFiClient tcpClient;
WiFiUDP    udp;

// ─────────────────────────────────────────
//  전역 변수
// ─────────────────────────────────────────
Ticker           keypadTimer;
volatile char    pressedKey        = 0;   // 인터럽트에서 채워지는 키값
String           state             = "SRT"; // 현재 상태 (SRT / SET / END)
unsigned long    lastTcpAttempt    = 0;
const uint32_t   TCP_RETRY_MS      = 5000; // TCP 재연결 주기

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

  // 조이스틱 버튼 핀 설정
  pinMode(P1_SW, INPUT_PULLUP);
  pinMode(P2_SW, INPUT_PULLUP);

  // Wi-Fi 연결
  connectWiFi();

  // UDP 리스너 시작 (PC → ESP32 상태 명령 수신용)
  udp.begin(UDP_PORT);
  Serial.println("UDP 리스너 시작 완료.");

  // Ticker 인터럽트 기반 키패드 스캔 시작 (30ms 주기)
  keypadTimer.attach(0.03, onKeypadScan);
  Serial.println("키패드 타이머 인터럽트 가동.");
}

// ─────────────────────────────────────────
//  loop
// ─────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── 1. Wi-Fi 끊김 감지 및 자동 재연결 ──────────────────
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[경고] Wi-Fi 끊김. 재연결 시도...");
    connectWiFi();
    udp.begin(UDP_PORT); // UDP 소켓 재개
    state = "SRT";
  }

  // ── 2. TCP 클라이언트 자동 재연결 (5초 간격) ──────────
  if (!tcpClient.connected()) {
    if (now - lastTcpAttempt >= TCP_RETRY_MS) {
      lastTcpAttempt = now;
      Serial.println("[TCP] 서버 연결 시도 중...");
      if (tcpClient.connect(serverIP, TCP_PORT)) {
        Serial.println("[TCP] 서버 연결 성공!");
      } else {
        Serial.println("[TCP] 연결 실패. (UDP/키패드는 계속 동작)");
      }
    }
  }

  // ── 3. UDP 상태 변경 명령 수신 (PC → ESP32) ───────────
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

  // ── 4. 키패드 인터럽트 처리 → TCP 명령 전송 ──────────
  if (pressedKey != 0) {
    char key = pressedKey;
    pressedKey = 0;  // 플래그 즉시 초기화

    Serial.print("[키패드] 입력 감지: ");
    Serial.println(key);

    // 특수 키 → 명령 문자열 매핑
    String command = "";
    switch (key) {
      case 'A': command = "SRT"; break;
      case 'B': command = "STP"; break;
      case 'C': command = "SET"; break;
      case 'D': command = "CUS"; break;
      case '#': command = "END"; break;
      // 숫자/기호 키는 필요에 따라 아래에 추가
      // default: command = String(key); break;
    }

    if (command != "") {
      // 로컬 상태도 즉시 반영 (SRT/SET/END 계열 키인 경우)
      if (command == "SRT") state = "SRT";
      else if (command == "SET") state = "SET";
      else if (command == "STP" || command == "END") state = "END";

      if (tcpClient.connected()) {
        Serial.print("[TCP 송신] ESP32 → PC: ");
        Serial.println(command);
        tcpClient.print(command + "\n");
      } else {
        Serial.println("[경고] TCP 미연결 — 명령 전송 불가: " + command);
      }
    }
  }

  // ── 5. TCP 수신 응답 처리 (PC → ESP32) ───────────────
  if (tcpClient.connected() && tcpClient.available() > 0) {
    String response = tcpClient.readStringUntil('\n');
    response.trim();
    Serial.print("[TCP 수신] PC → ESP32: ");
    Serial.println(response);
  }

  // ── 6. 상태가 END면 조이스틱 전송 건너뜀 ─────────────
  if (state == "END") {
    delay(50);
    return;
  }

  // ── 7. 조이스틱 데이터 읽기 (10회 평균으로 노이즈 제거) ─
  long sumP1X = 0, sumP1Y = 0, sumP2X = 0, sumP2Y = 0;
  for (int i = 0; i < 10; i++) {
    sumP1X += analogRead(P1_X);  sumP1Y += analogRead(P1_Y);
    sumP2X += analogRead(P2_X);  sumP2Y += analogRead(P2_Y);
  }
  int avgP1X = sumP1X / 10;  int avgP1Y = sumP1Y / 10;
  int avgP2X = sumP2X / 10;  int avgP2Y = sumP2Y / 10;

  // ── 8. 상태에 따른 UDP 데이터 구성 및 전송 ───────────
  String sendData = "";

  if (state == "SRT") {
    // 게임플레이: 정수 좌표 4개 전송
    sendData = String(avgP1X) + "," + String(avgP1Y) + "," +
               String(avgP2X) + "," + String(avgP2Y);
  }
  else if (state == "SET") {
    // 설정 모드: 클릭 상태 전송
    String p1Clk = (digitalRead(P1_SW) == LOW) ? "O" : "";
    String p2Clk = (digitalRead(P2_SW) == LOW) ? "O" : "";
    sendData = "P1CLK:" + p1Clk + "|P2CLK:" + p2Clk;
  }

  // UDP 전송
  udp.beginPacket(serverIP, UDP_PORT);
  udp.println(sendData);
  udp.endPacket();

  Serial.print("[UDP 송신-" + state + "] ");
  Serial.println(sendData);

  delay(20); // 20ms 주기 = 초당 50회 전송
}
