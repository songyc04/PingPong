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

#include <WiFi.h>              // 와이파이 안테나 구동 모듈
#include <WiFiUdp.h>           // 무선 초고속 데이터 편지를 발송하는 우체부 모듈
#include <Wire.h>              // I2C 직렬 통신선 제어 부품
#include <LiquidCrystal_I2C.h> // 16열 2행 캐릭터 LCD 전광판 제어 부품

// 와이파이 네트워크 접속용 아이디와 비밀번호 정보
const char* ssid     = "fusion";
const char* password = "12345678";

// 플레이어 2번 게스트 보드 전용 고정 네트워크 IP 설정 구역
IPAddress local_IP(192, 168, 0, 154); // 본인(P2 보드)의 고유 방 번호
IPAddress gateway(192, 168, 0, 1);    // 공유기 게이트웨이 주소
IPAddress subnet(255, 255, 255, 0);   // 서브넷 통신망 규격 마스크
IPAddress serverIP(192, 168, 0, 138); // 메인 게임 알고리즘이 돌아가는 컴퓨터 PC 주소

// 1번 마스터 보드 채널과 완벽히 격리 분리한 2번 보드만의 무선 소켓 포트
const int SEND_PORT    = 10002;   // P2 패들의 실시간 움직임 좌표를 PC로 발사하는 출구 포트
const int RECEIVE_PORT = 10004;   // 파이썬 PC가 계산해준 점수 편지를 받아오는 입구 포트

WiFiUDP udp; // 무선 편지를 실질적으로 송수신 처리할 객체

// 하드웨어 아날로그 조이스틱 및 인터럽트 버튼 구멍 배치 (실측 정보 일치)
const int PIN_LR = 35;   // 가로 X축 전압 계측 핀 구멍
const int PIN_UD = 32;   // 세로 Y축 전압 계측 핀 구멍
const int SW     = 14;   // 비상 상황 발생 시 판을 깨고 경기를 폭파할 강제 종료 버튼 구멍

LiquidCrystal_I2C lcd(0x27, 16, 2);

// 연산 규격 및 보정용 상수의 약속 규칙
#define JOY_MID          2048 // 12비트 스케일의 완벽한 수학적 정중앙값
#define JOY_MAX          4095 // 아날로그 센서가 읽을 수 있는 최대 한계값
#define JOY_THRESH        800 // 대기 상태에서 조이스틱을 과감히 밀어 경기를 수락했는지 판단할 컷트라인
#define SEND_INTERVAL_MS   10 // 1번 마스터와 완벽 동기화된 초고속 10ms 패킷 전송 주기
#define CHANGE_THRESH       8 // 손가락 미세 잔떨림으로 무선 편지가 마구잡이로 나가는 걸 막는 최소 변화량
#define FILTER_SIZE         8 // 손떨림 보정 상자 안에 누적시킬 조이스틱 샘플 데이터 개수
#define JOY_DEADZONE      150 // 조이스틱 핸들 유격처럼 손을 뗐을 때 혼자 흐르는 현상을 막는 무반응 안전지대

// [손떨림 방지 장치 (이동평균 필터)]
// 최근에 읽은 전압 수치 8개를 버퍼 상자에 저장하고 회전식으로 평균을 내서 패들을 스케이트 타듯 부드럽게 움직이게 다듬음
struct AxisFilter {
  int  buf[FILTER_SIZE]; // 최근 8번 계측한 수치를 담아둘 메모리 방
  long sum;              // 방에 들어있는 모든 데이터의 누적 합산
  int  idx;              // 가장 과거 기록의 자리를 짚어내는 바늘 포인터
  
  // 최초 시동 시 보관 상자 공간을 현재 초기 전압값으로 가득 채워두는 초기화 함수
  void init(int pin) {
    int v = analogRead(pin); sum = 0; for (int i = 0; i < FILTER_SIZE; i++) { buf[i] = v; sum += v; } idx = 0;
  }
  // 매 바퀴마다 가장 오래된 옛날 기록을 지우고 최신 데이터로 업데이트하며 실시간 평균값 전달
  int update(int pin) {
    sum -= buf[idx]; buf[idx] = analogRead(pin); sum += buf[idx];
    idx = (idx + 1) % FILTER_SIZE; // 바늘을 한 칸 전진시키되 8번 방에 도달하면 구조상 다시 0번 방으로 복귀
    return (int)(sum / FILTER_SIZE);
  }
};

