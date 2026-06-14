/*
 * ╔══════════════════════════════════════════════════════╗
 * ESP32 핀볼 [P2 게스트 보드] - LCD 멀티코어 및 카운트다운 완벽 수정본
 * (고정 IP: 192.168.0.154 / Python PC: 192.168.0.138)
 * ╚══════════════════════════════════════════════════════╝
 *
 * [하드웨어 핀 제어 특성]
 * - PIN_LR = VRY(35) : 조이스틱 좌우 제어 (왼쪽: 4095 / 오른쪽: 0)
 * - PIN_UD = VRX(32) : 조이스틱 상하 제어 (위: 0 / 아래: 4095)
 * - I2C 통신선       : SDA(25), SCL(26)
 */

#include <WiFi.h>              // 와이파이 안테나를 켜는 부품
#include <WiFiUdp.h>           // 편지(데이터)를 무선으로 초고속 발송하는 우체부 부품
#include <Wire.h>              // 화면(LCD)과 대화하기 위한 길을 터주는 부품
#include <LiquidCrystal_I2C.h> // 글자를 보여주는 LCD 화면 제어 부품

// 우리 강의실/실습실의 와이파이 이름과 비밀번호
const char* ssid     = "fusion";
const char* password = "12345678";

// 네트워크 고정 IP 설정 (이 보드의 고유 무선 주소)
IPAddress local_IP(192, 168, 0, 200); // 본인(P1 보드)의 방 번호
IPAddress gateway(192, 168, 0, 1);    // 공유기(문지기) 주소
IPAddress subnet(255, 255, 255, 0);   // 통신망 규격 정보
IPAddress serverIP(192, 168, 0, 138); // 게임 화면이 켜져 있는 메인 파이썬 컴퓨터 주소

// 2번 보드와 데이터가 뒤섞이지 않도록 통로(포트)를 분리
const int SEND_PORT    = 10001;   // 조이스틱 움직임을 컴퓨터로 보내는 출구 포트
const int RECEIVE_PORT = 10003;   // 컴퓨터가 보낸 게임 점수를 받아오는 입구 포트

WiFiUDP udp; // 무선 편지를 주고받을 우체부 객체

// 하드웨어 핀 번호 (실제 꽂아둔 구멍 기준)
const int PIN_LR = 35;   // 조이스틱 왼쪽/오른쪽 움직임 감지 센서 구멍
const int PIN_UD = 32;   // 조이스틱 위쪽/아래쪽 움직임 감지 센서 구멍
const int SW     = 14;   // 조이스틱을 꾹 누르면 딸깍하는 버튼 구멍

// 0x27 주소에 있는 16글자 2줄짜리 LCD 전광판 판정 객체
LiquidCrystal_I2C lcd(0x27, 16, 2);

// 조이스틱 계산을 돕기 위한 기준 규칙
#define JOY_MID          2048 // 조이스틱이 가질 수 있는 수치(0~4095)의 딱 절반인 정중앙값
#define JOY_MAX          4095 // 조이스틱을 끝까지 밀었을 때 나오는 최대값
#define JOY_THRESH        800 // 메뉴를 고르거나 시작할 때, 조이스틱을 과감하게 밀었는지 판단하는 기준선
#define SEND_INTERVAL_MS   10 // 통신 주기 (10ms = 0.01초에 한 번씩 엄청나게 빠르게 좌표를 보냄)
#define CHANGE_THRESH       8 // 조이스틱 전압이 미세하게 떨릴 때 불필요하게 편지를 보내는 걸 막는 기준값
#define FILTER_SIZE         8 // 최근에 읽은 조이스틱 값 몇 개를 모아서 평균 낼지 정하는 개수
#define JOY_DEADZONE      150 // 자동차 핸들 유격처럼 손을 떼고 가만히 있을 때 혼자 흐르는 걸 막는 안전 구역

// [손떨림 방지 장치 (이동평균 필터)]
// 스마트폰 동영상 손떨림 방지처럼, 최근 조이스틱 값 8개를 모아서 평균을 내어 부드럽게 만들어주는 상자
struct AxisFilter {
  int  buf[FILTER_SIZE]; // 최근 8번의 조이스틱 값을 보관하는 기억 장소
  long sum;              // 보관된 8개 값의 총합
  int  idx;              // 가장 오래된 기록을 지우기 위해 가리키는 순환 바늘

