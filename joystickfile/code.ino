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

#include <WiFi.h>                                              //#include ...: 와이파이 연결(WiFi), UDP 통신(WiFiUdp), I2C 통신(Wire), I2C LCD 제어(LiquidCrystal_I2C)를 위한 외부 라이브러리들을 불러옵니다.
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>                                                                                            

// ─── 네트워크 설정 (P1 전용 규격) ───────────────────────────
const char* ssid     = "fusion";                             //ssid, password: 접속할 와이파이 공유기(AP)의 이름과 비밀번호입니다.                           
const char* password = "12345678";

IPAddress local_IP(192, 168, 0, 200);                       //local_IP: 이 ESP32 보드가 사용할 고정 IP 주소입니다.
IPAddress gateway(192, 168, 0, 1);                        //local_IP: gateway, subnet: 공유기 게이트웨이 주소와 서브넷 마스크 설정입니다.
IPAddress subnet(255, 255, 255, 0);                      //serverIP: 데이터를 주고받을 파이썬 프로그램이 켜져 있는 PC의 IP 주소입니다.
IPAddress serverIP(192, 168, 0, 167);

const int SEND_PORT    = 10001;                       //SEND_PORT: 파이썬 PC로 데이터를 보낼 포트 번호입니다.
const int RECEIVE_PORT = 10003;                      //RECEIVE_PORT: 파이썬 PC로부터 데이터를 받을 포트 번호입니다.
 
WiFiUDP udp;                                             //udp: UDP 통신 기능을 수행할 객체를 선언합니다.

// ─── 핀 정의 (P1 전용 규격) ─────────────────────────────────
const int PIN_LR = 35;   // 좌우 (VRY)                //PIN_LR, PIN_UD, SW: 하드웨어 핀 번호 정의입니다. 35번 핀은 좌우, 32번 핀은 상하 아날로그 신호를 읽고, 14번 핀은 조이스틱 클릭 버튼 신호를 받습니다.
const int PIN_UD = 32;   // 상하 (VRX)              
const int SW     = 14;   // 버튼                      

LiquidCrystal_I2C lcd(0x27, 16, 2);                     //lcd(...): 주소값이 0x27이고 크기가 16열 2행(16x2)인 I2C LCD 객체를 생성합니다.

// ─── 조이스틱 상수 ────────────────────────────────────────
#define JOY_MID          2048                   //JOY_MID, JOY_MAX: 12비트 ADC 기준 중립값(2048)과 최댓값(4095)입니다.
#define JOY_MAX          4095
#define JOY_THRESH        800                 //JOY_THRESH: 메뉴 이동 시 스틱을 꺾었다고 판정할 문턱값(800)입니다.
#define SEND_INTERVAL_MS   10             //SEND_INTERVAL_MS: 조이스틱 데이터를 전송하는 최소 주기(10ms)입니다.
#define CHANGE_THRESH       8              //CHANGE_THRESH: 조이스틱 값이 최소 이 수치(8) 이상 변했을 때만 패킷을 전송하여 불필요한 트래픽을 방지합니다.
#define FILTER_SIZE         8                  //FILTER_SIZE: 노이즈 제거를 위한 이동평균 필터의 데이터 개수(8개)입니다.
#define JOY_DEADZONE      150             //JOY_DEADZONE: 미세한 흔들림에 반응하지 않도록 설정한 중심부 데드존(정밀 중립 범위, 150)입니다.

// ─── 축별 독립 이동평균 필터 ──────────────────────────────
struct AxisFilter {                                //struct AxisFilter: 조이스틱의 아날로그 값 흔들림을 잡아주기 위한 이동평균 필터 구조체 정의입니다. 최근 8개의 데이터를 저장할 배열(buf), 총합(sum), 현재 인덱스(idx)를 가집니다.
  int  buf[FILTER_SIZE];
  long sum;
  int  idx;

  void init(int pin) {                           //init(...): 필터 초기화 함수입니다. 처음 센서 값을 읽어와 버퍼 8개를 모두 채우고 합계를 구합니다.
    int v = analogRead(pin);
    sum = 0;
    for (int i = 0; i < FILTER_SIZE; i++) {
      buf[i] = v;
      sum += v;
    }
    idx = 0;
  }