AxisFilter filterLR; // 플레이어 2번용 좌우축 손떨림 보정 상자
AxisFilter filterUD; // 플레이어 2번용 상하축 손떨림 보정 상자

int RAW_LR_MID = 2048; // 부팅 시점 2번 하드웨어 부품 자체의 가로축 실측 중립 영점
int RAW_UD_MID = 2048; // 부팅 시점 2번 하드웨어 부품 자체의 세로축 실측 중립 영점

// 매치 게임 가동 국면을 감지하는 4대 상태 레지스터
enum GameState { STATE_END, STATE_SET, STATE_SRT, STATE_STP };
volatile GameState gState = STATE_END;     // 두 머리가 같이 접근하므로 volatile 조치
volatile bool joystickActive = false;      // PC 사인이 오기 전까지 무선 송출을 걸어잠그는 잠금쇠 방화벽
volatile bool resetMenuFlags = false;      // 조이스틱 연타 오작동 입력을 깔끔하게 소거해주는 내부 초기화 플래그

// [화장실 문잠금 장치 (이진 세마포어)]
// 코어 0번(통신)과 코어 1번(메인)이 동시에 LCD 디스플레이 주소에 낙서를 하다가 폰트가 다 깨지는 현상을 방어하는 상호 배제 잠금틀
SemaphoreHandle_t lcdSemaphore = NULL;     // 화면 제어용 열쇠 변수
volatile bool lcdPending   = false;        // "대기실에 새로운 글자가 준비 완료되었으니 화면을 새로고침해라" 플래그
char lcdBuf0[17]  = "                ";    // LCD 첫째 줄 화면 내용 임시 보관 버퍼 대기실
char lcdBuf1[17]  = "                ";    // LCD 둘째 줄 화면 내용 임시 보관 버퍼 대기실

// 플레이어 2번 비상 탈출 버튼 전용 연타 차단(디바운싱 쿨타임) 변수
volatile bool buttonClicked = false;       // 버튼 승인이 완료되었음을 loop 함수에 인계하는 신호등
volatile unsigned long lastInterruptTime = 0; // 버튼 승인이 떨어진 마지막 시점의 내장 스톱워치 타임스탬프
#define DEBOUNCE_TIME_MS 250               // 쿨타임 (물리 버튼판이 부딪치며 다다다닥 떨리는 0.25초 이내의 잡음 신호는 싹 무시)

unsigned long tLastSend = 0; // 무선 패킷 편지를 컴퓨터로 방출했던 과거의 마지막 스톱워치 타임 변수
int lastSentX = JOY_MID;     // 잔떨림 검사를 위해 직전 타이밍에 발사했던 과거 X 좌표 백업 메모리
int lastSentY = JOY_MID;     // 잔떨림 검사를 위해 직전 타이밍에 발사했던 과거 Y 좌표 백업 메모리

TaskHandle_t NetworkRxTask; // 0번 머리(Core 0) 자원을 사용하여 무선 편지 수신만 전문으로 처리할 전담 스레드 이름표

// [버스 하차 벨 장치 (인터럽트)]
// 플레이어 2번 유저가 경기 도중에 판을 깨고 이탈하고 싶어서 버튼을 누르면, 메인 코드가 연산 중이더라도 즉시 비상 탈출 시퀀스로 가로챔
void IRAM_ATTR clickButton() {
  unsigned long now = millis(); // 하드웨어 카운터 시계로부터 부팅 이후 경과된 현재 밀리초 타임 획득
  // 현재 스톱워치 타임과 마지막 클릭 승인 시점의 간격이 설정해 둔 쿨타임 기준선(250ms)을 넘겼는지 판별
  if (now - lastInterruptTime > DEBOUNCE_TIME_MS) {
    buttonClicked = true;       // 미세 기계 노이즈 잡음이 아닌 유저 의지가 실린 진짜 비상 강제종료 클릭으로 최종 승인
    lastInterruptTime = now;    // 유효 승인 과거 기록을 방금 클릭이 터진 따끈따끈한 현재 시점으로 갱신 동기화
  }
}

