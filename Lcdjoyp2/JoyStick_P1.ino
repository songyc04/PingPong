/*
 * ╔══════════════════════════════════════════════════════╗
 * ESP32 핀볼 [P1 보드] - 최종 완성본
 * (고정 IP: 192.168.0.200 / Python PC: 192.168.0.138)
 * ╚══════════════════════════════════════════════════════╝
 *
 * [핀 특성 - 실측 기반]
 *  PIN_LR = VRY(35) : 좌우 전담 → 왼쪽=HIGH(4095), 오른쪽=LOW(0)
 *  PIN_UD = VRX(32) : 상하 전담 → 위=LOW(0),       아래=HIGH(4095)
 *
 * [소프트웨어 출력 목표]
 *  X : 왼쪽=0,  오른쪽=4095  (PIN_LR 반전)
 *  Y : 위=0,    아래=4095    (PIN_UD 그대로)
 *  중립 : X=2048, Y=2048
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ─── 네트워크 설정 ────────────────────────────────────────
const char* ssid     = "fusion";
const char* password = "12345678";

IPAddress local_IP(192, 168, 0, 200);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress serverIP(192, 168, 0, 138);

const int SEND_PORT    = 10001;
const int RECEIVE_PORT = 10003;

WiFiUDP udp;

// ─── 핀 정의 ──────────────────────────────────────────────
const int PIN_LR = 35;   // 좌우 (VRY)
const int PIN_UD = 32;   // 상하 (VRX)
const int SW     = 14;   // 버튼

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─── 조이스틱 상수 ────────────────────────────────────────
#define JOY_MID          2048
#define JOY_MAX          4095
#define JOY_THRESH        800   // 메뉴 조작 임계값
#define SEND_INTERVAL_MS   10   // 인게임 전송 주기 (100Hz)
#define CHANGE_THRESH       8   // 인게임 변화 감지 임계값
#define FILTER_SIZE         8   // 이동평균 샘플 수
#define JOY_DEADZONE      150   // 중립 데드존 (raw 단위)

// ─── 축별 독립 이동평균 필터 ──────────────────────────────
struct AxisFilter {
  int  buf[FILTER_SIZE];
  long sum;
  int  idx;

  // 실측값으로 버퍼 초기화 (0-fill 방지)
  void init(int pin) {
    int v = analogRead(pin);
    sum = 0;
    for (int i = 0; i < FILTER_SIZE; i++) { buf[i] = v; sum += v; }
    idx = 0;
  }

  int update(int pin) {
    sum     -= buf[idx];
    buf[idx] = analogRead(pin);
    sum     += buf[idx];
    idx      = (idx + 1) % FILTER_SIZE;
    return (int)(sum / FILTER_SIZE);
  }
};

AxisFilter filterLR;   // 좌우축 (PIN_LR = VRY)
AxisFilter filterUD;   // 상하축 (PIN_UD = VRX)

int RAW_LR_MID = 2048;  // setup()에서 실측
int RAW_UD_MID = 2048;  // setup()에서 실측

// ─── 게임 상태 ────────────────────────────────────────────
enum GameState { STATE_END, STATE_SET, STATE_SRT };
volatile GameState gState         = STATE_END;
volatile bool      joystickActive = false;
volatile int       setMenuCursor  = 0;
volatile bool      resetMenuFlags = false;

// ─── LCD 버퍼 (멀티코어 뮤텍스 보호) ─────────────────────
portMUX_TYPE lcdMux    = portMUX_INITIALIZER_UNLOCKED;
volatile bool lcdUpdateReq = false;
char lcdBuf0[17] = "MAIN MENU       ";
char lcdBuf1[17] = "READY...        ";

// ─── 버튼 인터럽트 ────────────────────────────────────────
volatile bool          buttonClicked     = false;
volatile unsigned long lastInterruptTime = 0;
#define DEBOUNCE_TIME_MS 250

// ─── 타이머 ───────────────────────────────────────────────
unsigned long tLastSend       = 0;
unsigned long tLastDebugPrint = 0;

// ─── 마지막 전송값 (변화 감지용) ─────────────────────────
int lastSentX = JOY_MID;
int lastSentY = JOY_MID;

TaskHandle_t NetworkRxTask;

// ════════════════════════════════════════════════════════
//  축 매핑: raw 평균값 → 0~4095
//  데드존 안 → 정확히 2048 반환
// ════════════════════════════════════════════════════════
int mapAxis(int raw, int midVal) {
  if (abs(raw - midVal) <= JOY_DEADZONE) return JOY_MID;
  int mapped;
  if (raw < midVal)
    mapped = map(raw, 0, midVal - JOY_DEADZONE, 0, JOY_MID);
  else
    mapped = map(raw, midVal + JOY_DEADZONE, JOY_MAX, JOY_MID, JOY_MAX);
  return constrain(mapped, 0, JOY_MAX);
}

// 소프트 X: 왼쪽=0, 오른쪽=4095 (PIN_LR 반전)
int getFilteredX() {
  int raw = filterLR.update(PIN_LR);
  int val = mapAxis(raw, RAW_LR_MID);
  return JOY_MAX - val;   // 왼쪽 HIGH → 반전
}

// 소프트 Y: 위=0, 아래=4095 (PIN_UD 그대로)
int getFilteredY() {
  int raw = filterUD.update(PIN_UD);
  return mapAxis(raw, RAW_UD_MID);
}

// ════════════════════════════════════════════════════════
//  LCD — Core 1 전용 렌더, 뮤텍스로 버퍼 보호
//  Core 0(networkRxLoop)은 requestLCD()만 호출
//  실제 I2C 쓰기는 항상 Core 1의 renderLCD()에서만 수행
// ════════════════════════════════════════════════════════
void requestLCD(const char* line0, const char* line1) {
  portENTER_CRITICAL(&lcdMux);
  snprintf(lcdBuf0, 17, "%-16s", line0);
  snprintf(lcdBuf1, 17, "%-16s", line1);
  lcdUpdateReq = true;
  portEXIT_CRITICAL(&lcdMux);
}

void renderLCD() {
  if (!lcdUpdateReq) return;

  // 버퍼를 로컬 스냅샷으로 복사 후 임계구역 해제
  char line0[17], line1[17];
  portENTER_CRITICAL(&lcdMux);
  memcpy(line0, lcdBuf0, 17);
  memcpy(line1, lcdBuf1, 17);
  lcdUpdateReq = false;
  portEXIT_CRITICAL(&lcdMux);

  // I2C 쓰기는 임계구역 밖 Core 1에서만
  lcd.setCursor(0, 0); lcd.print(line0);
  lcd.setCursor(0, 1); lcd.print(line1);
}

// ════════════════════════════════════════════════════════
//  UDP 송신
// ════════════════════════════════════════════════════════
void sendUDP(const String& msg) {
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(serverIP, SEND_PORT);
    udp.print(msg);
    udp.endPacket();
  }
  // 조이스틱 데이터(':' 포함)는 시리얼 로그 생략
  if (msg.indexOf(':') == -1) {
    String logMsg = msg; logMsg.trim();
    Serial.print("[UDP 송신] -> "); Serial.println(logMsg);
  }
}

// ════════════════════════════════════════════════════════
//  버튼 인터럽트 (IRAM)
// ════════════════════════════════════════════════════════
void IRAM_ATTR clickButton() {
  unsigned long t = millis();
  if (t - lastInterruptTime > DEBOUNCE_TIME_MS) {
    buttonClicked     = true;
    lastInterruptTime = t;
  }
}

// ════════════════════════════════════════════════════════
//  UDP 수신 태스크 — Core 0
//  LCD는 requestLCD()만 호출 (I2C 직접 접근 금지)
// ════════════════════════════════════════════════════════
void networkRxLoop(void* pvParameters) {
  char rxBuffer[255];
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      int packetSize = udp.parsePacket();
      if (packetSize) {
        int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
        if (len > 0) rxBuffer[len] = '\0';

        String response = String(rxBuffer);
        response.trim();

        Serial.print("📥 [수신] <- [");
        Serial.print(response);
        Serial.println("]");

        if (response == "END") {
          gState         = STATE_END;
          joystickActive = false;
          resetMenuFlags = true;
          requestLCD("MAIN MENU", "READY...");
        }
        else if (response == "SET") {
          gState         = STATE_SET;
          joystickActive = false;
          setMenuCursor  = 0;
          resetMenuFlags = true;
          requestLCD("> 1. OPTION SET", "  2. BACK");
        }
        else if (response == "SRT") {
          gState         = STATE_SRT;
          joystickActive = true;
          tLastSend      = millis();
          requestLCD("PLAYING GAME!!", "P1 vs P2 MATCH");
        }
        else if (response.startsWith("WIN")  ||
                 response.startsWith("LOSE") ||
                 response.startsWith("DRAW")) {
          joystickActive = false;
          requestLCD("GAME OVER", response.c_str());
        }
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

// ════════════════════════════════════════════════════════
//  setup()
// ════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(100);

  // ① LCD 완전 초기화 (가장 먼저 — 깨진 문자 방지)
  Wire.begin(21, 22);
  delay(50);
  lcd.init();
  delay(50);
  lcd.backlight();
  delay(50);
  lcd.clear();
  delay(50);

  // ② WiFi 연결 시작 (논블로킹 킥오프)
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  Serial.println("📡 [WiFi] 연결 시도 중...");

  // ③ 나머지 하드웨어 초기화
  analogReadResolution(12);
  pinMode(SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SW), clickButton, FALLING);

  // ④ WiFi 연결 완료 대기 (LCD에 진행 표시)
  requestLCD("WiFi 연결 중...", "Please wait...");
  renderLCD();

  int dotCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    dotCount++;
    char statusLine[17];
    snprintf(statusLine, 17, "Connecting%.*s   ", (dotCount % 4) + 1, "....");
    requestLCD("WiFi 연결 중...", statusLine);
    renderLCD();
  }

  Serial.printf("✅ [WiFi] 연결 완료! IP: %s\n",
                WiFi.localIP().toString().c_str());
  requestLCD("WiFi Connected!", WiFi.localIP().toString().c_str());
  renderLCD();
  delay(800);

  // ⑤ UDP 소켓 열기
  udp.begin(RECEIVE_PORT);

  // ⑥ 조이스틱 캘리브레이션 (손 떼고 대기)
  requestLCD("CALIBRATING...", "DO NOT TOUCH JST");
  renderLCD();

  const int CAL_SAMPLES = 64;
  long sumLR = 0, sumUD = 0;
  for (int i = 0; i < CAL_SAMPLES; i++) {
    sumLR += analogRead(PIN_LR);
    sumUD += analogRead(PIN_UD);
    delay(3);
  }
  RAW_LR_MID = (int)(sumLR / CAL_SAMPLES);
  RAW_UD_MID = (int)(sumUD / CAL_SAMPLES);

  Serial.printf("📐 [캘리브] RAW_LR_MID(좌우/VRY)=%d  RAW_UD_MID(상하/VRX)=%d\n",
                RAW_LR_MID, RAW_UD_MID);

  // ⑦ 필터 버퍼 실측값으로 초기화 + 워밍업
  filterLR.init(PIN_LR);
  filterUD.init(PIN_UD);
  for (int i = 0; i < FILTER_SIZE * 2; i++) {
    filterLR.update(PIN_LR);
    filterUD.update(PIN_UD);
    delay(2);
  }

  // ⑧ 수신 태스크 시작 (Core 0)
  xTaskCreatePinnedToCore(
    networkRxLoop, "NetworkRxTask",
    10000, NULL, 1, &NetworkRxTask, 0
  );

  requestLCD("MAIN MENU", "READY...");
  renderLCD();
  Serial.println("🎮 [시스템] 초기화 완료 - 게임 대기 중");
}

// ════════════════════════════════════════════════════════
//  loop() — Core 1
// ════════════════════════════════════════════════════════
void loop() {
  // LCD 렌더 (Core 1 전용 — 가장 먼저)
  renderLCD();

  // ── 버튼 처리 ──────────────────────────────────────────
  if (buttonClicked) {
    buttonClicked = false;
    if      (gState == STATE_END || gState == STATE_SRT) sendUDP("SET\n");
    else if (gState == STATE_SET)                        sendUDP("CLK\n");
  }

  // ── 메뉴 플래그 리셋 ───────────────────────────────────
  static bool lastMenuUp   = false;
  static bool lastMenuDown = false;
  static bool lastUpState  = false;
  if (resetMenuFlags) {
    lastMenuUp = lastMenuDown = lastUpState = false;
    resetMenuFlags = false;
  }

  unsigned long now = millis();

  // ── X/Y 한 번만 읽기 (루프당 ADC 이중 호출 방지) ───────
  int cx = getFilteredX();
  int cy = getFilteredY();

  // ── 디버그 출력 (인게임 중 생략, 150ms 간격) ───────────
  if (gState != STATE_SRT && now - tLastDebugPrint >= 150) {
    Serial.printf("🔍 [모니터] X:%4d  Y:%4d\n", cx, cy);
    tLastDebugPrint = now;
  }

  // ── 상태별 처리 ────────────────────────────────────────
  if (gState == STATE_END) {
    // 위로(Y=0 방향) → 게임 시작 요청
    bool isUp = (cy < JOY_MID - JOY_THRESH);
    if (isUp && !lastUpState) sendUDP("SRT\n");
    lastUpState = isUp;
  }
  else if (gState == STATE_SET) {
    bool isUp   = (cy < JOY_MID - JOY_THRESH);
    bool isDown = (cy > JOY_MID + JOY_THRESH);

    if (isUp && !lastMenuUp) {
      sendUDP("UP\n");
      setMenuCursor = 0;
      requestLCD("> 1. OPTION SET", "  2. BACK");
    }
    lastMenuUp = isUp;

    if (isDown && !lastMenuDown) {
      sendUDP("DN\n");
      setMenuCursor = 1;
      requestLCD("  1. OPTION SET", "> 2. BACK");
    }
    lastMenuDown = isDown;
  }
  else if (gState == STATE_SRT && joystickActive) {
    // 인게임: 10ms 주기, 변화량 초과 시에만 전송
    if (now - tLastSend >= SEND_INTERVAL_MS) {
      if (abs(cx - lastSentX) > CHANGE_THRESH ||
          abs(cy - lastSentY) > CHANGE_THRESH) {

        // 정수 두 개를 "X:Y\n" 문자열로 전송
        // 파이썬: x, y = map(int, data.decode().strip().split(':'))
        char packet[16];
        snprintf(packet, sizeof(packet), "%d:%d\n", cx, cy);
        sendUDP(packet);

        lastSentX = cx;
        lastSentY = cy;
        
        Serial.printf("🚀 [송신] :");
      }
      tLastSend = now;
    }
  }
}
