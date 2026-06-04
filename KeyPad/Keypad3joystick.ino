#include <WiFi.h>
#include <Keypad.h>
#include <Ticker.h> 

const char* ssid     = "fusion";     
const char* password = "12345678"; 
const char* serverIP = "192.168.0.122"; 
const uint16_t port  = 10001;          

WiFiClient client;

const byte ROWS = 4; 
const byte COLS = 4; 

char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'}, 
  {'4','5','6','B'}, 
  {'7','8','9','C'}, 
  {'*','0','#','D'}  
};

byte rowPins[ROWS] = {18, 5, 17, 16}; 
byte colPins[COLS] = {23, 22, 21, 19}; 

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

Ticker keypadTimer;
volatile char pressedKey = 0; 

unsigned long lastConnectAttempt = 0;
const unsigned long connectInterval = 5000; 

void IRAM_ATTR onKeypadScan() {
  char key = customKeypad.getKey();
  if (key) {
    pressedKey = key; 
  }
}

void setup() {
  Serial.begin(115200);
  delay(10);

  Serial.println();
  Serial.print("Wi-Fi 연결 중: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi 연결 완료!");
  Serial.print("ESP32 IP 주소: ");
  Serial.println(WiFi.localIP());

  keypadTimer.attach(0.03, onKeypadScan);
  Serial.println("타이머 인터럽트 기반 키패드 스캔 가동 시작.");
}

void loop() {
  unsigned long currentMillis = millis();

  if (!client.connected()) {
    if (currentMillis - lastConnectAttempt >= connectInterval) {
      lastConnectAttempt = currentMillis; 
      
      Serial.println("서버 연결 시도 중...");
      if (client.connect(serverIP, port)) {
        Serial.println("서버 연결 성공!");
      } else {
        Serial.println("연결 실패. (인터럽트가 백그라운드에서 키패드를 계속 감시하는 중)");
      }
    }
  }

  if (pressedKey != 0) {
    char key = pressedKey;
    pressedKey = 0; 

    Serial.print("[인터럽트 감지] 키 입력: ");
    Serial.println(key);
    
    String command = "";
    switch (key) {
      case 'A': command = "SRT"; break;
      case 'B': command = "STP"; break;
      case 'C': command = "SET"; break;
      case 'D': command = "CUS"; break;
      case '#': command = "END"; break; 
    }

    if (command != "" && client.connected()) {
      Serial.print("[송신] ESP32 -> PC: ");
      Serial.println(command);
      client.print(command + "\n"); 
    } else if (command != "" && !client.connected()) {
      Serial.println("[경고] 서버 미연결로 인터럽트 명령어 전송 불가");
    }
  }

  if (client.connected() && client.available() > 0) {
    String response = client.readStringUntil('\n');
    response.trim();
    Serial.print("[수신] PC -> ESP32: ");
    Serial.println(response);
  }
}