// 조이스틱 부품마다 미세하게 틀어져 있는 체중계 영점을 수학적으로 완전한 2048 정중앙으로 교정하는 함수
int mapAxis(int raw, int midVal) {
  if (abs(raw - midVal) <= JOY_DEADZONE) return JOY_MID; // 중립 핸들 유격 안전지대 내에 있으면 강제 정지 좌표(2048) 부여
  int mapped;
  if (raw < midVal) mapped = map(raw, 0, midVal - JOY_DEADZONE, 0, JOY_MID); // 절반 미만 영역 비율 비례 변환
  else mapped = map(raw, midVal + JOY_DEADZONE, JOY_MAX, JOY_MID, JOY_MAX); // 절반 초과 영역 비율 비례 변환
  return constrain(mapped, 0, JOY_MAX); // 비례식 연산 결과 수치가 작동 범위 0~4095 바깥으로 튀어나가지 않게 최종 마감
}

// 손떨림 보정 상자와 영점 정렬, 거울 반전 계산까지 완벽히 마친 2번 패들의 클린 가로 X 좌표 도출
int getFilteredX() {
  int raw = filterLR.update(PIN_LR);  // 1. 가로축 핀 전압 미세 진동 노이즈 깎기
  int val = mapAxis(raw, RAW_LR_MID); // 2. 부품 영점 정밀 정렬 매핑
  int x = JOY_MAX - val;              // 3. 기판이 거꾸로 부착된 환경 사양에 맞춘 하드웨어 거울 반전 계산
  if (abs(x - JOY_MID) <= 100) x = JOY_MID; // 정지 상태에서 흘러내리는 현상 원천 봉쇄
  return x;
}

// 손떨림 보정 상자와 영점 조절 연산을 모두 거쳐 깨끗하게 정제된 2번 패들의 세로 Y 좌표 도출
int getFilteredY() {
  int raw = filterUD.update(PIN_UD);  // 1. 세로축 전압 미세 노이즈 평균화 처리
  return mapAxis(raw, RAW_UD_MID);    // 2. 중심점 오차 보정을 적용한 세로축 최종 순수 좌표 반환
}

// 머리끼리 부딪쳐 충돌하는 일을 원천 차단하며 LCD에 띄울 문구를 대기실 예약 메모리 칸에 집어넣는 함수
void requestLCD(const char* text) {
  if (lcdSemaphore == NULL) return; // 열쇠 장치가 작동하지 않는 극초기 국면이라면 즉시 차단
  
  // 10밀리초 타임 이내에 화장실 문 열쇠 쟁취에 성공했다면 내부 진입 허가
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    size_t len = strlen(text); // 전달받은 문자열 데이터의 전체 글자수 연산
    
    // 1줄 최대 스케일인 16자 이하일 땐 첫 줄에 배치하고 아랫줄 방은 깨끗하게 공백 처리
    if (len <= 16) { 
      snprintf(lcdBuf0, 17, "%-16s", text); 
      snprintf(lcdBuf1, 17, "%-16s", ""); 
    } 
    // 16자를 넘어 길게 날아온 문장이라면 정밀하게 16글자 단위로 싹둑 잘라 첫째 줄 둘째 줄 분할 보관
    else {
      char line0[17], line1[17]; 
      strncpy(line0, text, 16);     line0[16] = '\0'; 
      strncpy(line1, text + 16, 16); line1[16] = '\0';
      snprintf(lcdBuf0, 17, "%-16s", line0); 
      snprintf(lcdBuf1, 17, "%-16s", line1);
    }
    lcdPending = true;            // "대기실에 새로운 문자열 적재 완료했으니 화면 새로고침해라" 램프 점등
    xSemaphoreGive(lcdSemaphore); // 볼일 끝났으니 다음 머리를 위해 열쇠 즉각 반납 릴리즈
  }
}

