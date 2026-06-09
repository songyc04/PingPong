/*
 * ╔══════════════════════════════════════════════════════╗
 * ESP32 핀볼 [P1 보드] - 설정 탈출 타이밍 및 SRT 락 버그 수정본
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

#define JOY_MID       2048   
#define JOY_THRESH     800   
#define SEND_INTERVAL_MS 30   

enum GameState {
  STATE_END,  // 메인 메뉴 상태
  STATE_SET,  // 설정창 상태
  STATE_SRT   // 인게임 상태
};
volatile GameState gState = STATE_END; 
volatile bool joystickActive = false; 
volatile int setMenuCursor = 0; 

// 조이스틱 플래그 초기화를 위한 전역 static 변수 대체용 volatile 플래그
volatile bool resetMenuFlags = false;

// Core 1 LCD 전담 출력을 위한 버퍼 및 플래그
volatile bool lcdUpdateReq = false;
char lcdBuf0[32] = "MAIN MENU";
char lcdBuf1[32] = "READY...";

volatile bool          buttonClicked = false;
volatile unsigned long lastInterruptTime = 0;
#define DEBOUNCE_TIME_MS 250 

unsigned long tLastSend = 0;
TaskHandle_t NetworkRxTask;

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
  
  if (msg.indexOf(':') == -1) {
    String logMsg = msg;
    logMsg.trim();
    Serial.print("[UDP 상태 송신] -> "); 
    Serial.println(logMsg);
  }
}

void sendJoystickNow() {
  int x = analogRead(VRX);
  int y = analogRead(VRY);
  String msg = "X:" + String(x) + ",Y:" + String(y) + "\n";
  sendUDP(msg);
}

void IRAM_ATTR clickButton() {
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > DEBOUNCE_TIME_MS) {
    buttonClicked = true;
    lastInterruptTime = interruptTime;
  }
}

// ════════════════════════════════════════════
//  ★ 무한루프 스레드: Core 0 구동 (UDP 수신전용)
// ════════════════════════════════════════════
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

      // 1. 파이썬이 설정 탈출을 승인하거나 게임이 완전히 종료되었을 때만 END 전환
      if (response == "END") {
        gState = STATE_END;
        joystickActive = false; 
        resetMenuFlags = true; // 메뉴 이동 플래그 강제 리셋 요청
        requestLCD("MAIN MENU", "READY...");
      } 
      // 2. 설정창 진입 명령
      else if (response == "SET") {
        gState = STATE_SET;
        joystickActive = false; 
        setMenuCursor = 0; 
        requestLCD("> 1. OPTION SET", "  2. BACK");
      } 
      // 3. 인게임 시작 명령 (설정창 락이 걸려있지 않은 메인 메뉴 상태에서만 허용)
      else if (response == "SRT") {
        if (gState == STATE_END) { 
          gState = STATE_SRT;
          joystickActive = true;  
          requestLCD("PLAYING GAME!!", "P1 vs P2 MATCH");
          
          sendJoystickNow(); delay(5);
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

// ════════════════════════════════════════════
//  MAIN LOOP: Core 1 구동
// ════════════════════════════════════════════
void loop() {
  renderLCD();

  // 1. 인터럽트 버튼 이벤트 처리
  if (buttonClicked) {
    buttonClicked = false; 
    
    if (gState == STATE_END || gState == STATE_SRT) {
      sendUDP("SET\n");
    } 
    else if (gState == STATE_SET) {
      if (setMenuCursor == 1) {
        Serial.println("➔ [메뉴 조작] BACK 버튼 선택됨! 파이썬에 탈출 요청 전달.");
        // ★ 패킷이 충돌하지 않도록 깔끔하게 'BACK'만 전송하고 파이썬의 'END' 응답을 기다립니다.
        sendUDP("CLK\n"); 
      } else {
        sendUDP("CLK\n");
      }
    }
  }

  int x = analogRead(VRX);
  int y = analogRead(VRY);
  unsigned long now = millis();

  // 메뉴 이동 내부 플래그 제어 정적 변수
  static bool lastMenuUp = false;
  static bool lastMenuDown = false;
  static bool lastUpState = false;

  // 파이썬으로부터 END 패킷을 받고 메인으로 돌아왔을 때 무조건 조이스틱 트리거 변수 초기화
  if (resetMenuFlags) {
    lastMenuUp = false;
    lastMenuDown = false;
    lastUpState = false;
    resetMenuFlags = false;
    Serial.println("🔄 [시스템] 메인 메뉴 이동 완료. 조이스틱 제어 플래그를 초기화했습니다.");
  }

  // [상태 A] 메인메뉴 상태 -> 오직 상단(UP) 제어로만 SRT(게임시작) 유도
  if (gState == STATE_END) {
    bool isUp = (y < JOY_MID - JOY_THRESH);
    if (isUp && !lastUpState) {
      sendUDP("SRT\n");
    }
    lastUpState = isUp;
  } 
  
  // [상태 B] 설정창 상태 -> 오직 전용 순수 메뉴 제어명령(UP / DN)만 송신 가능
  else if (gState == STATE_SET) {
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
      String msg = "X:" + String(x) + ",Y:" + String(y) + "\n";
      sendUDP(msg);
      tLastSend = now;
    }
  }

  delay(2); 
}
