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

#include <WiFi.h>              // 와이파이 안테나 기지국 무선 제어 라이브러리
#include <WiFiUdp.h>           // 딜레이 프리 UDP 데이터 무선 패킷 처리 라이브러리
#include <Wire.h>              // I2C 직렬 통신망 버스 제어 라이브러리
#include <LiquidCrystal_I2C.h> // 16x2 캐릭터 텍스트 디스플레이 출력 라이브러리

// 접속할 공유기(AP)의 와이파이 식별 아이디 및 패스워드 설정
const char* ssid     = "fusion";
const char* password = "12345678";

// 플레이어 2번 게스트 보드가 독자적으로 점유할 전용 고정 네트워크 IP 세팅
IPAddress local_IP(192, 168, 0, 154); // P2 보드 본인의 고유 IP
IPAddress gateway(192, 168, 0, 1);    // 공유기 게이트웨이 주소
IPAddress subnet(255, 255, 255, 0);   // 서브넷 구조 마스크
IPAddress serverIP(192, 168, 0, 138); // 메인 파이썬 연산 서버 PC IP

// 마스터 1번 보드와 겹치지 않도록 철저히 격리 분리한 2번 보드 고유의 소켓 통신 포트
const int SEND_PORT    = 10002;   // 플레이어 2번 패들의 실시간 무빙 좌표를 PC로 쏘는 출구 포트
const int RECEIVE_PORT = 10004;   // 컴퓨터측에서 정산된 2번 전용 가용 데이터를 수신받는 입구 포트

WiFiUDP udp; // 실질적인 무선 데이터 트랜잭션을 담달할 통신 객체

// 하드웨어 아날로그 조이스틱 및 인터럽트 스위치 배치 규격 (실측 정보 일치)
const int PIN_LR = 35;   // 가로 X축 전압 스캔 센서 입력 핀
const int PIN_UD = 32;   // 세로 Y축 전압 스캔 센서 입력 핀
const int SW     = 14;   // 비상 상황 발생 시 경기를 터뜨릴 강제 종료 버튼 핀

LiquidCrystal_I2C lcd(0x27, 16, 2);

// 신호 규격화 및 노이즈 억제 상수 정의 구역
#define JOY_MID          2048 // 아날로그 전압 중심 스케일의 중간 영점값
#define JOY_MAX          4095 // 아날로그 입력 범위의 한계 최대값
#define JOY_THRESH        800 // 대기 상태에서 시작 입력을 판별할 조이스틱 기울기 컷트라인
#define SEND_INTERVAL_MS   10 // 마스터와 동기화된 초고속 10ms 패킷 무선 전송 주기
#define CHANGE_THRESH       8 // 조이스틱 노이즈 잔떨림을 차단할 최소 변화량 기준 수치
#define FILTER_SIZE         8 // 이동평균 필터 내부 버퍼에 기억할 샘플 누적 메모리 개수
#define JOY_DEADZONE      150 // 손을 떼고 정지 상태에 있을 때 패들의 유령 움직임을 막는 무반응 안전지대

// [이동평균 필터 구조체]
// 2번 조이스틱 센서의 전압 저항 미세 잡음을 8칸 순환 평균 연산으로 깎아내어 패들 서스펜션을 매우 부드럽게 유지
struct AxisFilter {
  int  buf[FILTER_SIZE]; // 최근 8번 계측한 전압 내역 보관용 원형 메모리 버퍼
  long sum;              // 버퍼 내에 누적 탑재된 모든 값들의 합산 변수
  int  idx;              // 가장 과거 기록의 인덱스를 짚어내는 배열 포인터
  
  // 최초 시동 시 필터 내부 버퍼 공간을 현재 초기 전압 데이터로 가득 채워두는 함수
  void init(int pin) {
    int v = analogRead(pin); sum = 0; for (int i = 0; i < FILTER_SIZE; i++) { buf[i] = v; sum += v; } idx = 0;
  }
  // 매 가동 주기마다 가장 오래된 옛날 기록을 버리고 최신값으로 업데이트하며 실시간 평균 반환
  int update(int pin) {
    sum -= buf[idx]; buf[idx] = analogRead(pin); sum += buf[idx];
    idx = (idx + 1) % FILTER_SIZE; // 포인터를 한 칸 전진시키되 8번 방에 도달하면 사이클 구조로 0번 복귀
    return (int)(sum / FILTER_SIZE);
  }
};