// 오직 1번 머리(loop 메인 루프 코어)의 지령을 받아 전광판 하드웨어 소자에 실제 글자를 주사 출력하는 구동 함수
void renderLCD() {
  if (!lcdPending || lcdSemaphore == NULL) return; // 새로고침할 필요가 전혀 없다면 통신 자원 소모 방지를 위해 즉시 패스
  
  char line0[17], line1[17];
  // 1번 머리가 대기실 공간에 있는 문구를 안전하게 복사해오기 위해 화장실 문 열쇠 점유 획득
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    memcpy(line0, lcdBuf0, 17); memcpy(line1, lcdBuf1, 17); // 대기실 데이터를 내 안전 메모리 스냅샷 공간으로 복사
    lcdPending = false;           // 백업 완수했으니 대기실 처리 완수 상태로 램프 소등
    xSemaphoreGive(lcdSemaphore); // 복사하자마자 무선 수신 전담 코어(Core 0)를 위해 즉각 열쇠 복귀 반납
  } else return; // 열쇠 확보 경쟁에서 밀려났다면 충돌 방지를 위해 이번 루프 사이클은 패스
  
  // 리얼 하드웨어 디스플레이 패널 소자에 최종 글자 마킹 출력
  lcd.clear(); 
  lcd.setCursor(0, 0); lcd.print(line0); 
  lcd.setCursor(0, 1); lcd.print(line1); 
}

// 메인 연산 서버 PC를 타겟으로 삼아 무선 네트워크 데이터 편지(UDP 패킷)를 발송하는 함수
void sendUDP(const String& msg) {
  if (WiFi.status() == WL_CONNECTED) { // 보드가 무선 공유기 안테나망에 정상 가입 연결되어 있을 때만 무선 송출 작동
    udp.beginPacket(serverIP, SEND_PORT); // 봉투 겉면에 목적지 컴퓨터 IP 주소와 지정 출구 포트번호 낙인
    udp.print(msg);                       // 편지 소스 알맹이 주입
    udp.endPacket();                      // 무선 안테나를 울려 실질적 전송 처리 방출
  }
}

// [0번 머리(Core 0) 백그라운드 전담 루프 함수]
// 메인 프로그램 계산 속도가 바쁘든 말든 전혀 방해받지 않는 독립 부서에서, 컴퓨터가 던지는 가동 신호 편지를 실시간 낚아챔
void networkRxLoop(void* pvParameters) {
  char rxBuffer[255]; // 편지가 도착했을 때 텍스트 내용을 받아 적어둘 임시 도화지 버퍼 수신함
  
  for (;;) { // 무한 궤도 순환 (통신 스레드가 파괴되지 않고 백그라운드 감시 체계를 영구 상주 유지하는 구조)
    if (WiFi.status() == WL_CONNECTED) {
      int packetSize = udp.parsePacket(); // 우체통 소켓 통로에 파이썬 서버가 보낸 무선 봉투가 안착했는지 용량 체크
      if (packetSize) { 
        int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1); // 우편물 알맹이 데이터를 메모리에 전부 긁어와 복사
        if (len > 0) rxBuffer[len] = '\0'; // 문장의 맨 끝마크 널 문자를 후미 칸에 삽입하여 문장 종결 마감
        String response = String(rxBuffer); response.trim(); // 수신 데이터에 앞뒤로 섞인 불필요한 줄바꿈이나 공백 필터링 청소

        // [종료 프로토콜 파싱 구역]: 컴퓨터로부터 한 세트 매치 마감 결과 신호("END:LOSE:WIN")를 하달 받았을 때
        if (response.indexOf(':') != -1 && response.indexOf("END") != -1) {
          gState = STATE_END;         // 플레이어 2번 보드의 가동 모드를 즉시 대기 화면 단계(STATE_END)로 이탈 전환
          joystickActive = false;     // 매치가 종료되었으므로 조이스틱 무빙 패들 좌표 송신 자물쇠 잠금
          resetMenuFlags = true;      // 메뉴 튕김 방지 중복 플래그들을 원상태로 클리어하라고 메인 루프에 신호 전달
          
          char part1[10], part2[10], part3[10];
          // 콜론 기호 자리를 정밀 가위질하여 3개 파트로 파싱 분할 수용
          sscanf(response.c_str(), "%[^:]:%[^:]:%[^:]", part1, part2, part3);
          
          // ★1번 마스터 보드와 가장 핵심적인 차별화 코드 포인트!!
          // 2번 게스트 보드는 문자열 3토막 데이터 중 가장 우측 꼬리 칸인 세 번째 방(part3)에 든 본인의 승패 정보 데이터를 택해서 디스플레이 전광판 표출 예약
          requestLCD(("You " + String(part3)).c_str());
          continue; 
        } 
        else if(response == "STP") { // [일시정지 신호 수신 국면 전환]
          gState = STATE_END; joystickActive = false; resetMenuFlags = true;
          requestLCD("READY"); continue;
        }
        else if (response == "SET") { // [마스터 환경설정 진입 메뉴 동기화 수신]
          gState = STATE_SET; joystickActive = false; resetMenuFlags = true;
          requestLCD("SETTING"); continue; 
        }
        else if (response == "SRT") { // [경기 정식 가동 개시 하달 신호 수신]
          gState = STATE_SRT; 
          joystickActive = true;      // 비로소 패들 이동 무선 빗장이 풀려나며 실시간 좌표 전송 엔진 완전 활성화 상태 돌입
          resetMenuFlags = true;
          requestLCD("MATCH P1 vs P2"); continue;
        }
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS); // 멀티태스킹 스케줄러가 코어 0번을 과독점해서 셧다운되지 않게 1밀리초 슬립 휴식 부여
  }
}