  // 처음 보드가 켜졌을 때, 현재 조이스틱 값으로 상자 8칸을 가득 채우는 초기화 함수
  void init(int pin) {
    int v = analogRead(pin);
    sum = 0;
    for (int i = 0; i < FILTER_SIZE; i++) { buf[i] = v; sum += v; }
    idx = 0;
  }

  // 매 순간 조이스틱 값을 새로 읽어서 가장 오래된 값은 버리고, 8개 값의 따끈따끈한 평균을 내어주는 기능
  int update(int pin) {
    sum -= buf[idx];
    buf[idx] = analogRead(pin);
    sum += buf[idx];
    idx = (idx + 1) % FILTER_SIZE; // 8칸을 다 채우면 다시 0번째 칸으로 돌아가서 덮어씀
    return (int)(sum / FILTER_SIZE);
  }
};

AxisFilter filterLR; // 왼쪽/오른쪽 손떨림 보정 장치
AxisFilter filterUD; // 위쪽/아래쪽 손떨림 보정 장치

int RAW_LR_MID = 2048; // 부팅 시 조이스틱을 아무도 안 건드렸을 때의 실제 좌우 중심 측정값
int RAW_UD_MID = 2048; // 부팅 시 조이스틱을 아무도 안 건드렸을 때의 실제 상하 중심 측정값

// 게임의 현재 진행 상태를 기록하는 칸
enum GameState { STATE_END, STATE_SET, STATE_SRT, STATE_STP };
volatile GameState gState = STATE_END;     // 대기 상태, 설정 상태, 경기 상태 등을 기억함
volatile bool joystickActive = false;      // 경기가 시작하기 전까진 조이스틱이 컴퓨터로 좌표를 못 쏘게 막는 잠금쇠
volatile bool resetMenuFlags = false;      // 상태가 바뀔 때 조이스틱 메뉴 조작이 튕기지 않도록 리셋해주는 신호

// [화장실 문잠금 장치 (이진 세마포어)]
// 컴퓨터 머리가 2개(듀얼 코어)인데, 둘이 동시에 LCD 화면에 낙서를 하면 글자가 깨짐.
// 그래서 "열쇠를 가진 머리 하나만 LCD 내용을 수정하자"고 약속하는 안전장치
SemaphoreHandle_t lcdSemaphore = NULL;     // 하나뿐인 화장실 열쇠 변수
volatile bool lcdPending   = false;        // "LCD 화면에 띄울 새 글자가 대기 중이다"라는 표시 램프
char lcdBuf0[17]  = "                ";    // 1행(첫째 줄) 화면에 담길 글자 임시 임시 저장소
char lcdBuf1[17]  = "                ";    // 2행(둘째 줄) 화면에 담길 글자 임시 임시 저장소

// 조이스틱 버튼의 연타 방지(쿨타임) 변수
volatile bool buttonClicked = false;       // 버튼이 제대로 눌렸다는 걸 루프에 전달하는 플래그
volatile unsigned long lastInterruptTime = 0; // 버튼이 마지막으로 승인된 과거 스톱워치 시간
#define DEBOUNCE_TIME_MS 250               // 쿨타임 (버튼을 누를 때 0.25초 동안 부르르 떨리는 신호는 다 무시)

unsigned long tLastSend = 0; // 마지막으로 무선 편지를 보냈던 과거 시간 기록
int lastSentX = JOY_MID;     // 방금 전에 컴퓨터로 보냈던 이전 X 좌표 기록용
int lastSentY = JOY_MID;     // 방금 전에 컴퓨터로 보냈던 이전 Y 좌표 기록용

TaskHandle_t NetworkRxTask;  // 컴퓨터가 보낸 무선 신호를 감시하는 '0번 머리(Core 0)' 전담 스레드 이름표

