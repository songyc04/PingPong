/*
 * ╔══════════════════════════════════════════════════════╗
 * ESP32 핀볼 [P1 마스터 보드] - LCD 멀티코어 및 카운트다운 완벽 수정본
 * (고정 IP: 192.168.0.200 / Python PC: 192.168.0.138)
 * ╚══════════════════════════════════════════════════════╝
 *
 * [하드웨어 핀 제어 특성]
 * - PIN_LR = VRY(35) : 조이스틱 좌우 제어 (왼쪽: 4095 / 오른쪽: 0)
 * - PIN_UD = VRX(32) : 조이스틱 상하 제어 (위: 0 / 아래: 4095)
 * - I2C 통신선       : SDA(25), SCL(26)
 */

#include <WiFi.h>              // 무선 와이파이 연결 제어 라이브러리
#include <WiFiUdp.h>           // 딜레이가 적은 초고속 UDP 통신 라이브러리
#include <Wire.h>              // I2C 직렬 통신(SDA, SCL 핀) 라이브러리
#include <LiquidCrystal_I2C.h> // I2C 캐릭터 텍스트 LCD 화면 제어 라이브러리

// 접속할 공유기(AP)의 와이파이 아이디 및 비밀번호
const char* ssid     = "fusion";
const char* password = "12345678";

// 네트워크 고정 IP 설정 (P1 보드 고유 네트워크 영역)
IPAddress local_IP(192, 168, 0, 200); // 본인(P1 보드)의 고정 IP
IPAddress gateway(192, 168, 0, 1);    // 게이트웨이(공유기) 주소
IPAddress subnet(255, 255, 255, 0);   // 서브넷 마스크 규격
IPAddress serverIP(192, 168, 0, 138); // 게임이 구동 중인 메인 파이썬 PC IP

// 데이터 혼선 방지를 위한 포트 분리 (P1 마스터 전용 채널)
const int SEND_PORT    = 10001;   // 조이스틱 좌표 및 명령을 PC로 송신하는 출구 포트
const int RECEIVE_PORT = 10003;   // PC로부터 스코어 및 게임 상태를 수신하는 입구 포트

WiFiUDP udp; // UDP 통신 기능을 수행할 객체 선언

// 하드웨어 입출력 핀 배열 (실측 기준 일치)
const int PIN_LR = 35;   // 조이스틱 좌우 아날로그 전압 계측 핀
const int PIN_UD = 32;   // 조이스틱 상하 아날로그 전압 계측 핀
const int SW     = 14;   // 조이스틱 스위치 입력 핀 (인터럽트 사용)

// 0x27 I2C 주소를 가진 16열 2행(16x2) LCD 객체 생성
LiquidCrystal_I2C lcd(0x27, 16, 2);

// 조이스틱 소프트웨어 연산용 기준 상수 설정
#define JOY_MID          2048 // 12비트(0~4095) 해상도의 수학적 중간 영점값
#define JOY_MAX          4095 // 아날로그 핀이 읽을 수 있는 최대 전압 한계치
#define JOY_THRESH        800 // 대기 화면에서 시작/메뉴 조작을 인식할 기울기 임계값
#define SEND_INTERVAL_MS   10 // 패킷 전송 주기 (10ms = 1초에 100번 실시간 전송)
#define CHANGE_THRESH       8 // 조이스틱 전압 미세 떨림으로 인한 불필요한 전송 차단 기준치
#define FILTER_SIZE         8 // 이동평균 필터가 기억할 최근 샘플 데이터 개수
#define JOY_DEADZONE      150 // 손을 뗐을 때 패들이 스스로 흐르는 현상을 막는 무반응 안전 영역

// [이동평균 필터 구조체]
// 조이스틱의 전기적 미세 노이즈(잡음)를 최근 8칸 데이터의 평균 연산으로 깎아내어 패들 움직임을 부드럽게 만듦
struct AxisFilter {
  int  buf[FILTER_SIZE]; // 최근 8번 계측한 값을 보관할 원형 배열 버퍼
  long sum;              // 버퍼 내에 들어있는 모든 데이터의 합산 변수
  int  idx;              // 가장 오래된 기록을 가리키는 인덱스 포인터