  int update(int pin) {                          //update(...): 새로운 센서 값을 읽을 때마다 가장 오래된 값을 빼고 새 값을 더해 평균을 내는 핵심 필터링 함수입니다. 인덱스는 0~7을 순환합니다.
    sum     -= buf[idx];
    buf[idx] = analogRead(pin);
    sum     += buf[idx];
    idx      = (idx + 1) % FILTER_SIZE;
    return (int)(sum / FILTER_SIZE);
  }
};

AxisFilter filterLR;                                //filterLR, filterUD: 좌우축과 상하축을 각각 필터링할 독립된 필터 객체를 생성합니다.
AxisFilter filterUD;

int RAW_LR_MID = 2048;                    //RAW_LR_MID, RAW_UD_MID: 전원 결합 시 측정할 실제 조이스틱 하드웨어의 중립 오차 값 저장용 변수입니다.
int RAW_UD_MID = 2048;

// ─── 게임 상태 ────────────────────────────────────────────
enum GameState { STATE_END, STATE_SET, STATE_SRT, STATE_STP};              //enum GameState ...: 게임의 상태 종류(종료/대기, 세팅, 시작/플레이, 일시정지)를 정의합니다.
volatile GameState gState         = STATE_END;                                           //gState = STATE_END: 현재 게임 상태 변수이며 초기값은 대기(END) 상태입니다. (volatile은 멀티코어 간 데이터 왜곡 방지용 키워드)
volatile bool      joystickActive = false; // score the goal! 수신 시 true 전환      //joystickActive: 실제 게임 중 조이스틱 조작 패킷을 전송할지 여부를 결정하는 플래그입니다.
volatile int       setMenuCursor  = 0;                                                         //setMenuCursor: 세팅 메뉴에서 커서의 위치를 나타냅니다.
volatile bool      resetMenuFlags = false;                                                   //resetMenuFlags: 상태가 바뀔 때 메뉴 조작 상태를 깨끗이 초기화하기 위한 플래그입니다.

// ─── 멀티코어 안전 LCD 공유 버퍼 및 세마포어 ─────────────────
SemaphoreHandle_t lcdSemaphore = NULL;                                            // lcdSemaphore: 멀티코어 환경에서 2개의 코어가 동시에 LCD 버퍼에 접근하다가 글자가 깨지는 것을 막아주는 '열쇠(세마포어)' 변수입니다.
volatile bool      lcdPending   = false;                                                    // lcdPending: LCD에 새로 출력할 내용이 대기 중인지 나타내는 플래그입니다.
char lcdBuf0[17]  = "                ";                                                       // lcdBuf0, lcdBuf1: LCD 첫 번째 줄과 두 번째 줄에 출력할 문자열을 임시 저장하는 안전 버퍼입니다. (16칸 + 널문자 1칸 = 17)
char lcdBuf1[17]  = "                ";                                                      

// ─── 버튼 인터럽트 ────────────────────────────────────────
volatile bool         buttonClicked     = false;                                    //buttonClicked: 버튼이 눌렸음을 루프에 알리는 플래그입니다.
volatile unsigned long lastInterruptTime = 0;                                   //lastInterruptTime, DEBOUNCE_TIME_MS: 버튼을 한 번 눌렀을 때 미세하게 여러 번 눌린 것으로 인식되는 하드웨어 노이즈(바운싱)를 250ms 동안 무시하기 위한 디바운스 변수입니다.
#define DEBOUNCE_TIME_MS 250

// ─── 타이머 ──────────── ───────────────────────────────────
unsigned long tLastSend       = 0;                                              //tLastSend 등: 데이터 전송 주기 계산 등을 위한 시간 기록용 변수들입니다.
unsigned long tLastDebugPrint = 0;

// ─── 마지막 전송값 ────────────────────────────────────────
int lastSentX = JOY_MID;                                                      //lastSentX, lastSentY: 직전에 파이썬 서버로 전송했던 X, Y 좌표값을 기억하여 변화가 있을 때만 전송하도록 비교하는 변수입니다.
int lastSentY = JOY_MID;

TaskHandle_t NetworkRxTask;                                               //NetworkRxTask: Core 0에서 무한 루프로 돌아갈 UDP 수신 태스크(스레드) 핸들러입니다.

