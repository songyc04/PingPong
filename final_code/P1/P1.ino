/*
 * ╔══════════════════════════════════════════════════════╗
 * ESP32 핀볼 [P1 보드] - LCD 멀티코어 및 카운트다운 완벽 수정본
 * (고정 IP: 192.168.0.200 / Python PC: 192.168.0.138)
 * ╚══════════════════════════════════════════════════════╝
 *
 * [핀 특성 - 실측 기반]
 * PIN_LR = VRY(35) : 좌우 전담 → 왼쪽=HIGH(4095), 오른쪽=LOW(0)
 * PIN_UD = VRX(32) : 상하 전담 → 위=LOW(0),        아래=HIGH(4095)
 * I2C    : SDA=25, SCL=26 (P1 보드 실측 규격)
 *
 * [소프트웨어 출력 목표]
 * X : 왼쪽=0,  오른쪽=4095  (PIN_LR 반전)
 * Y : 위=0,    아래=4095    (PIN_UD 그대로)
 * 중립 : X=2048, Y=2048
 *
 * [LCD 정책]
 * 파이썬에서 수신한 메시지만 LCD에 표시 (단, 시스템 명령어인 END, SET, SRT, STOP 제외)
 * ESP32 자체 상태(WiFi, 캘리브 등)는 시리얼 모니터에만 출력
 *
 * [LCD 멀티코어 설계]
 * - requestLCD() : Core 0 / Core 1 어디서든 호출 가능 (세마포어 버퍼 복사)
 * - renderLCD()  : 반드시 Core 1(loop())에서만 호출 (I2C 단독 제어)
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// 네트워크 설정
const char* ssid = "사랑채";
const char* password = "tkfkdco!";

IPAddress local_IP(192, 168, 0, 200);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress serverIP(192, 168, 0, 4);

#define SEND_PORT 10001    // P1 -> Python
#define RECEIVE_PORT 10003 // Python -> P2

WiFiUDP udp;

// 핀 정의
#define PIN_LR 33  // 좌우 (VRY)
#define PIN_UD 32  // 상하 (VRX)
#define SW 14      // 버튼
#define SDA 25     // LCD SDA
#define SCL 26     // LCD SCL

LiquidCrystal_I2C lcd(0x27, 16, 2);

// 조이스틱 상수
#define JOY_MID          2048
#define JOY_MAX          4095
#define JOY_THRESH        800
#define SEND_INTERVAL_MS   10
#define CHANGE_THRESH       8
#define FILTER_SIZE         8
#define JOY_DEADZONE      150

// 축별 독립 이동평균 필터
struct AxisFilter {
  int  buf[FILTER_SIZE];
  long sum;
  int  idx;

  void init(int pin) {
    int v = analogRead(pin);
    
    sum = 0;
    for (int i = 0; i < FILTER_SIZE; i++) {
      Serial.println(v);
      buf[i] = v;
      sum += v;
    }
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

// 게임 상태
enum GameState { STATE_END, STATE_SET, STATE_SRT, STATE_STP};
volatile GameState gState = STATE_END;
volatile bool joystickActive = false;
volatile bool resetMenuFlags = false;
volatile int setMenuCursor  = 0;

// 멀티코어 안전 LCD 공유 버퍼 및 세마포어
SemaphoreHandle_t lcdSemaphore = NULL;
volatile bool lcdPending = false;
char lcdBuf0[17];
char lcdBuf1[17];

// 버튼 인터럽트
volatile bool buttonClicked = false;
volatile unsigned long lastInterruptTime = 0;
#define DEBOUNCE_TIME_MS 250

void IRAM_ATTR clickButton() {
  unsigned long now = millis();
  if (now - lastInterruptTime > DEBOUNCE_TIME_MS) {
    buttonClicked = true;
    lastInterruptTime = now;
  }
}

// 타이머
unsigned long tLastSend = 0;
unsigned long tLastDebugPrint = 0;

// 마지막 전송값
int lastSentX = JOY_MID;
int lastSentY = JOY_MID;

TaskHandle_t NetworkRxTask;

// 축 매핑
int mapAxis(int raw, int midVal) {
  if (abs(raw - midVal) <= JOY_DEADZONE) return JOY_MID;
  int mapped;
  if (raw < midVal) {
    mapped = map(raw, 0, midVal - JOY_DEADZONE, 0, JOY_MID);
  }
  else {
    mapped = map(raw, midVal + JOY_DEADZONE, JOY_MAX, JOY_MID, JOY_MAX);
  }
    
  return constrain(mapped, 0, JOY_MAX);
}

int getFilteredX() {
  int raw = filterLR.update(PIN_LR);
  int val = JOY_MAX - mapAxis(raw, RAW_LR_MID);

  if (abs(val - JOY_MID) <= 100) val = JOY_MID;
  return val;
}

int getFilteredY() {
  int raw = filterUD.update(PIN_UD);
  return mapAxis(raw, RAW_UD_MID);
}

// LCD 함수
// Core 0 / Core 1 어디서든 호출 가능한 안전 복사 함수
void requestLCD(const char* text) {
  if (lcdSemaphore == NULL) return;

  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    size_t len = strlen(text);
    if (len <= 16) {
      snprintf(lcdBuf0, 17, "%-16s", text);
      snprintf(lcdBuf1, 17, "%-16s", "");
    }
    else {
      char line0[17], line1[17];
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

void renderLCD() {
  if (!lcdPending || lcdSemaphore == NULL) return;

  char line0[17], line1[17];
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    memcpy(line0, lcdBuf0, 17);
    memcpy(line1, lcdBuf1, 17);
    lcdPending = false;
    xSemaphoreGive(lcdSemaphore);
  }
  else return;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line0);
  lcd.setCursor(0, 1);
  lcd.print(line1);
}

// UDP 송신
void sendUDP(const String& msg) {
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(serverIP, SEND_PORT);
    udp.print(msg);
    udp.endPacket();
  }
  if (msg.indexOf(':') == -1) {
    String logMsg = msg;
    logMsg.trim();
    Serial.print("[UDP 송신] -> ");
    Serial.println(logMsg);
  }
}

// UDP 수신 Task - Core 0
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

        // 수신 문자열 제어
        if (response.indexOf(':') != -1 && response.indexOf("END") != -1) {
          gState = STATE_END;
          joystickActive = false;
          resetMenuFlags = true;
          char part1[10], part2[10], part3[10];
          sscanf(response.c_str(), "%[^:]:%[^:]:%[^:]", part1, part2, part3);
          Serial.println(String("추출 결과: ") + part2);
          requestLCD(("You " + String(part2)).c_str());
          vTaskDelay(1 / portTICK_PERIOD_MS);
          continue;
        } 
        else if (response == "STP") {
          gState = STATE_STP;
          joystickActive = false;
          resetMenuFlags = true;
          vTaskDelay(1 / portTICK_PERIOD_MS);
          requestLCD("READY...");
          continue;
        } 
        else if (response == "END") {
          gState = STATE_END;
          joystickActive = false;
          resetMenuFlags = true;
          vTaskDelay(1 / portTICK_PERIOD_MS);
          requestLCD("P1 CONTROLLER");
          continue;
        } 
        else if (response == "SET") {
          gState = STATE_SET;
          joystickActive = false;
          setMenuCursor  = 0;
          resetMenuFlags = true;
          vTaskDelay(1 / portTICK_PERIOD_MS);
          requestLCD("SETTING");
          continue;
        } 
        else if (response == "SRT") {
          gState = STATE_SRT;
          joystickActive = true;
          vTaskDelay(1 / portTICK_PERIOD_MS);
          requestLCD("MATCH P1 vs P2");
          continue;
        }

        // ── 필터링 조건 (시스템 명령어의 LCD 출력 차단) ──
        if (response == "END" || response == "SET" || response == "SRT" || response == "STOP") {
          vTaskDelay(1 / portTICK_PERIOD_MS);
          continue;
        }
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // 바이너리 세마포어 초기화
  lcdSemaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(lcdSemaphore);

  // I2C LCD 초기화
  Wire.begin(SDA, SCL);
  delay(50);
  lcd.init();
  delay(50);
  lcd.backlight();
  delay(50);
  lcd.clear();
  delay(50);

  // 고정IP 적용 및 네트워크 설정 적용
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  Serial.println("📡 [WiFi] 연결 시도 중...");
  while (WiFi.status() != WL_CONNECTED) delay(200);
  Serial.printf("✅ [WiFi] 연결 완료! IP: %s\n", WiFi.localIP().toString().c_str());

  // 하드웨어 및 인터럽트 구성
  analogReadResolution(12);
  pinMode(SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SW), clickButton, FALLING);

  // 포트 활성화
  udp.begin(RECEIVE_PORT);

  // 조이스틱 정밀 중립 보정 계산
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
  Serial.printf("📐 [캘리브] RAW_LR_MID=%d  RAW_UD_MID=%d\n", RAW_LR_MID, RAW_UD_MID);

  // 필터링 파이프라인 구축 및 워밍업
  filterLR.init(PIN_LR);
  filterUD.init(PIN_UD);
  for (int i = 0; i < FILTER_SIZE * 2; i++) {
    filterLR.update(PIN_LR);
    filterUD.update(PIN_UD);
    delay(2);
  }

  // Core 0 비동기 수신 스레드 기동
  xTaskCreatePinnedToCore(
    networkRxLoop, "NetworkRxTask", 10000, NULL, 1, &NetworkRxTask, 0
  );
  requestLCD("P1 CONTROLLER");
}

void loop() {
  renderLCD(); 
  
  // 인터럽트 최상단 강제 가로채기 및 판정 고도화
  if (buttonClicked) {
    buttonClicked = false;
    if (gState == STATE_END || gState == STATE_SRT) {
      sendUDP("SET\n");
      joystickActive = false;
      setMenuCursor  = 0;
      resetMenuFlags = true;
    }
    else if (gState == STATE_SET) {
      sendUDP("CLK\n");
    }
  }

  // 메뉴 상태 동기화
  static bool lastMenuUp   = false;
  static bool lastMenuDown = false;
  static bool lastUpState  = false;
  if (resetMenuFlags) {
    lastMenuUp = lastMenuDown = lastUpState = false;
    resetMenuFlags = false;
  }

  unsigned long now = millis();

  // 아날로그 데이터 수집 및 매핑
  int cx = getFilteredX();
  int cy = getFilteredY();

  // 1. 메인 메뉴 대기 화면 모드
  if (gState == STATE_END) {
    bool isUp = (cy < JOY_MID - JOY_THRESH);
    if (isUp && !lastUpState) {
      sendUDP("SRT\n");
    }
    lastUpState = isUp;
  }
  // 2. 환경 세팅창 핸들링 모드
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
  // 3. 실시간 게임 세션 루프
  else if (gState == STATE_SRT && joystickActive) {
    if (now - tLastSend >= SEND_INTERVAL_MS) {
      if (abs(cx - lastSentX) > CHANGE_THRESH || abs(cy - lastSentY) > CHANGE_THRESH) {

        char packet[16];
        snprintf(packet, sizeof(packet), "%d:%d\n", cx, cy);
        sendUDP(packet);  // 실시간 원격 패킷 전송

        lastSentX = cx;
        lastSentY = cy;

        Serial.printf("%d:%d\n", cx, cy);
      }
      tLastSend = now;
    }
  }
}