  // 최초 가동 시 필터 내부 버퍼를 현재 전압값으로 가득 채워 초기화하는 함수
  void init(int pin) {
    int v = analogRead(pin);
    sum = 0;
    for (int i = 0; i < FILTER_SIZE; i++) { buf[i] = v; sum += v; }
    idx = 0;
  }

  // 매 루프마다 새로운 입력값을 받아 가장 오래된 값을 회전식으로 교체하며 평균값을 계산
  int update(int pin) {
    sum -= buf[idx];
    buf[idx] = analogRead(pin);
    sum += buf[idx];
    idx = (idx + 1) % FILTER_SIZE; // 8번 방에 도달하면 원형 구조에 따라 0번 방으로 복귀
    return (int)(sum / FILTER_SIZE);
  }
};

AxisFilter filterLR; // 좌우축 노이즈 제거용 필터 인스턴스
AxisFilter filterUD; // 상하축 노이즈 제거용 필터 인스턴스

int RAW_LR_MID = 2048; // 최초 부팅 시 가만히 둔 상태의 좌우 중립 실측값
int RAW_UD_MID = 2048; // 최초 부팅 시 가만히 둔 상태의 상하 중립 실측값

// 게임 가동 국면을 통제하기 위한 내부 상태 메모리
enum GameState { STATE_END, STATE_SET, STATE_SRT, STATE_STP };
volatile GameState gState = STATE_END;     // 두 코어가 동시 접근하므로 변조 방지 volatile 적용
volatile bool joystickActive = false;      // 실제 경기 시작 전까지 좌표 송신을 차단하는 안전 잠금장치
volatile bool resetMenuFlags = false;      // 게임 상태 전환 시 조이스틱 메뉴 조작용 중복 입력을 초기화할 신호

// [LCD 멀티코어 세마포어 동기화]
// Core 0(통신)과 Core 1(메인)이 동시에 LCD 화면을 갱신하려다 글자가 깨지는 현상을 차단하는 상호 배제 잠금 장치
SemaphoreHandle_t lcdSemaphore = NULL;     // 세마포어 열쇠 변수
volatile bool lcdPending   = false;        // "출력 처리할 새 문자열 데이터가 대기 중이다"를 나타내는 플래그
char lcdBuf0[17]  = "                ";    // LCD 1행(윗줄) 내용 임시 보관 버퍼
char lcdBuf1[17]  = "                ";    // LCD 2행(아랫줄) 내용 임시 보관 버퍼

// 조이스틱 버튼 하드웨어 디바운싱(연타 오작동 방지) 변수
volatile bool buttonClicked = false;       // 버튼이 정상 클릭되었음을 메인 루프에 알리는 플래그
volatile unsigned long lastInterruptTime = 0; // 버튼이 마지막으로 승인된 과거 내장 스톱워치 타임스탬프
#define DEBOUNCE_TIME_MS 250               // 쿨타임 (버튼 내부 금속판이 떨리는 0.25초 동안의 신호는 무시)

unsigned long tLastSend = 0; // 마지막으로 무선 패킷을 전송한 시점의 내장 스톱워치 시간
int lastSentX = JOY_MID;     // 직전 루프에서 컴퓨터로 발사했던 X 좌표 백업 변수
int lastSentY = JOY_MID;     // 직전 루프에서 컴퓨터로 발사했던 Y 좌표 백업 변수

TaskHandle_t NetworkRxTask;  // Core 0번 프로세서에서 백그라운드로 독립 구동할 수신 스레드 이름표