// ════════════════════════════════════════════════════════
//  축 매핑
// ════════════════════════════════════════════════════════
int mapAxis(int raw, int midVal) {                                                                            //mapAxis(...): 조이스틱의 원본 아날로그 값(raw)을 입력받아 정밀 보정하는 함수입니다.                                                                                                                
  if (abs(raw - midVal) <= JOY_DEADZONE) return JOY_MID;                                            //1.실제 중립값(midVal) 기준 데드존 범위 내에 있으면 완벽한 중립값(2048)을 반환합니다.
  int mapped;                                                                                                 //2.중립보다 작으면 0 ~ (중립-데드존) 구간을 0 ~ 2048 수치로 정밀 비례 매핑합니다.
  if (raw < midVal)                                                                                           //3.중립보다 크면 (중립+데드존) ~ 4095 구간을 2048 ~ 4095 수치로 매핑합니다.
    mapped = map(raw, 0, midVal - JOY_DEADZONE, 0, JOY_MID);                                   //4.범위가 0~4095를 벗어나지 않도록 강제 제한(constrain)합니다.                              
  else
    mapped = map(raw, midVal + JOY_DEADZONE, JOY_MAX, JOY_MID, JOY_MAX);
  return constrain(mapped, 0, JOY_MAX);
}

int getFilteredX() {        //getFilteredX(): 좌우 값을 읽어 필터링하고 매핑합니다. 하드웨어 특성상 왼쪽이 HIGH고 오른쪽이 LOW이므로 소프트웨어 요구 규격(오른쪽이 4095)에 맞추기 위해 값을 반전(JOY_MAX - val) 시켜 반환합니다.
  int raw = filterLR.update(PIN_LR);
  int val = mapAxis(raw, RAW_LR_MID);
  return JOY_MAX - val; // P1 소프트웨어 타겟 반전 매핑 완료
}

int getFilteredY() {                                            //getFilteredY(): 상하 값을 읽어 필터링 및 매핑한 후 정방향 그대로 반환합니다 (위=0, 아래=4095).
  int raw = filterUD.update(PIN_UD);
  return mapAxis(raw, RAW_UD_MID); // P1 소프트웨어 타겟 정방향 매핑 완료
}

// ════════════════════════════════════════════════════════
//  LCD 함수 (P2 구조 이식 - 세마포어 동기화)
// ════════════════════════════════════════════════════════

// Core 0 / Core 1 어디서든 호출 가능한 안전 복사 함수       //requestLCD(...): 어떤 코어에서든 안전하게 LCD 출력을 요청하는 함수입니다. 세마포어가 없으면 그냥 시리얼에 줄을 긋고 종료합니다.
void requestLCD(const char* text) {
  if (lcdSemaphore == NULL) {
    Serial.println("text ---------------");
    return;
  }

  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {          //xSemaphoreTake(...): 다른 코어가 LCD 버퍼를 쓰고 있는지 확인하고 100ms 동안 열쇠를 획득하려고 시도합니다. 열쇠를 얻으면(pdTRUE) 텍스트 길이를 재고 시리얼에 출력합니다.
    size_t len = strlen(text);
    Serial.println(text);

    if (len <= 16) {                                     //if (len <= 16) ... else ...:
      snprintf(lcdBuf0, 17, "%-16s", text);       //1 글자 수가 16자 이하면 첫 줄(lcdBuf0)에 왼쪽 정렬 후 나머지를 빈칸으로 채우고 둘째 줄은 비웁니다.
      snprintf(lcdBuf1, 17, "%-16s", "");         //2 16자를 초과하면 글자를 반으로 쪼개어 첫 16자는 첫 줄에, 나머지는 둘째 줄(lcdBuf1)에 나누어 담습니다.
    } else {
      char line0[17], line1[17];
      strncpy(line0, text, 16);
      line0[16] = '\0';
      strncpy(line1, text + 16, 16);
      line1[16] = '\0';
      snprintf(lcdBuf0, 17, "%-16s", line0);
      snprintf(lcdBuf1, 17, "%-16s", line1);
    }

    lcdPending = true;                          //lcdPending = true, xSemaphoreGive(...): 새 데이터가 들어왔음을 표시하고, 다 썼으므로 들고 있던 세마포어 열쇠를 반환합니다.
    xSemaphoreGive(lcdSemaphore);
  }
}