// [버스 하차 벨 장치 (인터럽트)]
// 보드가 아무리 바쁘게 계산하고 있어도, 버튼이 눌리는 순간 하던 일을 멈추고 이 함수로 즉시 순간이동함
void IRAM_ATTR clickButton() {
  unsigned long now = millis(); // 보드가 켜진 후 흘러간 현재 시간(밀리초) 확인
  // 엘리베이터 닫힘 버튼 연타 방지처럼, 마지막으로 누른 지 0.25초가 지났을 때만 진짜 클릭으로 인정
  if (now - lastInterruptTime > DEBOUNCE_TIME_MS) {
    buttonClicked = true;       // 진짜 유효한 클릭이 발생했다고 표시
    lastInterruptTime = now;    // 마지막으로 클릭을 승인한 시간을 지금 시간으로 바꿈
  }
}

// 부품마다 조금씩 삐뚤어진 조이스틱의 물리적 가운데 값을 수학적인 정중앙(2048)으로 싹 맞춰주는 기능
int mapAxis(int raw, int midVal) {
  // 만약 읽어온 값이 처음에 측정한 엉터리 중심점 근처(안전지대 150 안)에 있으면
  if (abs(raw - midVal) <= JOY_DEADZONE) return JOY_MID; // 미세한 떨림으로 치고 강제로 완벽한 정지 상태(2048)를 줌
  
  int mapped;
  // 기준점보다 작을 때와 클 때를 반으로 나누어 각각 0~2048 / 2048~4095 범위로 정밀하게 비율을 맞춰 변환함
  if (raw < midVal) mapped = map(raw, 0, midVal - JOY_DEADZONE, 0, JOY_MID);
  else mapped = map(raw, midVal + JOY_DEADZONE, JOY_MAX, JOY_MID, JOY_MAX);
  
  return constrain(mapped, 0, JOY_MAX); // 변환된 값이 0~4095 범위를 벗어나지 않게 안전하게 가둠
}

// 필터로 다듬고, 영점 맞추고, 기판 방향에 맞춰 좌우 반전까지 끝마친 깨끗한 X(가로) 좌표 가져오기
int getFilteredX() {
  int raw = filterLR.update(PIN_LR);  // 1. 가로축 조이스틱 전압 손떨림 방지 처리
  int val = mapAxis(raw, RAW_LR_MID); // 2. 삐뚤어진 가운데 정렬 맞추기
  int x = JOY_MAX - val;              // 3. 조이스틱 부품이 뒤집혀 장착된 것을 코드로 좌우 반전 계산
  if (abs(x - JOY_MID) <= 100) x = JOY_MID; // 멈춰있을 때 흐르지 않게 한 번 더 꽉 잡아줌
  return x;
}

// 필터로 다듬고 영점까지 완벽히 맞춘 깨끗한 Y(세로) 좌표 가져오기
int getFilteredY() {
  int raw = filterUD.update(PIN_UD);  // 1. 세로축 조이스틱 전압 손떨림 방지 처리
  return mapAxis(raw, RAW_UD_MID);    // 2. 가운데 영점 정렬을 마친 결과 반환
}

// 0번 머리와 1번 머리가 서로 충돌하지 않게, LCD에 띄우고 싶은 글자를 임시 대기실 버퍼에 예약하는 함수
void requestLCD(const char* text) {
  if (lcdSemaphore == NULL) return; // 열쇠 장치가 아직 준비 안 됐으면 패스
  
  // 0.01초 안에 화장실 문 열쇠를 획득하는 데 성공하면 진입
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    size_t len = strlen(text); // 적으려는 글자가 총 몇 글자인지 글자수 세기
    
    // 16글자 이하의 짧은 글자면 첫째 줄에 예쁘게 집어넣고 아랫줄은 깨끗하게 지움
    if (len <= 16) { 
      snprintf(lcdBuf0, 17, "%-16s", text); 
      snprintf(lcdBuf1, 17, "%-16s", ""); 
    } 
    // 16글자를 넘어가는 긴 글자면 앞의 16자는 첫째 줄에, 남은 글자는 둘째 줄로 잘라서 배치
    else {
      char line0[17], line1[17]; 
      strncpy(line0, text, 16);     line0[16] = '\0'; 
      strncpy(line1, text + 16, 16); line1[16] = '\0';
      snprintf(lcdBuf0, 17, "%-16s", line0); 
      snprintf(lcdBuf1, 17, "%-16s", line1);
    }
    lcdPending = true;            // "임시 대기실에 새 글자 세팅 완료했으니 전광판에 띄워라" 하고 불 켬
    xSemaphoreGive(lcdSemaphore); // 볼일 끝났으니 다음 사람 쓰라고 열쇠 즉시 반납
  }
}