// 보드 하드웨어 기판에 전원이 공급되는 최초 순간 작동되어 주변 장치 초기화 및 독립 통신 스레드를 부서 배치하는 공간
void setup() {
  Serial.begin(115200); // 컴퓨터 시리얼 모니터 관측 포트 속도 개방
  
  // 가동 개시 타이밍에 화장실 문잠금 장치(이진 세마포어)를 인스턴스 빌드하고 즉시 풀린 상태(개방 상태)로 설정
  lcdSemaphore = xSemaphoreCreateBinary(); xSemaphoreGive(lcdSemaphore); 
  
  Wire.begin(25, 26); // P2 기판 실측 사양 동기화: 25번 데이터선(SDA), 26번 클럭선(SCL) 구멍으로 I2C 화면선 개통
  lcd.init(); lcd.backlight(); lcd.clear(); // LCD 디스플레이 패널 초기화 및 백라이트 조명 점등

  WiFi.config(local_IP, gateway, subnet); WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(200); } // 와이파이 안테나 마크가 뜰 때까지 0.2초 주기로 뺑뺑이 무한 대기

  analogReadResolution(12); // 전압 감지 변환 해상도를 12비트(최대 4095 정밀 스케일) 규격으로 선언
  pinMode(SW, INPUT_PULLUP); // 비상 강제종료 버튼 구멍을 풀업 저항 인입 모드로 빌드 (안 누르면 5V HIGH, 누르면 그라운드 쇼트로 0V LOW 감출)
  attachInterrupt(digitalPinToInterrupt(SW), clickButton, FALLING); // 전압이 LOW로 완전히 떨어지는 순간 하차 벨 인터럽트 강제 소환 매핑 등록
  udp.begin(RECEIVE_PORT); // 수신 우체통 포트를 활성화하여 파이썬 서버 패킷 대기 국면 진입

  // [체중계 영점 조절 (오토 캘리브레이션)]
  // 조이스틱 레버를 가만히 정지시킨 상황에서 3ms 간격으로 연속 64번 샘플을 빠르게 스캔해 수학적 평균치를 산출함.
  // 이 과정을 거치기 때문에 부품 공정상 발생한 미세 중심 오차가 프로그램 내부에서 2048 완전 중립 영점으로 깔끔히 보정됨.
  long sumLR = 0, sumUD = 0;
  for (int i = 0; i < 64; i++) { sumLR += analogRead(PIN_LR); sumUD += analogRead(PIN_UD); delay(3); }
  RAW_LR_MID = (int)(sumLR / 64);
  RAW_UD_MID = (int)(sumUD / 64);

  filterLR.init(PIN_LR); filterUD.init(PIN_UD); // 두 축의 손떨림 보정 상자 내부를 초기 전압으로 채움 완수

  // [FreeRTOS 독립 부서 배치 명령]:
  // 무선 편지 수신 함수(`networkRxLoop`)를 메인 로직 코어와 완전히 절연된 부서인 '0번 머리(Core 0번 프로세서)'에 영구 상주 독립 배치함
  xTaskCreatePinnedToCore(networkRxLoop, "NetworkRxTask", 10000, NULL, 1, &NetworkRxTask, 0);
  requestLCD("P2 CONTROLLER"); // 메인 전광판에 플레이어 2번 기기 가동 시작 알림 안내 출력 예약
}