// [버튼 인터럽트 서비스 루틴] 
// 메인 코드가 가동 중이더라도 버튼이 눌리는 순간 하드웨어 단에서 최우선으로 가로채 실행함
void IRAM_ATTR clickButton() {
  unsigned long now = millis(); // 하드웨어 내부 카운터로부터 부팅 이후 흐른 밀리초 검출
  // 현재 시간과 마지막 승인 시간의 차이가 설정해 둔 쿨타임(250ms)을 충족했는지 판별
  if (now - lastInterruptTime > DEBOUNCE_TIME_MS) {
    buttonClicked = true;       // 미세 떨림 노이즈가 아닌 사람이 누른 진짜 신호로 최종 승인
    lastInterruptTime = now;    // 마지막 유효 클릭 시간을 방금 터진 현재 시간으로 갱신
  }
}

// 조이스틱 부품마다 미세하게 틀어져 있는 물리적인 중립 전압 오차를 수학적 2048 정중앙으로 교정하는 함수
int mapAxis(int raw, int midVal) {
  // 입력된 날것 전압값이 부팅 시 측정한 중심값 기준 데드존 영역 내부에 있다면
  if (abs(raw - midVal) <= JOY_DEADZONE) return JOY_MID; // 미세 오차를 무시하고 완전 중립 정지 좌표(2048) 반환
  
  int mapped;
  // 중립점보다 작을 때와 클 때를 분할하여 각각 0~2048 / 2048~4095 영역으로 정밀 비례 변환 계산
  if (raw < midVal) mapped = map(raw, 0, midVal - JOY_DEADZONE, 0, JOY_MID);
  else mapped = map(raw, midVal + JOY_DEADZONE, JOY_MAX, JOY_MID, JOY_MAX);
  
  return constrain(mapped, 0, JOY_MAX); // 변환값이 가동 영역 0~4095 범위를 벗어나 튀지 않도록 가둠
}

// 필터링, 영점 교정, 물리 방향에 따른 소프트웨어 좌우 반전을 거쳐 정제된 가로 X 좌표 도출
int getFilteredX() {
  int raw = filterLR.update(PIN_LR);  // 1. 좌우 핀 전압 노이즈 필터링
  int val = mapAxis(raw, RAW_LR_MID); // 2. 부품 중심점 오차 교정
  int x = JOY_MAX - val;              // 3. 기판 장착 사양에 맞춘 하드웨어 좌우 반전 연산
  if (abs(x - JOY_MID) <= 100) x = JOY_MID; // 중립 근방 손떨림 흐름 방지 영역 추가
  return x;
}

// 필터링 및 영점 교정 연산을 수료하여 깨끗하게 정제된 세로 Y 좌표 도출
int getFilteredY() {
  int raw = filterUD.update(PIN_UD);  // 1. 상하 핀 전압 노이즈 필터링
  return mapAxis(raw, RAW_UD_MID);    // 2. 중심값 영점 교정 처리 후 변환값 반환
}

// 어느 코어(Core 0 / 1)에서든 상호 충돌 없이 LCD 글자 출력을 임시 버퍼 슬롯에 안전하게 예약하는 함수
void requestLCD(const char* text) {
  if (lcdSemaphore == NULL) return; // 세마포어 키가 비활성화된 초기 국면이라면 오작동 방지를 위해 즉시 차단
  
  // 10ms 이내에 세마포어 점유권(열쇠)을 획득하는 데 성공했다면 진입 허가
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    size_t len = strlen(text); // 출력 문자열의 전체 길이 계산
    
    // 16글자 이하일 때는 첫 번째 줄에 텍스트를 배치하고 아래 줄 공간은 깨끗하게 공백 채움
    if (len <= 16) { 
      snprintf(lcdBuf0, 17, "%-16s", text); 
      snprintf(lcdBuf1, 17, "%-16s", ""); 
    } 
    // 16글자를 초과하여 길게 들어오면 앞의 16자는 윗줄에, 나머지 글자는 아랫줄로 재단하여 분할 배치
    else {
      char line0[17], line1[17]; 
      strncpy(line0, text, 16);     line0[16] = '\0'; 
      strncpy(line1, text + 16, 16); line1[16] = '\0';
      snprintf(lcdBuf0, 17, "%-16s", line0); 
      snprintf(lcdBuf1, 17, "%-16s", line1);
    }
    lcdPending = true;            // "새 출력이 대기 버퍼에 저장 완료되었다"고 신호 램프 온
    xSemaphoreGive(lcdSemaphore); // 다른 업무 처리를 위해 세마포어 소유권(열쇠) 즉각 반납
  }
}