AxisFilter filterLR; // 플레이어 2번 가로축 전용 전압 노이즈 제거 필터
AxisFilter filterUD; // 플레이어 2번 세로축 전용 전압 노이즈 제거 필터

int RAW_LR_MID = 2048; // 부팅 시점 2번 하드웨어 부품 고유의 물리 가로축 중립 실측값
int RAW_UD_MID = 2048; // 부팅 시점 2번 하드웨어 부품 고유의 물리 세로축 중립 실측값

// 매치 게임의 전개 단계를 감지할 4대 상태 보관 메모리
enum GameState { STATE_END, STATE_SET, STATE_SRT, STATE_STP };
volatile GameState gState = STATE_END;     // 교차 코어가 접근하므로 volatile 조치
volatile bool joystickActive = false;      // 실경기 하달 신호 접수 전까지 무선 송출 방화벽 역할을 하는 락 플래그
volatile bool resetMenuFlags = false;      // 연타 오작동 매칭을 소거하기 위해 내부 레지스터를 정리해주는 플래그

// [LCD 멀티코어 이진 세마포어 배타적 제어 장치]
// 코어 0번과 코어 1번 프로세서가 LCD 전광판 메모리 주소 영역에 동시 침범해 폰트가 깨지는 현상을 완벽 방어하는 잠금 기법
SemaphoreHandle_t lcdSemaphore = NULL;     // 세마포어 안전 열쇠 레지스터
volatile bool lcdPending   = false;        // "출력 보류 중인 따끈따끈한 새 문자열 패키지가 대기 중이다" 신호등 플래그
char lcdBuf0[17]  = "                ";    // LCD 윗줄 대응 임시 적재 공간 버퍼
char lcdBuf1[17]  = "                ";    // LCD 아랫줄 대응 임시 적재 공간 버퍼

// 플레이어 2번 비상 탈출 스위치 전용 디바운싱(하드웨어 채터링 방지) 레지스터
volatile bool buttonClicked = false;       // 버튼 승인 처리가 완수되었음을 loop 스레드에 인계하는 플래그
volatile unsigned long lastInterruptTime = 0; // 버튼 승인이 가동된 마지막 시점의 내장 스톱워치 타임스탬프
#define DEBOUNCE_TIME_MS 250               // 쿨타임 (물리 판때기가 마구 부딪치며 파르르 떨리는 0.25초 동안의 떨림은 가차 없이 차단)

unsigned long tLastSend = 0; // 무선 패킷을 컴퓨터로 방출했던 과거 시점의 마지막 내장 스톱워치 타임 변수
int lastSentX = JOY_MID;     // 손떨림 검증 처리를 위해 직전 타이밍에 발사했던 옛날 X 좌표 백업 메모리
int lastSentY = JOY_MID;     // 손떨림 검증 처리를 위해 직전 타이밍에 발사했던 옛날 Y 좌표 백업 메모리

TaskHandle_t NetworkRxTask; // 코어 0번 프로세서 자원을 단독 소모하여 무선 수신만을 실시간 처리할 전담 매니저 태스크 이름표

// [버튼 인터럽트 감지 루틴]
// 플레이어 2번 유저가 경기 진행 도중 강제로 판을 깨고 이탈하고 싶을 때 버튼을 누르면, 최우선으로 연산을 가로채 이쪽으로 소환 처리
void IRAM_ATTR clickButton() {
  unsigned long now = millis(); // 하드웨어 타이머 기반 스톱워치로부터 부팅 이후 경과된 현재 밀리초 획득
  // 현재 스톱워치 타임에서 마지막 승인 시점을 뺀 간격이 쿨타임 기준선(250ms)을 돌파했는지 진단
  if (now - lastInterruptTime > DEBOUNCE_TIME_MS) {
    buttonClicked = true;       // 기계 채터링 잡음이 아닌 유저 의지가 실린 진짜 비상 탈출 클릭으로 최종 공인
    lastInterruptTime = now;    // 유효 승인 과거 기록을 방금 클릭이 발생한 현재 시점으로 동기화 갱신
  }
}