void renderLCD() {                                //renderLCD(): 이 함수는 반드시 Core 1(loop 함수)에서만 주기적으로 실행됩니다.
  if (!lcdPending) return;                        //1 lcdPending이 거짓(새 글자 없음)이면 바로 종료합니다.
  if (lcdSemaphore == NULL) return;       //2 열쇠를 받아와서 안전 버퍼의 내용을 로컬 변수(line0, line1)로 순식간에 복사(memcpy)해 옵니다.
                                                        //3 복사가 끝나면 즉시 열쇠를 돌려줍니다. (I2C 전송 시간 동안 다른 코어가 멈추는 걸 방지하기 위함)
  char line0[17], line1[17];
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    memcpy(line0, lcdBuf0, 17);
    memcpy(line1, lcdBuf1, 17);
    lcdPending = false;
    xSemaphoreGive(lcdSemaphore);
  } else {
    return;
  }

  lcd.clear();                                      //lcd.print(...): 복사해 온 문자열을 실제 하드웨어 LCD 화면에 깔끔하게 출력합니다.
  lcd.setCursor(0, 0); lcd.print(line0);
  lcd.setCursor(0, 1); lcd.print(line1);
}

// ════════════════════════════════════════════════════════
//  UDP 송신
// ════════════════════════════════════════════════════════
void sendUDP(const String& msg) {                     //sendUDP(...): 와이파이가 연결되어 있다면 지정된 파이썬 PC IP와 포트로 문자열 데이터를 전송합니다.
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(serverIP, SEND_PORT);
    udp.print(msg);
    udp.endPacket();
  }
  if (msg.indexOf(':') == -1) {                                //if (msg.indexOf(':') == -1) ...: 매 순간 보내는 실시간 조이스틱 좌표(X:Y) 데이터가 아니라 시스템 명령어(예: SRT, SET 등)일 때만 시리얼 모니터에 로그를 남겨 가독성을 높입니다.
    String logMsg = msg;
    logMsg.trim();
    Serial.print("[UDP 송신] -> ");
    Serial.println(logMsg);
  }
}

// ════════════════════════════════════════════════════════
//  버튼 인터럽트 (IRAM)
// ════════════════════════════════════════════════════════
void IRAM_ATTR clickButton() {                                            //clickButton(): 조이스틱 버튼이 눌렸을 때 실행되는 인터럽트 서비스 루틴(ISR)입니다.
  unsigned long t = millis();                                                //IRAM_ATTR 속성 덕분에 매우 빠르게 실행되며 디바운스 조건(250ms 경과)을 충족하면
  if (t - lastInterruptTime > DEBOUNCE_TIME_MS) {                //buttonClicked 플래그를 참으로 만듭니다
    buttonClicked     = true;
    lastInterruptTime = t;
  }
}