// 오직 Core 1(loop 메인 스레드)에서만 하드웨어 I2C 통신망을 독점 제어하여 실제 LCD 화면에 물리적으로 주사하는 함수
void renderLCD() {
  if (!lcdPending || lcdSemaphore == NULL) return; // 새로고침할 문자열이 없거나 키가 손상되었다면 즉시 차단
  
  char line0[17], line1[17];
  // 코어 1 프로세서가 버퍼 내용을 스냅샷 복사해 오기 위해 세마포어 권한 잠시 획득
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    memcpy(line0, lcdBuf0, 17);
    memcpy(line1, lcdBuf1, 17);
    lcdPending = false;           // 내용을 안전하게 빼왔으므로 대기 플래그 리셋
    xSemaphoreGive(lcdSemaphore); // 무선 수신 코어(Core 0)가 언제든 접근할 수 있게 즉시 열쇠 반납
  } else return; // 제어권 경쟁에서 밀렸다면 충돌 예방을 위해 다음 루프로 패스
  
  // 실제 물리 LCD 소자에 문자열 스트림 출력
  lcd.clear(); 
  lcd.setCursor(0, 0); lcd.print(line0);
  lcd.setCursor(0, 1); lcd.print(line1);
}

// 메인 게임이 구동 중인 파이썬 PC 컴퓨터를 겨냥하여 무선 UDP 패킷을 발송하는 함수
void sendUDP(const String& msg) {
  if (WiFi.status() == WL_CONNECTED) { // 보드가 무선 공유기 네트워크망에 정상 귀속되어 있을 때만 기동
    udp.beginPacket(serverIP, SEND_PORT); // 봉투에 목적지 IP와 전용 포트 번호를 낙인
    udp.print(msg);                       // 메시지 알맹이 주입
    udp.endPacket();                      // 무선 안테나 모듈을 통해 최종 발송 처리
  }
}

// [Core 0 백그라운드 상주 루프 함수]
// 메인 제어 계산과 완전히 단절된 독립 코어에서, 컴퓨터가 무선으로 송신해 주는 지령 프로토콜을 실시간 낚아챔
void networkRxLoop(void* pvParameters) {
  char rxBuffer[255]; // 유디피 패킷이 도달했을 때 내용을 받아 적을 임시 수신 바구니 그릇
  
  for (;;) { // 무한 루프 (스레드가 종료되지 않고 백그라운드에서 평생 감시 체계 가동)
    if (WiFi.status() == WL_CONNECTED) {
      int packetSize = udp.parsePacket(); // 우체통 소켓 구멍에 도착한 데이터 패킷이 있는지 용량 체크
      if (packetSize) { 
        int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1); // 그릇 용량 한계치까지 데이터를 긁어와 복사
        if (len > 0) rxBuffer[len] = '\0'; // 문자열 종결마크 널 문자를 후미에 강제 삽입
        String response = String(rxBuffer); response.trim(); // 데이터 앞뒤의 공백 및 줄바꿈 기호 청소

        // [종료 프로토콜 수신]: 메인 PC로부터 경기 마감 사인("END:WIN:LOSE")을 받았을 때
        if (response.indexOf(':') != -1 && response.indexOf("END") != -1) {
          gState = STATE_END;         // 보드의 상태를 즉시 대기 화면 단계(STATE_END)로 이탈 유도
          joystickActive = false;     // 매치가 종료되었으므로 조이스틱 연산을 통한 좌표 송신 잠금
          resetMenuFlags = true;      // 메뉴 튕김 방지용 스토퍼 변수들을 리셋하도록 루프 스레드에 신호 전달
          
          char part1[10], part2[10], part3[10];
          // 콜론 기호를 기준으로 수신된 데이터 문자열을 삼등분 쪼개기 하여 파싱 변수에 분할 수용
          sscanf(response.c_str(), "%[^:]:%[^:]:%[^:]", part1, part2, part3);
          requestLCD(("You " + String(part2)).c_str()); // P1 마스터 보드는 두 번째 칸(본인 결과 정보)만 골라 전광판 표출 예약
          continue; 
        } 
        else if(response == "STP") { // [일시정지 수신]
          gState = STATE_STP; joystickActive = false; resetMenuFlags = true;
          requestLCD("READY"); continue;
        }
        else if (response == "SET") { // [환경설정 메뉴 개방 수신]
          gState = STATE_SET; joystickActive = false; resetMenuFlags = true;
          requestLCD("SETTING MODE"); continue; 
        }
        else if (response == "SRT") { // [경기 스타트 명령 수신]
          gState = STATE_SRT; 
          joystickActive = true;      // 비로소 빗장이 풀리며 실시간 패들 조작 데이터 무선 송출이 허용되는 완전 활성화 상태 돌입
          resetMenuFlags = true;
          requestLCD("GAME START"); continue;
        }
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS); // FreeRTOS 멀티태스킹 스케줄러가 CPU 독점으로 다운되지 않게 1ms 휴식 부여
  }
}