// 조이스틱 하드웨어 부품 자체 편차로 중심 전압이 조금 틀어져 있는 사양을 수학적으로 완전 교정하는 영점 함수
int mapAxis(int raw, int midVal) {
  if (abs(raw - midVal) <= JOY_DEADZONE) return JOY_MID; // 중립 대기 가이드 영역 내부에 안착 중이라면 강제 정지값(2048) 반환
  int mapped;
  if (raw < midVal) mapped = map(raw, 0, midVal - JOY_DEADZONE, 0, JOY_MID); // 하프 미만 영역 비례식 대입 변환
  else mapped = map(raw, midVal + JOY_DEADZONE, JOY_MAX, JOY_MID, JOY_MAX); // 하프 초과 영역 비례식 대입 변환
  return constrain(mapped, 0, JOY_MAX); // 비례 변환 결과 수치가 가동 범위 0~4095 바깥으로 오버플로우 되지 않게 최종 마감 후 반환
}

// 노이즈 필터 및 반전 기하학 계산을 통과하여 정제 가공된 플레이어 2번 패들의 클린 가로 X 좌표 도출
int getFilteredX() {
  int raw = filterLR.update(PIN_LR);  // 1. 가로축 핀 전압 미세 진동 노이즈 제거
  int val = mapAxis(raw, RAW_LR_MID); // 2. 부품 영점 정밀 보정 매핑
  int x = JOY_MAX - val;              // 3. 기판 거꾸로 부착된 환경 특성에 맞춘 하드웨어 거울 반전 연산
  if (abs(x - JOY_MID) <= 100) x = JOY_MID; // 중립 근방 손떨림 유령 무빙 버그 원천 봉쇄 데드존 설정
  return x;
}

// 필터링 및 중심 영점 보정 연산을 모두 수료하여 투명하게 정제된 플레이어 2번 패들의 세로 Y 좌표 도출
int getFilteredY() {
  int raw = filterUD.update(PIN_UD);  // 1. 세로축 전압 전기 노이즈 필터 연산
  return mapAxis(raw, RAW_UD_MID);    // 2. 중심점 편차 교정 수식을 적용한 세로축 최종 순수 좌표 반환
}

// 교차 코어 간 충돌 시그널을 완전 차단하며 LCD 출력 폰트 데이터를 예약 버퍼 슬롯에 탑재해두는 함수
void requestLCD(const char* text) {
  if (lcdSemaphore == NULL) return; // 열쇠 장치가 빌드되지 않은 극초기 시점이라면 시스템 다운 예방을 위해 전면 차단
  
  // 10밀리초 이내 타임 텀 안에서 이진 세마포어 키 쟁취에 성공했다면 잠금 열고 내부 진입
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    size_t len = strlen(text); // 전달받은 문자열 데이터의 물리 한계 길이 연산
    
    // 1줄 최대 규격인 16자 이하일 시 윗줄에 배치하고 아래 레이아웃 공간은 빈 공백 처리
    if (len <= 16) { 
      snprintf(lcdBuf0, 17, "%-16s", text); 
      snprintf(lcdBuf1, 17, "%-16s", ""); 
    } 
    // 16자를 초과해 길게 날아온 문자열 패키지라면 정밀하게 16글자 시점으로 칼자루 재단하여 윗줄 아랫줄 분할 보관
    else {
      char line0[17], line1[17]; 
      strncpy(line0, text, 16);     line0[16] = '\0'; 
      strncpy(line1, text + 16, 16); line1[16] = '\0';
      snprintf(lcdBuf0, 17, "%-16s", line0); 
      snprintf(lcdBuf1, 17, "%-16s", line1);
    }
    lcdPending = true;            // "출력 보류 중인 신형 문자 패키지 메모리 로딩 완료" 하드웨어 시그널 램프 점등
    xSemaphoreGive(lcdSemaphore); // 다른 작업을 맡은 코어를 위해 세마포어 소유권 즉각 릴리즈 반납
  }
}