// 오직 메인 루프(1번 머리)에서만 진짜 하드웨어 통신선을 써서 LCD 화면을 새로고침해 띄우는 함수
void renderLCD() {
  if (!lcdPending || lcdSemaphore == NULL) return; // 대기실에 새로 적힌 글자가 없으면 굳이 일 안 하고 패스
  
  char line0[17], line1[17];
  // 글자를 실제 전광판에 옮겨 적는 동안 다른 머리가 접근 못 하게 열쇠 획득
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    memcpy(line0, lcdBuf0, 17); // 대기실 윗줄 글자 복사해오기
    memcpy(line1, lcdBuf1, 17); // 대기실 아랫줄 글자 복사해오기
    lcdPending = false;           // 복사가 끝났으니 대기 플래그 리셋
    xSemaphoreGive(lcdSemaphore); // 백그라운드 통신 코어가 언제든 쓸 수 있게 즉시 열쇠 돌려주기
  } else return; // 열쇠 뺏기에 실패했으면 충돌 방지를 위해 다음 기회에 실행
  
  // 실제 내 눈에 보이는 하드웨어 LCD 모듈에 최종 글자 주사
  lcd.clear(); 
  lcd.setCursor(0, 0); lcd.print(line0);
  lcd.setCursor(0, 1); lcd.print(line1);
}

// 메인 게임을 연산하고 있는 컴퓨터를 향해 무선 인터넷 편지(UDP 패킷)를 발송하는 함수
void sendUDP(const String& msg) {
  if (WiFi.status() == WL_CONNECTED) { // 와이파이가 정상적으로 인터넷에 연결되어 있을 때만 실행
    udp.beginPacket(serverIP, SEND_PORT); // 편지 봉투 겉면에 컴퓨터 주소와 나가는 문(포트) 작성
    udp.print(msg);                       // 편지 알맹이 집어넣기
    udp.endPacket();                      // 무선 안테나로 전격 발송
  }
}

// [0번 머리(Core 0)가 평생 백그라운드에서 실행하는 전담 루프 함수]
// 메인 프로그램 계산과 완전히 독립된 곳에서, 컴퓨터가 보내오는 무선 게임 신호를 1밀리초도 안 놓치고 낚아챔
void networkRxLoop(void* pvParameters) {
  char rxBuffer[255]; // 컴퓨터가 보낸 무선 편지를 임시로 받아 적어둘 편지 용지 그릇
  
  for (;;) { // 무한 반복 (스레드가 죽지 않고 백그라운드에서 평생 보초를 서는 구조)
    if (WiFi.status() == WL_CONNECTED) {
      int packetSize = udp.parsePacket(); // 무선 우체통 구멍에 도착한 편지가 있는지 크기 체크
      if (packetSize) { 
        int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1); // 그릇 용량만큼 편지 내용을 읽어서 저장
        if (len > 0) rxBuffer[len] = '\0'; // 문장의 끝을 알리는 마크를 맨 뒤에 강제로 삽입
        String response = String(rxBuffer); response.trim(); // 글자 앞뒤의 불필요한 줄바꿈이나 공백 청소

        // [게임 종료 신호 접수]: 컴퓨터가 경기가 끝났다고 "END:WIN:LOSE" 형태로 신호를 보냈을 때
        if (response.indexOf(':') != -1 && response.indexOf("END") != -1) {
          gState = STATE_END;         // 보드 상태를 대기 화면 상태(STATE_END)로 강제 회귀
          joystickActive = false;     // 게임이 끝났으니 조이스틱 좌표 송신 기능을 자물쇠로 잠금
          resetMenuFlags = true;      // 조이스틱 메뉴 조작이 연타로 오작동하지 않게 차단벽 변수 리셋 신호 켬
          
          char part1[10], part2[10], part3[10];
          // 콜론(:) 기호를 기준으로 날아온 문장을 3토막으로 잘라서 파싱함
          sscanf(response.c_str(), "%[^:]:%[^:]:%[^:]", part1, part2, part3);
          requestLCD(("You " + String(part2)).c_str()); // P1 보드는 두 번째 조각(본인 승패 결과)만 발췌해서 전광판 표출 예약
          continue; 
        } 
        else if(response == "STP") { // [일시정지 신호 수신]
          gState = STATE_STP; joystickActive = false; resetMenuFlags = true;
          requestLCD("READY"); continue;
        }
        else if (response == "SET") { // [환경설정 메뉴 진입 신호 수신]
          gState = STATE_SET; joystickActive = false; resetMenuFlags = true;
          requestLCD("SETTING MODE"); continue; 
        }
        else if (response == "SRT") { // [진짜 경기 시작 신호 수신]
          gState = STATE_SRT; 
          joystickActive = true;      // 조이스틱 잠금쇠가 해제되어 실시간 패들 무빙 좌표 전송 기능 활성화!
          resetMenuFlags = true;
          requestLCD("GAME START"); continue;
        }
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS); // 시스템이 과부하로 다운되지 않도록 1밀리초 동안 잠시 숨 돌리기 휴식 부여
  }
}