// 보드 전원이 켜지는 순간 단 1번 순행 실행되어 장치 초기화 및 멀티코어 부서를 셋업하는 공간
void setup() {
  Serial.begin(115200); // 디버깅용 PC 시리얼 모니터 관측 속도 개방
  
  // 극초기 구동 시 이진 세마포어 잠금장치를 생성하고, 즉시 사용 가능하도록 릴리즈 처리
  lcdSemaphore = xSemaphoreCreateBinary(); xSemaphoreGive(lcdSemaphore); 
  
  Wire.begin(25, 26); // P1 실측 하드웨어 사양 매칭: 25(SDA), 26(SCL) 포트로 I2C 통신 가동
  lcd.init(); lcd.backlight(); lcd.clear(); // LCD 스캔 엔진 활성화 및 조명 점등

  WiFi.config(local_IP, gateway, subnet); WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(200); } // 와이파이 연결 안착 시까지 0.2초 텀으로 대기

  analogReadResolution(12); // 아날로그-디지털 변환(ADC) 해상도를 12비트(최대 4095 스케일)로 고정
  pinMode(SW, INPUT_PULLUP); // 내부 풀업 회로 적용 (스위치를 누르는 순간 그라운드로 쇼트 나며 0V LOW 검출)
  attachInterrupt(digitalPinToInterrupt(SW), clickButton, FALLING); // 전압이 떨어지는 순간 인터럽트 함수 강제 소환 매핑
  udp.begin(RECEIVE_PORT); // 수신용 우체통 포트를 리스닝 모드로 개방

  // [오토 캘리브레이션 (자동 영점 조절)]:
  // 조이스틱을 가만히 정지시킨 상태에서 3ms 간격으로 총 64번 계측 연산하여 평균 물리 중립 수치 산출
  // 이 과정을 거침으로써 부품 제조 편차로 틀어진 아날로그 중심 오차가 프로그램 내부에서 완전 상쇄됨
  long sumLR = 0, sumUD = 0;
  for (int i = 0; i < 64; i++) { sumLR += analogRead(PIN_LR); sumUD += analogRead(PIN_UD); delay(3); }
  RAW_LR_MID = (int)(sumLR / 64);
  RAW_UD_MID = (int)(sumUD / 64);

  filterLR.init(PIN_LR); filterUD.init(PIN_UD); // 두 축의 이동평균 필터 내부 원형 배열 초기화 충전

  // [FreeRTOS 독립 태스크 명령]: 
  // networkRxLoop 수신 함수를 메인 로직과 분리된 완전히 별개의 부서인 'Core 0번 프로세서'에 상주 격리 배치함
  xTaskCreatePinnedToCore(networkRxLoop, "NetworkRxTask", 10000, NULL, 1, &NetworkRxTask, 0);
  requestLCD("P1 CONTROLLER"); 
}

