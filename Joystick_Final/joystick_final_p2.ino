/*
 * ╔══════════════════════════════════════════════════════╗
 * ESP32 핀볼 [P2 보드] - 패들 조이스틱 + LCD (동작 및 인터럽트 픽스)
 * (고정 IP: 192.168.0.154 / Python PC: 192.168.0.167)
 * ╚══════════════════════════════════════════════════════╝
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ─── 네트워크 설정 (P2 고정) ───────────────────────────────
const char* ssid     = "fusion";
const char* password = "12345678";

IPAddress local_IP(192, 168, 0, 154);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress serverIP(192, 168, 0, 167);

const int SEND_PORT    = 10002;   // P2 -> Python (조이스틱/제어)
const int RECEIVE_PORT = 10004;   // Python -> P2 (LCD/상태)

WiFiUDP udp;

// ─── 핀 정의 ────────────────────────────────────────────────
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

// ─── 단발성 ADC 스파이크 제거용 3샘플 중앙값 읽기 (안정화 적용) ────────────
int readAnalogMedian(int pin) {
  int a = analogRead(pin); delayMicroseconds(5);
  int b = analogRead(pin); delayMicroseconds(5);
  int c = analogRead(pin);
  if (a > b) { int t = a; a = b; b = t; }
  if (b > c) { int t = b; b = c; c = t; }
  if (a > b) { int t = a; a = b; b = t; }
  return b; 
}

// ─── 축별 독립 이동평균 필터 ──────────────────────────────
struct AxisFilter {
  int  buf[FILTER_SIZE];
  long sum;
  int  idx;

  void init(int pin) {
    int v = readAnalogMedian(pin);
    sum = 0;
    for (int i = 0; i < FILTER_SIZE; i++) { buf[i] = v; sum += v; }
    idx = 0;
  }

  int update(int pin) {
    sum     -= buf[idx];
    buf[idx] = readAnalogMedian(pin);
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
enum GameState { STATE_END, STATE_SET, STATE_SRT, STATE_STP };
// volatile 명시를 통해 두 코어 간 변수 오염 및 참조 오류를 완벽 차단합니다.
volatile GameState gState         = STATE_END;
volatile bool      joystickActive = false; 
volatile bool      resetMenuFlags = false;

// ─── 멀티코어 안전 LCD 공유 버퍼 및 세마포어 ─────────────────
SemaphoreHandle_t lcdSemaphore = NULL;
volatile bool      lcdPending   = false;
char lcdBuf0[17]  = "                ";
char lcdBuf1[17]  = "                ";

// ─── 버튼 인터럽트 (IRAM 전용 배치) ────────────────────────────────────────
volatile bool          buttonClicked     = false;
volatile unsigned long lastInterruptTime = 0;
#define DEBOUNCE_TIME_MS 250

void IRAM_ATTR clickButton() {
  unsigned long now = millis();
  if (now - lastInterruptTime > DEBOUNCE_TIME_MS) {
    buttonClicked = true;
    lastInterruptTime = now;
  }
}

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
  int x = JOY_MAX - val; 

  if (abs(x - JOY_MID) <= 100) x = JOY_MID;
  return x;
}

int getFilteredY() {
  int raw = filterUD.update(PIN_UD);
  return mapAxis(raw, RAW_UD_MID); 
}

// ════════════════════════════════════════════════════════
//  LCD 함수 (세마포어 동기화)
// ════════════════════════════════════════════════════════
void requestLCD(const char* text) {
  if (lcdSemaphore == NULL) return;

  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    size_t len = strlen(text);
    if (len <= 16) {
      snprintf(lcdBuf0, 17, "%-16s", text);
      snprintf(lcdBuf1, 17, "%-16s", "");
    } else {
      char line0[17]; char line1[17];
      strncpy(line0, text, 16); line0[16] = '\0';
      strncpy(line1, text + 16, 16); line1[16] = '\0';
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
  } else {
    return;
  }
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
  if (msg.indexOf(':') == -1) {
    String logMsg = msg; logMsg.trim();
    Serial.print("[UDP 송신] -> "); Serial.println(logMsg);
  }
}

// ════════════════════════════════════════════════════════
//  UDP 수신 태스크 — Core 0
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

        if (response.indexOf(':') != -1 && response.indexOf("END") != -1) {
          gState         = STATE_END;
          joystickActive = false;
          resetMenuFlags = true;
          char part1[10], part2[10], part3[10];
          sscanf(response.c_str(), "%[^:]:%[^:]:%[^:]", part1, part2, part3);
          Serial.println(String("추출 결과: ") + part3);
          requestLCD(("You " + String(part3)).c_str());
          vTaskDelay(1 / portTICK_PERIOD_MS);
          continue;
        } 
        // ── 상태 핸들링 및 타이밍 동기화 ──────────────────
        else if (response == "END") {
          gState         = STATE_END;
          joystickActive = false;
          resetMenuFlags = true;
          requestLCD("MAIN MENU");
          continue;
        }
        else if(response == "STP")
        {
          gState         = STATE_END;
          joystickActive = false;
          resetMenuFlags = true;
          requestLCD("READY...");
          continue;
        }
        else if (response == "SET") {
          gState         = STATE_SET;
          joystickActive = false;
          resetMenuFlags = true;
          requestLCD("SETTING");
          continue;          
        }
        else if (response == "SRT") {
          gState         = STATE_SRT;
          joystickActive = true;
          resetMenuFlags = true;
          requestLCD("MATCH P1 vs P2");
          continue;          
        }
        

        // ── 필터링 조건 (시스템 명령어의 LCD 출력 차단) ──
        if (response == "END" || response == "SET" || response == "SRT" || response == "STOP") {
          vTaskDelay(1 / portTICK_PERIOD_MS);
          continue;
        }

        requestLCD(response.c_str());
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

  lcdSemaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(lcdSemaphore);

  // I2C 핀 레이아웃 (SDA=25, SCL=26 구조)
  Wire.begin(25, 26);
  delay(50);
  lcd.init();
  delay(50);
  lcd.backlight();
  delay(50);
  lcd.clear();
  delay(50);

  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  Serial.println("📡 [WiFi] 연결 시도 중...");
  while (WiFi.status() != WL_CONNECTED) { delay(200); }
  Serial.printf("✅ [WiFi] 연결 완료! IP: %s\n", WiFi.localIP().toString().c_str());

  analogReadResolution(12);
  pinMode(SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SW), clickButton, FALLING);

  udp.begin(RECEIVE_PORT);

  Serial.println("📐 [캘리브] 조이스틱 중립값 측정 중...");
  const int CAL_SAMPLES = 64;
  long sumLR = 0, sumUD = 0;
  for (int i = 0; i < CAL_SAMPLES; i++) {
    sumLR += readAnalogMedian(PIN_LR);
    sumUD += analogRead(PIN_UD);
    delay(3);
  }
  RAW_LR_MID = (int)(sumLR / CAL_SAMPLES);
  RAW_UD_MID = (int)(sumUD / CAL_SAMPLES);
  Serial.printf("📐 [캘리브] RAW_LR_MID=%d  RAW_UD_MID=%d\n", RAW_LR_MID, RAW_UD_MID);

  filterLR.init(PIN_LR);
  filterUD.init(PIN_UD);
  for (int i = 0; i < FILTER_SIZE * 2; i++) {
    filterLR.update(PIN_LR);
    filterUD.update(PIN_UD);
    delay(2);
  }

  xTaskCreatePinnedToCore(
    networkRxLoop, "NetworkRxTask",
    10000, NULL, 1, &NetworkRxTask, 0
  );

  
}

// ════════════════════════════════════════════════════════
//  loop() — Core 1
// ════════════════════════════════════════════════════════
void loop() {
  renderLCD();

  // ── [수정] 인터럽트 최상단 강제 가로채기 및 판정 고도화 ──
  if (buttonClicked) {
    buttonClicked = false;
    if (gState == STATE_SRT) {
      Serial.println("🚨 [인터럽트 발생] P2 강제 강등 시도 -> END 전송");
      sendUDP("END\n");
      gState         = STATE_END;
      joystickActive = false;
      resetMenuFlags = true;
    }
  }

  static bool lastUpState = false;
  if (resetMenuFlags) {
    lastUpState    = false;
    resetMenuFlags = false;
    lastSentX      = JOY_MID;
    lastSentY      = JOY_MID;
  }

  unsigned long now = millis();

  int cx = getFilteredX();
  int cy = getFilteredY();

  // ── 상태별 동작 분기 ──────────────────────────────────────

  // 1. 메인 메뉴 대기 화면 모드
  if (gState == STATE_END) {
    bool isUp = (cy < JOY_MID - JOY_THRESH);
    if (isUp && !lastUpState) {
      sendUDP("SRT\n");
    }
    lastUpState = isUp;
  }
  // 2. 설정창 모드 - P2는 철저히 먹통 대기
  else if (gState == STATE_SET || gState == STATE_STP) {
    // 무시 패스
  }
  // 3. 실시간 게임 세션 루프 (STATE_SRT 플래그 동기화 구동 조건 매핑)
  else if (gState == STATE_SRT && joystickActive) {
    if (now - tLastSend >= SEND_INTERVAL_MS) {
      if (abs(cx - lastSentX) > CHANGE_THRESH ||
          abs(cy - lastSentY) > CHANGE_THRESH) {

        char packet[16];
        snprintf(packet, sizeof(packet), "%d:%d\n", cx, cy);
        sendUDP(packet); 

        lastSentX = cx;
        lastSentY = cy;

        Serial.printf("%d:%d\n", cx, cy);
      }
      tLastSend = now;
    }
  }
}