// 보드에 전기가 처음 들어왔을 때 단 1번만 순서대로 실행되어 부품들을 초기 세팅하는 구역
void setup() {
  Serial.begin(115200); // 컴퓨터 모니터와 대화할 수 있는 통신 속도 개방
  
  // 최초 시작 시 화장실 문잠금 장치(이진 세마포어)를 하나 찍어내고 즉시 열어둠
  lcdSemaphore = xSemaphoreCreateBinary(); xSemaphoreGive(lcdSemaphore); 
  
  Wire.begin(25, 26); // P1 하드웨어 실제 선 연결: 25번핀(SDA), 26번핀(SCL)으로 화면 통신선 개통
  lcd.init(); lcd.backlight(); lcd.clear(); // LCD 화면 노이즈 초기화 및 조명 켜기

  WiFi.config(local_IP, gateway, subnet); WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(200); } // 와이파이가 안착되어 연결될 때까지 0.2초 간격으로 무한 대기

  analogReadResolution(12); // 조이스틱 아날로그 전압 눈금을 12비트(0~4095 정밀 스케일)로 고정
  pinMode(SW, INPUT_PULLUP); // 버튼 구멍에 내부 저항 세팅 (안 누르면 5V HIGH, 누르는 순간 그라운드로 쇼트 나며 0V LOW)
  attachInterrupt(digitalPinToInterrupt(SW), clickButton, FALLING); // 전압이 0V로 툭 떨어지는 타이밍에 하차 벨(인터럽트) 매핑 등록
  udp.begin(RECEIVE_PORT); // 수신용 우체통 문을 활짝 열어둠

  // [체중계 영점 조절 (오토 캘리브레이션)]
  // 아무도 조이스틱을 안 건드린 초기 상태에서 64번 빠르게 전압을 측정해 진짜 평균 부품 영점을 계산함.
  // 이걸 해두기 때문에 부품 자체 오차로 틀어져 있던 중앙값이 프로그램 내부에서 완벽하게 딱 정중앙으로 보정됨.
  long sumLR = 0, sumUD = 0;
  for (int i = 0; i < 64; i++) { sumLR += analogRead(PIN_LR); sumUD += analogRead(PIN_UD); delay(3); }
  RAW_LR_MID = (int)(sumLR / 64);
  RAW_UD_MID = (int)(sumUD / 64);

  filterLR.init(PIN_LR); filterUD.init(PIN_UD); // 두 축의 손떨림 보정 상자 내부를 초기 전압으로 충전

  // [FreeRTOS 멀티코어 업무 분담]: 
  // 무선 편지 수신 함수(`networkRxLoop`)를 메인 로직과 완전히 격리하여 옆 동네인 '0번 머리(Core 0)' 프로세서에 독점 상주 배치함
  xTaskCreatePinnedToCore(networkRxLoop, "NetworkRxTask", 10000, NULL, 1, &NetworkRxTask, 0);
  requestLCD("P1 CONTROLLER"); 
}