// 오직 Core 1번 프로세서에서만 주기 순환 작동하며 실시간 패들 송출 제어를 담당하는 메인 루프
void loop() {
  renderLCD(); // 매 주기 시작 시 통신 코어가 백그라운드 버퍼에 채워놓은 새 LCD 문자열 예약이 있다면 실제 디스플레이에 주사

  // P1 마스터 보드 전용: 메뉴창 진입 및 항목 선택 결정을 처리하는 인터럽트 핸들러 영역
  if (buttonClicked) {
    buttonClicked = false; // 플래그 신속 초기화 (다음 클릭을 즉각 받아내야 하므로)
    if (gState == STATE_END || gState == STATE_SRT || gState == STATE_STP) sendUDP("SET\n"); 
    else if (gState == STATE_SET) sendUDP("CLK\n"); 
  }

  // 조이스틱을 한 방향으로 지속적으로 밀고 있을 때 무선 패킷이 연속 연타로 폭발하는 현상을 제어할 중복 플래그 스토퍼
  static bool lastUpState = false; static bool lastDnState = false;
  if (resetMenuFlags) { lastUpState = false; lastDnState = false; resetMenuFlags = false; }

  int cx = getFilteredX(); // 필터 및 보정이 끝난 가로 좌표 확보
  int cy = getFilteredY(); // 필터 및 보정이 끝난 세로 좌표 확보

  // 대기 화면(STATE_END) 국면 상태일 때 조이스틱을 위로 탁 치면 매치 개시 패킷 발송
  if (gState == STATE_END) {
    bool isUp = (cy < JOY_MID - JOY_THRESH); // 세로축 수치가 상단 임계 가이드라인 경계선 너머로 진입했는지 판별
    if (isUp && !lastUpState) sendUDP("SRT\n"); // 처음으로 밀어 올린 그 진입 시점 타이밍에 딱 1번만 무선 전송 실행
    lastUpState = isUp;
  }
  // P1 마스터 보드 전용 세팅창 메뉴 상하 커서 무선 제어 구역
  else if (gState == STATE_SET) {
    bool isUp = (cy < JOY_MID - JOY_THRESH); bool isDn = (cy > JOY_MID + JOY_THRESH);
    if (isUp && !lastUpState) sendUDP("UP\n");  
    if (isDn && !lastDnState) sendUDP("DN\n");  
    lastUpState = isUp; lastDnState = isDn;
  }
  // 파이썬 메인 컴퓨터로부터 매치 정상 시작 지령을 전달받아 조이스틱 봉인이 풀린 실시간 대결 가동 상태
  else if (gState == STATE_SRT && joystickActive) {
    // 마지막 무선 통신 발생 기준 시점 대비 딱 10ms 이상의 시간이 경과했는지 타이머 점검
    if (millis() - tLastSend >= SEND_INTERVAL_MS) {
      
      // 손떨림 방지: 직전 사격 데이터 대비 최소 8 단위를 초과하는 진짜 의미 있는 마이그레이션이 터졌을 때만 패킷 조립 가동
      if (abs(cx - lastSentX) > CHANGE_THRESH || abs(cy - lastSentY) > CHANGE_THRESH) {
        char packet[16];
        // 중요 정책 보정: 보드 단에서 좌표 가동 영역에 브레이크를 걸지 않고 최소 0부터 최대 4095 전 범위를 PC로 그대로 무전 전송
        // 이 처리를 통해 패들이 하프라인(중앙선) 위쪽 경계면을 돌파하여 상대방 진영 안쪽까지 깊숙이 자유롭게 무빙 이동할 수 있는 규칙이 구현됨
        snprintf(packet, sizeof(packet), "%d:%d\n", cx, cy);
        sendUDP(packet); 
        lastSentX = cx; lastSentY = cy; // 과거 백업 변수에 현재 좌표 동기화 보관
      }
      tLastSend = millis(); // 다음 주기 연산을 위한 전송 기준 타임스탬프 갱신
    }
  }
}