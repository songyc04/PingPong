/*
 * ╔══════════════════════════════════════════════════════╗
 * ESP32 핀볼 [P1 보드] - LCD 파이썬 수신 전용
 * (고정 IP: 192.168.0.200 / Python PC: 192.168.0.138)
 * ╚══════════════════════════════════════════════════════╝
 *
 * [핀 특성 - 실측 기반]
 *  PIN_LR = VRY(35) : 좌우 전담 → 왼쪽=HIGH(4095), 오른쪽=LOW(0)
 *  PIN_UD = VRX(32) : 상하 전담 → 위=LOW(0),       아래=HIGH(4095)
 *  I2C    : SDA=25, SCL=26
 *
 * [LCD 정책]
 *  파이썬에서 수신한 메시지만 LCD에 표시
 *  ESP32 자체 상태(WiFi, 캘리브 등)는 시리얼 모니터에만 출력
 *
 * [LCD 멀티코어 설계 - Queue 방식]
 *  Core 0 : requestLCD() → Queue에 push (논블로킹)
 *  Core 1 : renderLCD()  → Queue에서 pop → I2C 쓰기
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
IPAddress serverIP(192, 168, 0, 167);

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
#define JOY_THRESH        800
#define SEND_INTERVAL_MS   10
#define CHANGE_THRESH       8
#define FILTER_SIZE         8
#define JOY_DEADZONE      150

// ─── 축별 독립 이동평균 필터 ──────────────────────────────
struct AxisFilter {
  int  buf[FILTER_SIZE];
  long sum;
  int  idx;

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

AxisFilter filterLR;
AxisFilter filterUD;

int RAW_LR_MID = 2048;
int RAW_UD_MID = 2048;

// ─── 게임 상태 ────────────────────────────────────────────
enum GameState { STATE_END, STATE_SET, STATE_SRT };
volatile GameState gState         = STATE_END;
volatile bool      joystickActive = false;
volatile int       setMenuCursor  = 0;
volatile bool      resetMenuFlags = false;

// ─── LCD Queue (파이썬 수신 메시지 전용) ──────────────────
struct LcdMsg {
  char line0[17];
  char line1[17];
};

QueueHandle_t lcdQueue = NULL;

// ─── 버튼 인터럽트 ────────────────────────────────────────
volatile bool          buttonClicked     = false;
volatile unsigned long lastInterruptTime = 0;
#define DEBOUNCE_TIME_MS 250

// ─── 타이머 ───────────────────────────────────────────────
unsigned long tLastSend       = 0;
unsigned long tLastDebugPrint = 0;

// ─── 마지막 전송값 ────────────────────────────────────────
int lastSentX = JOY_MID;
int lastSentY = JOY_MID;

TaskHandle_t NetworkRxTask;

// ════════════════════════════════════════════════════════
//  축 매핑
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

int getFilteredX() {
  int raw = filterLR.update(PIN_LR);
  int val = mapAxis(raw, RAW_LR_MID);
  return JOY_MAX - val;
}

int getFilteredY() {
  int raw = filterUD.update(PIN_UD);
  return mapAxis(raw, RAW_UD_MID);
}

// ════════════════════════════════════════════════════════
//  LCD 함수 — 파이썬 수신 메시지 전용
// ════════════════════════════════════════════════════════

// Core 0 / Core 1 어디서든 호출 가능 (논블로킹)
// 파이썬에서 받은 문자열을 두 줄로 나눠서 Queue에 push
void requestLCD(const char* line0, const char* line1) {
  if (lcdQueue == NULL) return;
  LcdMsg msg;
  snprintf(msg.line0, 17, "%-16s", line0);
  snprintf(msg.line1, 17, "%-16s", line1);
  // 큐가 꽉 차면 가장 오래된 것 버리고 새 메시지 삽입
  if (xQueueSend(lcdQueue, &msg, 0) == errQUEUE_FULL) {
    LcdMsg dummy;
    xQueueReceive(lcdQueue, &dummy, 0);
    xQueueSend(lcdQueue, &msg, 0);
  }
}

// Core 1(loop()) 전용 — Queue에서 꺼내 I2C 쓰기
void renderLCD() {
  if (lcdQueue == NULL) return;
  LcdMsg msg;
  if (xQueueReceive(lcdQueue, &msg, 0) == pdTRUE) {
    lcd.setCursor(0, 0); lcd.print(msg.line0);
    lcd.setCursor(0, 1); lcd.print(msg.line1);
  }
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
//  파이썬 메시지 수신 → 상태 변경 + LCD 업데이트
//  I2C 직접 접근 절대 금지, requestLCD()만 사용
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

        // ── 상태 변경 ──────────────────────────────────
        if (response == "END") {
          gState         = STATE_END;
          joystickActive = false;
          resetMenuFlags = true;
        }
        else if (response == "SET") {
          gState         = STATE_SET;
          joystickActive = false;
          setMenuCursor  = 0;
          resetMenuFlags = true;
        }
        else if (response == "SRT") {
          gState         = STATE_SRT;
          joystickActive = true;
          tLastSend      = millis();
        }
        else if (response.startsWith("WIN")  ||
                 response.startsWith("LOSE") ||
                 response.startsWith("DRAW")) {
          joystickActive = false;
        }

        // ── LCD 표시: 파이썬 수신 메시지를 그대로 출력 ──
        // 16자 초과 시 두 번째 줄로 넘김
        char part0[17] = {0};
        char part1[17] = {0};
        int  msgLen    = response.length();

        if (msgLen <= 16) {
          // 16자 이하 → 1번째 줄에만 표시
          strncpy(part0, response.c_str(), 16);
        } else {
          // 17자 이상 → 앞 16자는 1번째 줄, 나머지는 2번째 줄
          strncpy(part0, response.c_str(), 16);
          strncpy(part1, response.c_str() + 16, 16);
        }
        requestLCD(part0, part1);
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

  // ① LCD 초기화 (빈 화면으로 시작 — 파이썬 메시지 대기)
  Wire.begin(25, 26);  // SDA=25, SCL=26
  delay(50);
  lcd.init();
  delay(50);
  lcd.backlight();
  delay(50);
  lcd.clear();         // 빈 화면 유지 (자체 메시지 없음)
  delay(50);

  // ② WiFi 연결 (시리얼 모니터에만 상태 출력)
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  Serial.println("📡 [WiFi] 연결 시도 중...");
  while (WiFi.status() != WL_CONNECTED) { delay(200); }
  Serial.printf("✅ [WiFi] 연결 완료! IP: %s\n",
                WiFi.localIP().toString().c_str());

  // ③ 나머지 하드웨어 초기화
  analogReadResolution(12);
  pinMode(SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SW), clickButton, FALLING);

  // ④ UDP 소켓 열기
  udp.begin(RECEIVE_PORT);

  // ⑤ 조이스틱 캘리브레이션 (시리얼 모니터에만 출력)
  Serial.println("📐 [캘리브] 조이스틱 중립값 측정 중...");
  const int CAL_SAMPLES = 64;
  long sumLR = 0, sumUD = 0;
  for (int i = 0; i < CAL_SAMPLES; i++) {
    sumLR += analogRead(PIN_LR);
    sumUD += analogRead(PIN_UD);
    delay(3);
  }
  RAW_LR_MID = (int)(sumLR / CAL_SAMPLES);
  RAW_UD_MID = (int)(sumUD / CAL_SAMPLES);
  Serial.printf("📐 [캘리브] RAW_LR_MID=%d  RAW_UD_MID=%d\n",
                RAW_LR_MID, RAW_UD_MID);

  // ⑥ 필터 초기화 + 워밍업
  filterLR.init(PIN_LR);
  filterUD.init(PIN_UD);
  for (int i = 0; i < FILTER_SIZE * 2; i++) {
    filterLR.update(PIN_LR);
    filterUD.update(PIN_UD);
    delay(2);
  }

  // ⑦ LCD Queue 생성
  lcdQueue = xQueueCreate(4, sizeof(LcdMsg));

  // ⑧ 수신 태스크 시작 (Core 0)
  xTaskCreatePinnedToCore(
    networkRxLoop, "NetworkRxTask",
    10000, NULL, 1, &NetworkRxTask, 0
  );

  Serial.println("🎮 [시스템] 초기화 완료 - 파이썬 메시지 대기 중");
}

// ════════════════════════════════════════════════════════
//  loop() — Core 1
// ════════════════════════════════════════════════════════
void loop() {
  // LCD 렌더링 (파이썬 수신 메시지만 표시)
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

  // ── X/Y 한 번만 읽기 ───────────────────────────────────
  int cx = getFilteredX();
  int cy = getFilteredY();

  // ── 디버그 출력 (인게임 제외, 150ms 간격) ──────────────
  if (gState != STATE_SRT && now - tLastDebugPrint >= 150) {
    Serial.printf("🔍 [모니터] X:%4d  Y:%4d\n", cx, cy);
    tLastDebugPrint = now;
  }

  // ── 상태별 처리 ────────────────────────────────────────
  if (gState == STATE_END) {
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
    }
    lastMenuUp = isUp;

    if (isDown && !lastMenuDown) {
      sendUDP("DN\n");
      setMenuCursor = 1;
    }
    lastMenuDown = isDown;
  }
  else if (gState == STATE_SRT && joystickActive) {
    if (now - tLastSend >= SEND_INTERVAL_MS) {
      if (abs(cx - lastSentX) > CHANGE_THRESH ||
          abs(cy - lastSentY) > CHANGE_THRESH) {
        char packet[16];
        snprintf(packet, sizeof(packet), "%d:%d\n", cx, cy);
        sendUDP(packet);
        lastSentX = cx;
        lastSentY = cy;
        Serial.printf(":", cx, cy);
      }
      tLastSend = now;
    }
  }
}