// 오직 1번 머리(Core 1번 마이크로프로세서)에서만 루프 순환 구동되며 실시간 패들 조이스틱 좌표 송출을 전담하는 메인 루프 스레드
void loop() {
  renderLCD(); // 매 주기 진입 타이밍마다 통신 코어가 대기실에 토스해둔 LCD 예약 문자열이 포착되면 디스플레이 전광판에 실제 주사

  // [플레이어 2번 전용 비상 탈출 버튼 매커니즘]:
  // 한창 격렬하게 플레이 게임 대결 배틀을 치르던 도중, 2번 유저가 버튼을 눌러 하차 벨 인터럽트를 가동시켰다면 진입
  if (buttonClicked) {
    buttonClicked = false; // 플래그를 원래 정상 상태인 false로 즉시 복원 (다음 긴급 탈출 클릭을 대기해야 하므로)
    
    if (gState == STATE_SRT) { // 진짜 한창 대결 게임 도중에 터진 유효 상황이 맞는지 안전 진단
      Serial.println("[인터럽트 발생] P2 강제 종료 시도");
      sendUDP("END\n"); // 메인 파이썬 서버 컴퓨터 측에 판을 깨서 게임을 즉시 비상 종료하겠다는 타격 편지 발송
      gState = STATE_END; joystickActive = false; resetMenuFlags = true; // 본인 보드 제어 클럭 상태도 대기 국면으로 긴급 대피
    }
  }

  // 조이스틱 레버를 한 방향으로 길게 밀고 있을 때 무선 전송 버퍼에 연속 편지가 과부하로 터지는 것을 막는 중복 플래그 스토퍼 변수
  static bool lastUpState = false;
  if (resetMenuFlags) { lastUpState = false; resetMenuFlags = false; lastSentX = JOY_MID; lastSentY = JOY_MID; }

  int cx = getFilteredX(); // 손떨림 보정, 영점 정렬, 거울 반전 계산이 수료 완료된 최정예 가로 패들 좌표 도출
  int cy = getFilteredY(); // 손떨림 보정, 영점 정렬 계산이 수료 완료된 최정예 세로 패들 좌표 도출

  // 대기 화면(STATE_END) 국면 상태일 때, 조이스틱 레버를 상단 기준선 이상으로 툭 밀어 올리면 대결 개시 시작 편지를 전달
  if (gState == STATE_END) {
    bool isUp = (cy < JOY_MID - JOY_THRESH); // 세로축 좌표가 위쪽 격발 임계 경계선 안쪽으로 진입했는지 연산 판별
    if (isUp && !lastUpState) sendUDP("SRT\n"); // 처음으로 밀어 올린 그 진입 시점 타이밍에 딱 1번만 무선 시작 요청 패킷 발송
    lastUpState = isUp;
  }
  // 정식 매치가 활성화된 실시간 좌표 송출 무선 제어 구역
  else if (gState == STATE_SRT && joystickActive) {
    // 마지막 무선 편지 발송 시점 대비 딱 10ms 이상의 시간이 시계상 경과했는지 점검 (초정밀 10ms 단위 하이퍼 전송 주기 확립)
    if (millis() - tLastSend >= SEND_INTERVAL_MS) {
      
      // 손떨림 필터링: 바로 직전 텀에 발사했던 좌표 수치 내역 대비 최소 8 칸을 초과하는 진짜 의미 있는 유저의 큰 조작 이동이 생겼는가 점검
      if (abs(cx - lastSentX) > CHANGE_THRESH || abs(cy - lastSentY) > CHANGE_THRESH) {
        char packet[16];
        
        // [중요 정책 보정]: 보드 내에서 인위적인 가동 좌표 한계 브레이크를 걸지 않고 최소 0부터 최대 4095 전 범위를 편지로 송출함.
        // 수신부 파이썬 프로그램은 이 전체 영역 데이터를 화면 크기에 맞춰서 넓게 사용하므로, 2번 패들이 하프라인(중앙선) 위쪽 경계면을 돌파하여 넘어갈 수 있는 유연한 게임 규칙 구현 파트.
        snprintf(packet, sizeof(packet), "%d:%d\n", cx, cy);
        sendUDP(packet); 
        lastSentX = cx; lastSentY = cy; // 다음 루프 돌 때의 손떨림 진단을 위해 올드 데이터 백업 칸에 저장 업데이트
      }
      tLastSend = millis(); // 타이머 시간 계산의 다음 기준점을 방금 처리가 터진 현재 스톱워치 타임으로 새롭게 리셋 갱신
    }
  }
}