// 오직 Core 1번(loop 제어 메인 루프)의 통제를 받아 전광판 하드웨어에 I2C 직렬 버스로 실제 글자를 주사 출력하는 함수
void renderLCD() {
  if (!lcdPending || lcdSemaphore == NULL) return; // 새로고침 요건이 아예 없거나 키가 망가졌다면 무의미한 직렬 통신 전력 소모 즉각 컷
  
  char line0[17], line1[17];
  // 코어 1번 프로세서가 대기 영역 문자열을 안전 수용해 오기 위해 세마포어 잠금 열쇠 획득 시도
  if (xSemaphoreTake(lcdSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    memcpy(line0, lcdBuf0, 17); memcpy(line1, lcdBuf1, 17); // 임시 보관 버퍼 데이터를 로컬 스냅샷 공간으로 복사
    lcdPending = false;           // 임무 완수했으므로 대기 플래그 램프 소등
    xSemaphoreGive(lcdSemaphore); // 복사하자마자 무선 수신 전담 코어(Core 0)를 위해 열쇠 원위치 반납
  } else return; // 세마포어 권한 확보 경쟁에서 졌다면 충돌 차단을 위해 이번 루프 텀은 패스
  
  // 물리 하드웨어 디스플레이 패널 소자에 리얼 폰트 스트림 주사 갱신
  lcd.clear(); 
  lcd.setCursor(0, 0); lcd.print(line0); // 1행 텍스트 마킹
  lcd.setCursor(0, 1); lcd.print(line1); // 2행 텍스트 마킹
}

// 파이썬 메인 연산 컴퓨터 서버의 개방 게이트 포트를 타겟 삼아 무선 UDP 데이터 패킷을 사격 송신하는 함수
void sendUDP(const String& msg) {
  if (WiFi.status() == WL_CONNECTED) { // 보드의 안테나가 무선 인터넷 공유기 기지에 정상 바인딩되어 있을 때만 무선 송출 가동
    udp.beginPacket(serverIP, SEND_PORT); // 봉투 외피에 목적지 PC IP 정보와 전용 포트 주소 낙인
    udp.print(msg);                       // 메시지 소스 본체 주입
    udp.endPacket();                      // 무선 안테나 하드웨어 칩셋으로 실질적 전송 처리 방출
  }
}

// [Core 0번 프로세서 완전 전담 백그라운드 루프 함수]
// 메인 계산이나 패들 무빙 연산 속도가 바쁘든 말든, 방해받지 않는 독립 부서에서 컴퓨터가 던져주는 무선 지령 패킷을 실시간 가로챔
void networkRxLoop(void* pvParameters) {
  char rxBuffer[255]; // 패킷이 도달했을 때 텍스트 소스를 받아 적어둘 임시 도화지 수신 버퍼
  
  for (;;) { // 무한 궤도 (통신 스레드가 파괴되지 않고 백그라운드 감시 체계를 항시 영구 상주 유지)
    if (WiFi.status() == WL_CONNECTED) {
      int packetSize = udp.parsePacket(); // 우체통 구멍 통로에 파이썬 서버 컴퓨터가 보낸 무선 봉투가 안착했는지 용량 체크
      if (packetSize) { 
        int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1); // 우편물 알맹이 데이터를 메모리에 전부 긁어와 복사
        if (len > 0) rxBuffer[len] = '\0'; // 문자열 종결 부호인 널 문자를 가장 꼬리 칸에 삽입 마감
        String response = String(rxBuffer); response.trim(); // 수신 원본 데이터에 섞인 줄바꿈이나 공백 필터링 청소

        // [종료 패킷 파싱 구역]: 메인 컴퓨터로부터 세트 경기 마감 결과 시그널("END:LOSE:WIN")을 하달 받았을 때
        if (response.indexOf(':') != -1 && response.indexOf("END") != -1) {
          gState = STATE_END;         // 플레이어 2번 보드의 가동 모드를 즉시 대기 국면(STATE_END)으로 변조 전환
          joystickActive = false;     // 매치가 완전히 종료되었으므로 조이스틱 무빙 패들 좌표 송신 무력화 잠금
          resetMenuFlags = true;      // 메뉴 튕김 방지 중복 플래그들을 원상태로 클리어하라고 신호 전달
          
          char part1[10], part2[10], part3[10];
          // 콜론 기호 자리를 정밀 가위질 재단하여 3개 파트로 파싱 분할 수용
          sscanf(response.c_str(), "%[^:]:%[^:]:%[^:]", part1, part2, part3);
          
          // ★1번 마스터와 핵심 차별화 코드 부각!!
          // 2번 게스트 보드는 문자열 3분할 데이터 중 가장 우측 꼬리 칸인 세 번째 방(part3)에 든 본인의 승패 정보 데이터를 택해서 디스플레이 예약 표출
          requestLCD(("You " + String(part3)).c_str());
          continue; 
        } 
        else if(response == "STP") { // [일시정지 수신 국면 전환]
          gState = STATE_END; joystickActive = false; resetMenuFlags = true;
          requestLCD("READY"); continue;
        }
        else if (response == "SET") { // [마스터 환경설정 진입 동기화 수신]
          gState = STATE_SET; joystickActive = false; resetMenuFlags = true;
          requestLCD("SETTING"); continue; 
        }
        else if (response == "SRT") { // [경기 정식 가동 개시 하달 사인 수신]
          gState = STATE_SRT; 
          joystickActive = true;      // 비로소 패들 이동 방화벽 잠금이 풀려나며 실시간 좌표 전송 무선 엔진 활성화
          resetMenuFlags = true;
          requestLCD("MATCH P1 vs P2"); continue;
        }
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS); // 프리알티오에스 멀티태스킹 스케줄러가 코어 0번을 독점해 셧다운되지 않게 1ms 브레이크 슬립 부여
  }
}

// 보드 하드웨어 기판에 전원이 공급되는 최초 시점 일 회 순행 작동되어 주변 소자 초기화 및 독립 통신 스레드를 부서 배치하는 공간
void setup() {
  Serial.begin(115200); // 컴퓨터 시리얼 모니터 디버깅 관측 포트 전격 개방
  
  // 가동 개시 타이밍에 이진 세마포어 키 변수를 인스턴스화하고 풀린 상태(개방 상태)로 릴리즈 처리
  lcdSemaphore = xSemaphoreCreateBinary(); xSemaphoreGive(lcdSemaphore); 
  
  Wire.begin(25, 26); // P2 기판 실측 사양 동기화: 25번 데이터선(SDA), 26번 클럭선(SCL)으로 매핑하여 I2C 전격 개통
  lcd.init(); lcd.backlight(); lcd.clear(); // LCD 내부 기판 소자 전압 안정화 및 화사하게 백라이트 조명 점등

  WiFi.config(local_IP, gateway, subnet); WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(200); } // 와이파이 연결 마크가 뜰 때까지 0.2초 주기로 뺑뺑이 무한 대기

  analogReadResolution(12); // 전압 스캔 ADC 해상도를 12비트(최대 4095 정밀 스케일) 규격으로 선언
  pinMode(SW, INPUT_PULLUP); // 비상 종료 스위치 핀을 풀업 저항 인입 모드로 빌드 (안 누르면 5V HIGH, 누르면 그라운드 쇼트로 0V LOW 검출)
  attachInterrupt(digitalPinToInterrupt(SW), clickButton, FALLING); // 전압이 LOW로 완전히 떨어지는 하강 엣지 타이밍에 즉시 감수 호출 인터럽트 등록
  udp.begin(RECEIVE_PORT); // 수신 우체통 포트를 활성화하여 파이썬 서버 패킷 대기 국면 진입

  // [부품 자동 영점 조절 (오토 캘리브레이션)]:
  // 조이스틱을 가만히 정지시킨 상황에서 3ms 간격으로 연속 64번 샘플을 계측 스캔해 수학적 평균치를 산출함
  // 이 과정을 거치기 때문에 부품 제조 공정상 발생한 미세 중심 전압 오차가 프로그램 단에서 2048 완전 중립 영점으로 완전 보정됨
  long sumLR = 0, sumUD = 0;
  for (int i = 0; i < 64; i++) { sumLR += analogRead(PIN_LR); sumUD += analogRead(PIN_UD); delay(3); }
  RAW_LR_MID = (int)(sumLR / 64);
  RAW_UD_MID = (int)(sumUD / 64);

  filterLR.init(PIN_LR); filterUD.init(PIN_UD); // 두 축의 이동평균 필터 버퍼 초기 충전 완수

  // [FreeRTOS 멀티코어 독립 태스크 배치 명령]:
  // networkRxLoop 무선 수신 전담 연산 함수를 메인 로직이 구동되는 코어 1번과 완전히 절연된 부서인 'Core 0번 프로세서'에 영구 상주 독립 배치함
  xTaskCreatePinnedToCore(networkRxLoop, "NetworkRxTask", 10000, NULL, 1, &NetworkRxTask, 0);
  requestLCD("P2 CONTROLLER"); // 메인 전광판에 플레이어 2번 기기 가동 시작 알림 안내 출력 예약
}

