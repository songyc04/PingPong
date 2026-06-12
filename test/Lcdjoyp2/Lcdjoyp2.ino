/*
 * ╔══════════════════════════════════════════════════════╗
 * ESP32 핀볼 [P1 보드] - LCD 멀티코어 완전 수정본
 * (고정 IP: 192.168.0.200 / Python PC: 192.168.0.138)
 *
 * [수정 사항]
 *  - 아두이노가 자체적으로 만드는 LCD 문구(WiFi 연결중,
 *    캘리브레이션, MAIN MENU/READY... 등 하드코딩 문자열)를
 *    전부 제거함.
 *  - LCD에는 오직 파이썬(PC)에서 UDP로 수신받은 문자열
 *    (response)만 그대로 표시함.
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
 *
 * [LCD 멀티코어 설계]
 *  - requestLCD() : Core 0 / Core 1 어디서든 호출 가능
 *  - renderLCD()  : 반드시 Core 1(loop())에서만 호출
 *  - 세마포어로 버퍼 보호, I2C 쓰기는 Core 1 전용
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ─── 네트워크 설정 ────────────────────────────────────────
const char* ssid     = "fusion";
const char* password = "12345678";

IPAddress local_IP(192, 168, 0, 154);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress serverIP(192, 168, 0, 167);

const int SEND_PORT    = 10002;
const int RECEIVE_PORT = 10004;

WiFiUDP udp;

// ─── 핀 정의 ──────────────────────────────────────────────
const int PIN_LR = 33;   // 좌우 (VRY)
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
volatile bool      srtRequestSent = false;  // SRT 중복 전송 방지 플래그

// ════════════════════════════════════════════════════════
//  LCD 멀티코어 안전 설계
//
//  ┌─────────────────────────────────────────────────┐
//  │  Core 0 (networkRxLoop)                         │
//  │    requestLCD("텍스트") 호출                     │
//  │      → xSemaphoreTake (세마포어 획득)            │
//  │      → 버퍼에 문자열 복사                        │
//  │      → lcdPending = true                        │
//  │      → xSemaphoreGive (세마포어 반환)            │
//  └──────────────────┬──────────────────────────────┘
//                     │ (버퍼만 수정, I2C 접근 없음)
//  ┌──────────────────▼──────────────────────────────┐
//  │  Core 1 (loop)                                  │
//  │    renderLCD() 호출                              │
//  │      → lcdPending 확인                          │
//  │      → xSemaphoreTake (세마포어 획득)            │
//  │      → 버퍼 스냅샷 복사                          │
//  │      → lcdPending = false                       │
//  │      → xSemaphoreGive (세마포어 반환)            │
//  │      → I2C 쓰기 (세마포어 밖, Core 1 전용)       │
//  └─────────────────────────────────────────────────┘
// ════════════════════════════════════════════════════════

// FreeRTOS 바이너리 세마포어 (portMUX 대신 사용)
// portMUX는 같은 코어에서만 안전, 세마포어는 코어 간 안전
SemaphoreHandle_t lcdSemaphore = NULL;
volatile bool     lcdPending   = false;
char lcdBuf0[17]  = "                ";
char lcdBuf1[17]  = "                ";

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
//  LCD 함수
// ════════════════════════════════════════════════════════

// Core 0 / Core 1 어디서든 호출 가능
// 세마포어로 버퍼 보호 후 문자열 복사만 수행 (I2C 접근 없음)
//
// 파이썬에서 받은 문자열(text)을 그대로 LCD에 표시한다.
//  - 16자 이하  : 1번째 줄에 표시, 2번째 줄은 공백 처리
//  - 17자 이상  : 1번째 줄에 앞 16자, 2번째 줄에 나머지 16자를 표시
void requestLCD(const char* text) {
  if (lcdSemaphore == NULL) return;

  // 최대 10ms 대기 후 세마포어 획득
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    size_t len = strlen(text);

    if (len <= 16) {
      snprintf(lcdBuf0, 17, "%-16s", text);
      snprintf(lcdBuf1, 17, "%-16s", "");
    } else {
      char line0[17];
      char line1[17];

      strncpy(line0, text, 16);
      line0[16] = '\0';

      strncpy(line1, text + 16, 16);
      line1[16] = '\0';

      snprintf(lcdBuf0, 17, "%-16s", line0);
      snprintf(lcdBuf1, 17, "%-16s", line1);
    }

    lcdPending = true;
    xSemaphoreGive(lcdSemaphore);
  }
}

// Core 1(loop()) 전용 — 실제 I2C 쓰기 수행
void renderLCD() {
  if (!lcdPending) return;
  if (lcdSemaphore == NULL) return;

  // 버퍼 스냅샷을 로컬 변수로 복사
  char line0[17], line1[17];
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    memcpy(line0, lcdBuf0, 17);
    memcpy(line1, lcdBuf1, 17);
    lcdPending = false;
    xSemaphoreGive(lcdSemaphore);
  } else {
    return; // 세마포어 획득 실패 시 이번 프레임 스킵
  }

  // I2C 쓰기는 세마포어 밖, Core 1 전용
  lcd.setCursor(0, 0);
  lcd.print(line0);
  lcd.setCursor(0, 1);
  lcd.print(line1);
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
//  requestLCD()만 호출 (I2C 직접 접근 절대 금지)
//  LCD에는 파이썬에서 받은 response 문자열을 그대로 표시한다.
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
          srtRequestSent = false;  // 다음 게임을 위해 재전송 잠금 해제
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
        else if (response == "STP") {
          // 기본 상태 메시지 - LCD 표시 없음
        }
        else {
          // SRT, STP, END, SET을 제외한 모든 수신 문자열은
          // 그대로 LCD에 표시 (WIN/LOSE/DRAW 포함)
          requestLCD(response.c_str());
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

  // ① 세마포어 생성 (LCD 함수보다 반드시 먼저)
  lcdSemaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(lcdSemaphore);  // 초기 상태: 사용 가능

  // ② LCD 완전 초기화 (자체 문구 출력 없이 화면만 클리어)
  Wire.begin(25, 26);
  delay(50);
  lcd.init();
  delay(50);
  lcd.backlight();
  delay(50);
  lcd.clear();
  delay(50);

  // ③ WiFi 연결 시작 (논블로킹)
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  Serial.println("📡 [WiFi] 연결 시도 중...");

  // ④ 나머지 하드웨어 초기화
  analogReadResolution(12);
  pinMode(SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SW), clickButton, FALLING);

  // ⑤ WiFi 연결 대기 (LCD에는 표시하지 않고 시리얼로만 진행 상황 출력)
  int dotCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    dotCount++;
    Serial.print(".");
  }
  Serial.println();
  Serial.printf("✅ [WiFi] 연결 완료! IP: %s\n",
                WiFi.localIP().toString().c_str());

  // ⑥ UDP 소켓 열기
  udp.begin(RECEIVE_PORT);

  // ⑦ 조이스틱 캘리브레이션 (LCD 표시 없이 시리얼로만 진행)
  Serial.println("📐 [캘리브] 조이스틱 캘리브레이션 중... 만지지 마세요");
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

  // ⑧ 필터 초기화 + 워밍업
  filterLR.init(PIN_LR);
  filterUD.init(PIN_UD);
  for (int i = 0; i < FILTER_SIZE * 2; i++) {
    filterLR.update(PIN_LR);
    filterUD.update(PIN_UD);
    delay(2);
  }

  // ⑨ 수신 태스크 시작 (모든 준비 완료 후 마지막에)
  xTaskCreatePinnedToCore(
    networkRxLoop, "NetworkRxTask",
    10000, NULL, 1, &NetworkRxTask, 0
  );

  // ⑩ LCD는 파이썬 메시지를 받기 전까지 빈 화면으로 둔다
  Serial.println("🎮 [시스템] 초기화 완료 - 게임 대기 중");
}

// ════════════════════════════════════════════════════════
//  loop() — Core 1
// ════════════════════════════════════════════════════════
void loop() {

  renderLCD();

  // 버튼 → 게임 종료
  if (buttonClicked) {
    buttonClicked = false;

    if (gState == STATE_SRT) {
      sendUDP("END\n");
    }
  }

  static bool lastUpState = false;

  unsigned long now = millis();

  int cx = getFilteredX();
  int cy = getFilteredY();

  // 디버그 출력
  if (gState != STATE_SRT && now - tLastDebugPrint >= 150) {
    Serial.printf("X:%4d  Y:%4d\n", cx, cy);
    tLastDebugPrint = now;
  }

  // 메인 메뉴
  if (gState == STATE_END) {

    bool isUp = (cy < JOY_MID - JOY_THRESH);

    if (isUp && !lastUpState && !srtRequestSent) {
      sendUDP("SRT\n");
      srtRequestSent = true;  // 파이썬이 상태를 바꿔줄 때까지 재전송 금지

      // 파이썬의 SRT 응답을 기다리지 않고 즉시 게임 상태로 전환
      // → 이후 조이스틱을 위로 올려도 다시 SRT가 전송되지 않음
      gState         = STATE_SRT;
      joystickActive = true;
      tLastSend      = now;
    }

    lastUpState = isUp;
  }

  // 설정창
  else if (gState == STATE_SET) {

    // P2는 설정창 조작 안 함

  }

  // 게임 중
  else if (gState == STATE_SRT && joystickActive) {

    if (now - tLastSend >= SEND_INTERVAL_MS) {

      if (abs(cx - lastSentX) > CHANGE_THRESH ||
          abs(cy - lastSentY) > CHANGE_THRESH) {

        char packet[16];

        snprintf(packet,
                 sizeof(packet),
                 "%d:%d\n",
                 cx,
                 cy);

        sendUDP(packet);

        lastSentX = cx;
        lastSentY = cy;
      }

      tLastSend = now;
    }
  }
}