// 오직 1번 머리(Core 1) 프로세서에서만 평생 뺑뺑이 돌며 내 조이스틱 무빙을 담당하는 메인 제어 루프
void loop() {
  renderLCD(); // 매 루프 시작할 때, 통신 코어가 백그라운드 대기실에 채워둔 새 LCD 문자열이 있다면 실제 화면에 물리 출력

  // P1 마스터 보드 전용 권한: 환경설정 메뉴 창을 열거나 항목 결정을 내리는 버튼 클릭 처리
  if (buttonClicked) {
    buttonClicked = false; // 플래그를 빠르게 원래 상태로 되돌림 (다음 클릭 신호를 받아야 하므로)
    if (gState == STATE_END || gState == STATE_SRT || gState == STATE_STP) sendUDP("SET\n"); 
    else if (gState == STATE_SET) sendUDP("CLK\n"); 
  }

  // 조이스틱을 한 방향으로 쭉 밀고 있을 때 무선 편지가 무한 연타로 폭발해서 통신망이 터지는 걸 막는 중복 차단 플래그
  static bool lastUpState = false; static bool lastDnState = false;
  if (resetMenuFlags) { lastUpState = false; lastDnState = false; resetMenuFlags = false; }

  int cx = getFilteredX(); // 손떨림 보정과 영점 조절이 끝난 최종 가로 좌표
  int cy = getFilteredY(); // 손떨림 보정과 영점 조절이 끝난 최종 세로 좌표

  // 게임 대기 화면(STATE_END)일 때, 조이스틱을 위로 탁 치면 경기 시작 요청 편지 발송
  if (gState == STATE_END) {
    bool isUp = (cy < JOY_MID - JOY_THRESH); // 세로 수치가 상단 임계 가이드 경계선 너머로 진입했는지 판별
    if (isUp && !lastUpState) sendUDP("SRT\n"); // 처음으로 밀어 올린 그 타이밍 찰나에 딱 1번만 무선 전송 실행
    lastUpState = isUp;
  }
  // P1 마스터 전용: 세팅창 메뉴 상하 고르기 조이스틱 무선 제어 구역
  else if (gState == STATE_SET) {
    bool isUp = (cy < JOY_MID - JOY_THRESH); bool isDn = (cy > JOY_MID + JOY_THRESH);
    if (isUp && !lastUpState) sendUDP("UP\n");  
    if (isDn && !lastDnState) sendUDP("DN\n");  
    lastUpState = isUp; lastDnState = isDn;
  }
  // 파이썬 PC로부터 경기 개시 사령을 받아서 잠금이 완전히 풀린 실시간 대결 가동 상태
  else if (gState == STATE_SRT && joystickActive) {
    // 이전 편지를 보낸 시간으로부터 딱 10ms(0.01초) 이상의 시간이 시계상 흘렀는지 타이머 주기 체크
    if (millis() - tLastSend >= SEND_INTERVAL_MS) {
      
      // 손떨림 검사: 직전 좌표랑 비교해서 최소 8칸 이상 확실하게 조이스틱을 움직였을 때만 의미 있는 무빙으로 인정하고 편지 조립
      if (abs(cx - lastSentX) > CHANGE_THRESH || abs(cy - lastSentY) > CHANGE_THRESH) {
        char packet[16];
        // [중요 정책 보정]: 보드 단에서 가동 영역 제한 브레이크를 임의로 걸지 않고 0부터 4095 전 범위를 그대로 전송함.
        // 축구 경기에서 수비수도 중앙선을 넘어 전진 공격할 수 있듯이, 내 패들이 하프라인(중앙선) 위를 돌파하여 상대방 진영 앞마당까지 깊숙하게 넘어갈 수 있는 유연한 규칙이 이 처리를 통해 구현됨.
        snprintf(packet, sizeof(packet), "%d:%d\n", cx, cy);
        sendUDP(packet); 
        lastSentX = cx; lastSentY = cy; // 이전 전송 기록 백업 변수에 현재 값을 저장해서 동기화
      }
      tLastSend = millis(); // 다음 주기 계산을 위해 기준 스톱워치 타임을 방금 보낸 시간으로 리셋
    }
  }
}
