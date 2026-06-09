/*
 * ╔══════════════════════════════════════════════════════╗
 * ESP32 핀볼 [P1 보드] - 조이스틱 축별 정밀 중립점(2048) 강제 고정본
 * (고정 IP: 192.168.0.200 / Python PC: 192.168.0.138)
 * ╚══════════════════════════════════════════════════════╝
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const char* ssid     = "fusion";
const char* password = "12345678";

IPAddress local_IP(192, 168, 0, 200);   
IPAddress gateway(192, 168, 0, 1);     
IPAddress subnet(255, 255, 255, 0);    

IPAddress serverIP(192, 168, 0, 138);   

const int SEND_PORT    = 10001;         
const int RECEIVE_PORT = 10003;         

WiFiUDP udp;

const int VRX = 32;
const int VRY = 35;
const int SW  = 14; 

LiquidCrystal_I2C lcd(0x27, 16, 2);

// 소프트웨어 기준의 상향/하향 스케일용 매크로
#define JOY_MID       2048   
#define JOY_THRESH     800   
#define SEND_INTERVAL_MS 30   

// [노이즈 필터 파라미터 세팅]
#define FILTER_SIZE 4      
#define CHANGE_THRESH 15   

// ★ [하드웨어 정밀 중립점 데이터 세팅] - 사용자 로그 기준 반영
#define RAW_X_MIN    0
#define RAW_X_MID    568    // X축 실제 물리 중립값
#define RAW_X_MAX    3723

#define RAW_Y_MIN    0
#define RAW_Y_MID    2929   // Y축 실제 물리 중립값
#define RAW_Y_MAX    3723

enum GameState {
  STATE_END,  
  STATE_SET,  
  STATE_SRT   
};
volatile GameState gState = STATE_END; 
volatile bool joystickActive = false; 
volatile int setMenuCursor = 0; 

volatile bool resetMenuFlags = false;
volatile bool lcdUpdateReq = false;
char lcdBuf0[32] = "MAIN MENU";
char lcdBuf1[32] = "READY...";

volatile bool          buttonClicked = false;
volatile unsigned long lastInterruptTime = 0;
#define DEBOUNCE_TIME_MS 250 

unsigned long tLastSend = 0;
TaskHandle_t NetworkRxTask;

int lastSentX = JOY_MID;
int lastSentY = JOY_MID;

// ════════════════════════════════════════════
//  ★ 분할 매핑 알고리즘이 적용된 필터 함수
// ════════════════════════════════════════════
int getFilteredX() {
  static int readings[FILTER_SIZE] = {RAW_X_MID, RAW_X_MID, RAW_X_MID, RAW_X_MID};
  static int index = 0;
  static int sum = RAW_X_MID * FILTER_SIZE;

  sum -= readings[index];
  readings[index] = analogRead(VRX);
  sum += readings[index];
  index = (index + 1) % FILTER_SIZE;

  int raw_avg = sum / FILTER_SIZE;
  int mapped_x = JOY_MID;

  // 중립점(568)을 기준으로 왼쪽/오른쪽을 나누어 2048로 강제 매핑합니다.
  if (raw_avg < RAW_X_MID) {
    mapped_x = map(raw_avg, RAW_X_MIN, RAW_X_MID, 0, JOY_MID);
  } else {
    mapped_x = map(raw_avg, RAW_X_MID, RAW_X_MAX, JOY_MID, 4095);
  }
  return constrain(mapped_x, 0, 4095);
}

int getFilteredY() {
  static int readings[FILTER_SIZE] = {RAW_Y_MID, RAW_Y_MID, RAW_Y_MID, RAW_Y_MID};
  static int index = 0;
  static int sum = RAW_Y_MID * FILTER_SIZE;

  sum -= readings[index];
  readings[index] = analogRead(VRY);
  sum += readings[index];
  index = (index + 1) % FILTER_SIZE;

  int raw_avg = sum / FILTER_SIZE;
  int mapped_y = JOY_MID;

  // 중립점(2929)을 기준으로 위/아래를 나누어 2048로 강제 매핑합니다.
  if (raw_avg < RAW_Y_MID) {
    mapped_y = map(raw_avg, RAW_Y_MIN, RAW_Y_MID, 0, JOY_MID);
  } else {
    mapped_y = map(raw_avg, RAW_Y_MID, RAW_Y_MAX, JOY_MID, 4095);
  }
  return constrain(mapped_y, 0, 4095);
}

void renderLCD() {
  if (!lcdUpdateReq) return;
  lcd.setCursor(0, 0); lcd.print(lcdBuf0);
  lcd.setCursor(0, 1); lcd.print(lcdBuf1);
  lcdUpdateReq = false;
}

void requestLCD(const char* line0, const char* line1) {
  snprintf(lcdBuf0, 17, "%-16s", line0);
  snprintf(lcdBuf1, 17, "%-16s", line1);
  lcdUpdateReq = true;
}

void sendUDP(String msg) {
  udp.beginPacket(serverIP, SEND_PORT);
  udp.print(msg);
  udp.endPacket();
  
  if (msg.indexOf(':') == -1) { // 파이썬 포맷 호환을 위해 세모콜론 검사로 수정
    String logMsg = msg;
    logMsg.trim();
    Serial.print("[UDP 상태 송신] -> "); 
    Serial.println(logMsg);
  }
}

void sendJoystickNow() {
  int x = getFilteredX();
  int y = getFilteredY();
  String msg = String(x) + ":" + String(y) + "\n";
  sendUDP(msg);
  lastSentX = x;
  lastSentY = y;
}

void IRAM_ATTR clickButton() {
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > DEBOUNCE_TIME_MS) {
    buttonClicked = true;
    lastInterruptTime = interruptTime;
  }
}

void networkRxLoop(void * pvParameters) {
  char rxBuffer[255];
  for(;;) {
    int packetSize = udp.parsePacket();
    if (packetSize) {
      int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
      if (len > 0) rxBuffer[len] = '\0';

      String response = String(rxBuffer);
      response.trim();

      Serial.print("📥 [UDP 수신] <- 파이썬 데이터: ["); 
      Serial.print(response); 
      Serial.println("]");

      if (response == "END") {
        gState = STATE_END;
        joystickActive = false; 
        resetMenuFlags = true; 
        requestLCD("MAIN MENU", "READY...");
      } 
      else if (response == "SET") {
        gState = STATE_SET;
        joystickActive = false; 
        setMenuCursor = 0; 
        requestLCD("> 1. OPTION SET", "  2. BACK");
      } 
      else if (response == "SRT") {
        if (gState == STATE_END) { 
          gState = STATE_SRT;
          joystickActive = true;  
          requestLCD("PLAYING GAME!!", "P1 vs P2 MATCH");
          
          Serial.println("🎮 [시스템] SRT 수신완료! 조이스틱 제어 활성화.");
          tLastSend = millis(); 
          delay(5);
          sendJoystickNow();
        } else {
          Serial.print("⚠️ [방어 방지] 현재 상태는 ");
          Serial.print(gState == STATE_SET ? "설정창(SET)" : "게임중(SRT)");
          Serial.println(" 이므로 SRT 진입 요청을 차단합니다.");
        }
      }
      else if (response.startsWith("WIN") || response.startsWith("LOSE") || response.startsWith("DRAW")) {
        joystickActive = false;
        requestLCD("GAME OVER", response.c_str());
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS); 
  }
}

void setup() {
  Serial.begin(115200);
  delay(300); 

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  
  requestLCD("MAIN MENU", "P1: 192.168.0.200");
  renderLCD();

  pinMode(SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SW), clickButton, FALLING);
  analogReadResolution(12); 

  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  udp.begin(RECEIVE_PORT);

  xTaskCreatePinnedToCore(networkRxLoop, "NetworkRxTask", 10000, NULL, 1, &NetworkRxTask, 0);
}

void loop() {
  renderLCD();

  if (buttonClicked) {
    buttonClicked = false; 
    if (gState == STATE_END || gState == STATE_SRT) {
      sendUDP("SET\n");
    } 
    else if (gState == STATE_SET) {
      if (setMenuCursor == 1) {
        Serial.println("➔ [메뉴 조작] BACK 버튼 선택됨! 파이썬에 탈출 요청 전달.");
        sendUDP("CLK\n"); 
      } else {
        sendUDP("CLK\n");
      }
    }
  }

  static bool lastMenuUp = false;
  static bool lastMenuDown = false;
  static bool lastUpState = false;

  if (resetMenuFlags) {
    lastMenuUp = false;
    lastMenuDown = false;
    lastUpState = false;
    resetMenuFlags = false;
    Serial.println("🔄 [시스템] 메인 메뉴 이동 완료. 조이스틱 제어 플래그를 초기화했습니다.");
  }

  unsigned long now = millis();

  // [상태 A] 메인메뉴 상태
  if (gState == STATE_END) {
    int y = getFilteredY(); 
    bool isUp = (y < JOY_MID - JOY_THRESH);
    if (isUp && !lastUpState) {
      sendUDP("SRT\n");
    }
    lastUpState = isUp;
  } 
  
  // [상태 B] 설정창 상태
  else if (gState == STATE_SET) {
    int y = getFilteredY(); 
    bool isUp = (y < JOY_MID - JOY_THRESH);
    bool isDown = (y > JOY_MID + JOY_THRESH);

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
  
  // [상태 C] 인게임 상태
  else if (gState == STATE_SRT && joystickActive) {
    if (now - tLastSend >= SEND_INTERVAL_MS) {
      int x_filtered = getFilteredX(); 
      int y_filtered = getFilteredY();
      
      if (abs(x_filtered - lastSentX) > CHANGE_THRESH || abs(y_filtered - lastSentY) > CHANGE_THRESH) {
        // 파이썬 양식 맞춤 전송 "X:Y\n"
        String msg = String(x_filtered) + ":" + String(y_filtered) + "\n";
        sendUDP(msg);
        
        lastSentX = x_filtered;
        lastSentY = y_filtered;

        String logMsg = msg;
        logMsg.trim();
        
        Serial.print("🚀 [필터링 송신] -> :");
        Serial.println(logMsg);
      }
      tLastSend = now; 
    }
  }

  delay(2); 
}