// ════════════════════════════════════════════════════════
//  UDP 수신 태스크 — Core 0 (구조 정렬 완료)
// ════════════════════════════════════════════════════════
void networkRxLoop(void* pvParameters) {                     //networkRxLoop(...): Core 0에서 독자적으로 무한 궤도를 도는 함수입니다. 파이썬 서버로부터 수신된 UDP 패킷이 있는지 상시 감시하고,
  char rxBuffer[255];                                                   //데이터가 있다면 읽어서 response 문자열 변수에 저장하고 공백을 제거합니다.
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      int packetSize = udp.parsePacket();
      if (packetSize) {
        int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
        if (len > 0) rxBuffer[len] = '\0';

        String response = String(rxBuffer);
        response.trim();

        Serial.print("📥 [수신] <- [");                     //Serial.print(...): 수신한 원본 메시지를 시리얼 모니터에 출력합니다.
        Serial.print(response);
        Serial.println("]");

        if (response.indexOf(':') != -1 && response.indexOf("END") != -1) {       //if (... "END" ...): 만약 패킷에 콜론(:)과 END가 포함되어 있다면(예: score:WIN:END 혹은 score:LOSE:END 형태), 게임 종료로 판단합니다
          gState         = STATE_END;                                                          //상태를 STATE_END로 바꾸고 조이스틱을 비활성화하며 가운데 결과 단어(part2, 예: WIN/LOSE)를
          joystickActive = false;                                                                  //추출하여 LCD에 "You WIN" 형태로 출력한 뒤 다음 루프로 넘어갑니다.
          resetMenuFlags = true;
          char part1[10], part2[10], part3[10];
          sscanf(response.c_str(), "%[^:]:%[^:]:%[^:]", part1, part2, part3);
          Serial.println(String("추출 결과: ") + part2);
          requestLCD(("You " + String(part2)).c_str());
          vTaskDelay(1 / portTICK_PERIOD_MS);
          continue;
        } 
        else if (response == "STP") {                                                   //else if (response == "STP"): 파이썬에서 STP(정지/메뉴) 명령이 왔다면 일시 정지(혹은 메뉴 복귀) 상태로 전환하고 LCD에 "MAIN MENU"를 띄웁니다.
          gState         = STATE_STP;
          joystickActive = false;
          resetMenuFlags = true;
          vTaskDelay(1 / portTICK_PERIOD_MS);
          requestLCD("MAIN MENU");
          continue;
        } 
        else if (response == "END") {                                             //else if (response == "END"): 단독으로 END 명령이 왔다면 완전 초기 대기 상태로 전환하고 LCD에 "READY..."를 띄웁니다.
          gState         = STATE_END;
          joystickActive = false;
          resetMenuFlags = true;
          vTaskDelay(1 / portTICK_PERIOD_MS);
          requestLCD("READY...");
          continue;
        } 
        else if (response == "SET") {                                         //else if (response == "SET"): 파이썬에서 SET 명령이 오면 세팅 모드(STATE_SET)로 전환하고 LCD에 "SETTING"을 표시합니다.
          gState         = STATE_SET;
          joystickActive = false;
          setMenuCursor  = 0;
          resetMenuFlags = true;
          requestLCD("SET");
          vTaskDelay(1 / portTICK_PERIOD_MS);
          requestLCD("SETTING");
          continue;
        } 
        else if (response == "SRT") {                            //else if (response == "SRT"): 파이썬에서 SRT(시작) 명령이 오면 실시간 플레이 상태(STATE_SRT)로 바꾸고 조이스틱 전송 플래그를
          gState         = STATE_SRT;                           //true로 켭니다. LCD에는 대전 문구("MATCH P1 vs p2")를 출력합니다.
          joystickActive = true;
          requestLCD("SRT");
          vTaskDelay(1 / portTICK_PERIOD_MS);
          requestLCD("MATCH P1 vs p2");
          continue;
        }

        // 위 조건에 해당 없는 메시지만 여기 도달                         
        Serial.println("RESPONSE: " + response);                        // 위 조건에 해당 없는 메시지만 ...: 특수 제어 명령(END, SET, SRT, STP 등)을 제외하고 파이썬 서버가 순수하게 보낸 일반 텍스트 메시지
        requestLCD(response.c_str());                                       //(예: 점수, 카운트다운 숫자 등)만 차별화하여 LCD 화면에 그대로 보여줍니다. vTaskDelay는 watchdog 타이머 리셋을 위한 필수 양보 시간입니다.
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

// ════════════════════════════════════════════════════════
//  setup() — 최상위 레벨로 포지션 복구 및 라인 정렬 완료
// ════════════════════════════════════════════════════════
void setup() {                                       //setup(): 프로그램 시작 시 딱 한 번 실행되는 초기화 구역입니다. 시리얼 통신 속도를 115200bps로 엽니다.
  Serial.begin(115200);
  delay(100);

  // ① 바이너리 세마포어 초기화
  lcdSemaphore = xSemaphoreCreateBinary();                            //lcdSemaphore = ...: 멀티코어 방어용 이진 세마포어를 생성하고, 최초로 사용할 수 있도록 열쇠를 한 번 풀어놓습니다(xSemaphoreGive).
  xSemaphoreGive(lcdSemaphore);

  // ② I2C LCD 초기화 (SDA=25, SCL=26 구조 반영)
  Wire.begin(25, 26);                                                    //Wire.begin(25, 26): P1 보드 실측 규격에 맞춰 SDA=25번, SCL=26번 핀으로 I2C 통신을 시작하고 LCD 화면을 켜고 백라이트를 활성화한 뒤 내용을 지웁니다.
  delay(50);
  lcd.init();
  delay(50);
  lcd.backlight();
  delay(50);
  lcd.clear();
  delay(50);

  // ③ 고정 IP 및 네트워크 설정 적용
  WiFi.config(local_IP, gateway, subnet);                            //WiFi.config(...): 앞서 선언한 고정 IP 환경을 주입하고 와이파이에 연결될 때까지 0.2초 간격으로 대기하며 연결 성공 시 IP를 출력합니다.
  WiFi.begin(ssid, password);
  Serial.println("📡 [WiFi] 연결 시도 중...");
  while (WiFi.status() != WL_CONNECTED) { delay(200); }
  Serial.printf("✅ [WiFi] 연결 완료! IP: %s\n", WiFi.localIP().toString().c_str());

  // ④ 하드웨어 및 인터럽트 구성
  analogReadResolution(12);                                                          //analogReadResolution(12): 아날로그 값을 정밀하게 읽기 위해 해상도를 12비트(0~4095)로 설정합니다.
  pinMode(SW, INPUT_PULLUP);                                                    //attachInterrupt(...): 버튼(SW) 핀을 내부 풀업 모드로 열고, 버튼을 누를 때(전압이 높은 곳에서 평지로 떨어질 때, FALLING) 인터럽트 함수(clickButton)가 바로 튀어 나가도록 연결합니다.
  attachInterrupt(digitalPinToInterrupt(SW), clickButton, FALLING);

  // ⑤ 포트 활성화
  udp.begin(RECEIVE_PORT);                                               //udp.begin(...): 설정한 포트 수신용 UDP 통신을 개시합니다.

  // ⑥ 조이스틱 정밀 중립 보정 계산
  Serial.println("📐 [캘리브] 조이스틱 중립값 측정 중...");            //📐 [캘리브] ...: 하드웨어마다 미세하게 조이스틱이 휘어있는 오차를 잡기 위해, 전원이 켜지는 순간 아무도 스틱을 만지지 않는 상태에서
  const int CAL_SAMPLES = 64;                                            //조이스틱 아날로그 값을 64번 연속으로 읽어 정밀한 중간값 평균(오차 보정 중심점)을 구해냅니다.
  long sumLR = 0, sumUD = 0; 
  for (int i = 0; i < CAL_SAMPLES; i++) {
    sumLR += analogRead(PIN_LR);
    sumUD += analogRead(PIN_UD);
    delay(3);
  }
  RAW_LR_MID = (int)(sumLR / CAL_SAMPLES);
  RAW_UD_MID = (int)(sumUD / CAL_SAMPLES);
  Serial.printf("📐 [캘리브] RAW_LR_MID=%d  RAW_UD_MID=%d\n", RAW_LR_MID, RAW_UD_MID);

  // ⑦ 필터링 파이프라인 구축 및 워밍업
  filterLR.init(PIN_LR);                                                 //filterLR.init(...): 조이스틱 이동평균 필터를 초기화하고,
  filterUD.init(PIN_UD);                                              //필터 연산 버퍼를 채우기 위해 루프 진입 전 16번 미리 업데이트(워밍업) 시켜줍니다.
  for (int i = 0; i < FILTER_SIZE * 2; i++) {
    filterLR.update(PIN_LR);
    filterUD.update(PIN_UD);
    delay(2);
  }

  // ⑧ Core 0 비동기 수신 스레드 기동
  xTaskCreatePinnedToCore(                                             //xTaskCreatePinnedToCore(...): ESP32 멀티코어의 핵심 구동문입니다. 수신 전용 함수인 networkRxLoop를 스택 크기 10000,
    networkRxLoop, "NetworkRxTask",                                //우선순위 1로 설정하여 명확하게 Core 0(맨 마지막 인자 0)에 붙여서 독립 기동시킵니다. 이로 인해 메인 루프와 완벽히 병렬 처리됩니다.
    10000, NULL, 1, &NetworkRxTask, 0
  );

  Serial.println("🎮 [시스템] 초기화 완료 - 파이썬 메시지 대기 중");             //sendUDP("END\n"): 하드웨어 켜짐 완료 시 파이썬 서버에게 "나 켜졌으니 초기 대기 상태로 세팅해줘"라는 신호로 END 패킷을 전송하며 끝냅니다.

  // ⭐ 파이썬 서버 동기화 패킷 발송 규칙 유지
  delay(100);
  sendUDP("END\n");
}

// ════════════════════════════════════════════════════════
//  loop() — 최상위 레벨로 포지션 복구 및 라인 정렬 완료
// ════════════════════════════════════════════════════════
void loop() {                                                //loop(): 메인 루프이며 자동으로 Core 1에서 무한 반복 구동됩니다. 진입 즉시 수신 코어(Core 0)가 남겨준 문자가 있다면 LCD 화면에 그리는 renderLCD()를 처리합니다.
  renderLCD(); 
  
  // ── 버튼 처리 ──────────────────────────────────────────
  if (buttonClicked) {                                                      //if (buttonClicked) ...: 인터럽트 플래그에 의해 버튼 클릭이 감지되었을 때의 처리입니다.
    buttonClicked = false;                                               //현재 게임 대기(STATE_END) 중이거나 게임 플레이(STATE_SRT) 중일 때 누르면 파이썬 서버에 세팅창으로 가겠다는 "SET\n" 패킷을 날리고 상태 관련 변수들을 초기화합니다.
    if (gState == STATE_END || gState == STATE_SRT) {       //만약 이미 세팅창(STATE_SET) 상태라면 현재 고른 메뉴를 선택하겠다는 클릭 명령인 "CLK\n" 패킷을 파이썬 서버로 보냅니다.
      sendUDP("SET\n");
      joystickActive = false;
      setMenuCursor  = 0;
      resetMenuFlags = true;
      requestLCD("");
    } else if (gState == STATE_SET) {
      sendUDP("CLK\n");
    }
  }

  // ── 메뉴 상태 관리 동기화 리셋 플래그 ──────────────────────────
  static bool lastMenuUp   = false;                              //static bool ...: 조이스틱을 꾹 밀고 있을 때 명령어가 무한대로 연사 전송되는 것을 막기 위해
  static bool lastMenuDown = false;                           //직전 조이스틱 꺾임 상태'를 기억하는 정적(static) 변수들입니다. 상태 리셋 플래그가 참이면 모두 거짓으로 초기화합니다.
  static bool lastUpState  = false;
  if (resetMenuFlags) {
    lastMenuUp = lastMenuDown = lastUpState = false;
    resetMenuFlags = false;
  }

  unsigned long now = millis();

  // 아날로그 데이터 수집 및 매핑
  int cx = getFilteredX();                                      //cx, cy: 필터링과 오차 보정, 하드웨어 반전 매핑이 완벽하게 끝난 최종 X좌표와 Y좌표를 변수에 저장합니다 (0~4095 범위).
  int cy = getFilteredY();

  // ── P1 코어 기능에 기한 동작 분기 ──────────────────────────────

  // 1. 메인 메뉴 대기 화면 모드
  if (gState == STATE_END) {                                //if (gState == STATE_END): 대기 상태일 때, 조이스틱을 위로 꺾었는지(cy < 2048 - 800) 확인합니다.
    bool isUp = (cy < JOY_MID - JOY_THRESH);       //위로 탁 꺾는 순간 단 한 번 파이썬 서버에 게임을 시작하겠다는 의미인 "SRT\n" 패킷을 전송합니다.

    if (isUp && !lastUpState) {
      sendUDP("SRT\n");
    }
    lastUpState = isUp;
  }

  // 2. 환경 세팅창 핸들링 모드
  else if (gState == STATE_SET) {                           //else if (gState == STATE_SET): 환경 세팅창 상태일 때의 조작입니다.
    bool isUp   = (cy < JOY_MID - JOY_THRESH);      //조이스틱을 위로 밀면 파이썬 서버에 위로 커서를 올린다는 의미의 "UP\n" 패킷을 보내고 내부 커서 메모리를 0으로 잡습니다.
    bool isDown = (cy > JOY_MID + JOY_THRESH);    //조이스틱을 아래로 밀면 아래 커서 이동 의미의 "DN\n" 패킷을 전송하고 내부 커서 메모리를 1로 잡습니다. 중복 연사 방지 로직이 적용되어 있습니다.
    (gState  == STATE_STP);

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
  else if (gState == STATE_SRT && joystickActive) {                                                              //else if (gState == STATE_SRT && joystickActive): 실제 게임 플레이 중일 때 실행되는 가장 핵심적인 실시간 전송 루프입니다.
    if (now - tLastSend >= SEND_INTERVAL_MS) {                                                               //1.최소 전송 주기 규칙(10ms)을 충족했는지 검사합니다.
      if (abs(cx - lastSentX) > CHANGE_THRESH || abs(cy - lastSentY) > CHANGE_THRESH) {      //2.필터링된 현재 좌표(cx, cy)가 직전에 보냈던 좌표(lastSentX, lastSentY)에 비해 문턱값(CHANGE_THRESH, 즉 8) 이상 움직였는지 검사하여 미세 노이즈 전송을 컷트합니다.
                                                                                                                                //3. 조건을 만족하면 X좌표:Y좌표\n (예: 2048:4095\n) 형태로 버퍼 문자열을 만들어 파이썬 PC로 초고속 UDP 패킷 전송을 수행하고 직전 전송 값을 업데이트합니다.
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