// 오직 Core 1번 마이크로프로세서에서만 루프 순환 구동되며 실시간 패들 조이스틱 좌표 송출을 전담하는 메인 루프 스레드
void loop() {
  renderLCD(); // 매 루프 사이클 주기 진입 타이밍마다 통신 코어가 토스해둔 LCD 예약 문자열이 포착되면 디스플레이 전광판에 실제 물리 주사

  // [플레이어 2번 전용 비상 탈출 인터럽트 처리기]:
  // 한창 격렬하게 플레이 게임 배틀을 치르던 도중, 2번 유저가 버튼을 눌러 인터럽트를 발동시켰다면 진입
  if (buttonClicked) {
    buttonClicked = false; // 플래그를 원래 정상 상태인 false로 즉시 복원 (다음 긴급 탈출 클릭을 대기해야 하므로)
    
    if (gState == STATE_SRT) { // 진짜 한창 배틀 게임 도중에 터진 유효 상황이 맞는지 안전 진단
      Serial.println("[인터럽트 발생] P2 강제 종료 시도");
      sendUDP("END\n"); // 메인 파이썬 서버 측에 판을 깨서 게임을 즉시 비상 종료하겠다는 타격 패킷 발송
      gState = STATE_END; joystickActive = false; resetMenuFlags = true; // 본인 보드 제어 클럭 상태도 대기 국면으로 긴급 대피
    }
  }

  // 조이스틱 레버를 한 방향으로 길게 밀고 있을 때 무선 전송 버퍼에 연속 데이터 과부하 버그가 터지는 것을 막는 중복 플래그 스토퍼 변수
  static bool lastUpState = false;
  if (resetMenuFlags) { lastUpState = false; resetMenuFlags = false; lastSentX = JOY_MID; lastSentY = JOY_MID; }

  int cx = getFilteredX(); // 필터, 영점 정렬, 거울 반전 계산이 수료 완료된 최정예 가로 패들 좌표 도출
  int cy = getFilteredY(); // 필터, 영점 정렬 계산이 수료 완료된 최정예 세로 패들 좌표 도출

  // 대기 화면(STATE_END) 국면 상태일 때, 조이스틱 레버를 상단 기준선 이상으로 툭 밀어 올리면 대결 개시 명령을 전달
  if (gState == STATE_END) {
    bool isUp = (cy < JOY_MID - JOY_THRESH); // 세로축 좌표가 위쪽 격발 임계 경계선 안쪽으로 진입했는지 연산 판별
    if (isUp && !lastUpState) sendUDP("SRT\n"); // 처음으로 밀어 올린 그 진입 시점 찰나 타이밍에 딱 1번만 무선 시작 요청 패킷 발송
    lastUpState = isUp;
  }
  // 파이썬 메인 서버로부터 정식 스타트 지령을 받아 조이스틱 잠금이 해제된 실시간 대결 상태
  else if (gState == STATE_SRT && joystickActive) {
    // 마지막 무선 통신 발생 기준 시점 대비 딱 10ms 이상의 시간이 시계상 경과했는지 점검 (초정밀 10ms 단위 하이퍼 전송 주기 확립)
    if (millis() - tLastSend >= SEND_INTERVAL_MS) {
      
      // 손떨림 필터링: 바로 직전 텀에 발사했던 좌표 수치 내역 대비 최소 8 단위를 초과하는 진짜 의미 있는 유저의 큰 조작 이동이 생겼는가 점검
      if (abs(cx - lastSentX) > CHANGE_THRESH || abs(cy - lastSentY) > CHANGE_THRESH) {
        char packet[16];
        
        // 중요 정책 보정: 보드 내에서 인위적인 가동 좌표 한계 브레이크를 걸지 않고 최소 0부터 최대 4095 전 범위를 유디피 사격 송출
        // 수신부 파이썬 프로그램은 이 전체 영역 데이터를 화면 크기에 맞춰 맵핑하므로, 2번 패들이 하프라인(중앙선) 위쪽 경계면을 돌파하여 넘어갈 수 있는 유연한 게임 규칙 구현 파트
        snprintf(packet, sizeof(packet), "%d:%d\n", cx, cy);
        sendUDP(packet); 
        lastSentX = cx; lastSentY = cy; // 다음 루프 사이클 회전 시의 손떨림 진단을 위해 올드 데이터 백업 칸에 저장 업데이트
      }
      tLastSend = millis(); // 타이머 시간 계산의 다음 기준점을 방금 처리가 터진 현재 스톱워치 타임으로 새롭게 리셋 갱신
    }
  }
}