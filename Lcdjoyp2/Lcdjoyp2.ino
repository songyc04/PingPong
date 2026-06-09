#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= WIFI =================
const char* ssid = "fusion";
const char* password = "12345678";

IPAddress local_IP(192, 168, 0, 207);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

// ================= UDP =================

// Python PC IP
IPAddress serverIP(192, 168, 0, 138);

// P2 조이스틱 송신 포트
const int SEND_PORT = 10002;

// P2 LCD 수신 포트
const int RECEIVE_PORT = 10004;

WiFiUDP udp;
char incomingPacket[255];

// ================= JOYSTICK =================
const int VRX = 32;
const int VRY = 35;
const int SW  = 25;

// ================= INTERRUPT =================
volatile bool endGame = false;

void IRAM_ATTR stopGame()
{
  endGame = true;
}

// ================= LCD FUNCTIONS =================

void showReady()
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("PING PONG");

  lcd.setCursor(0, 1);
  lcd.print("READY");
}

void showRunning()
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("PLAYING");
}

void showPaused()
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("STOP");
}

void showWin(int score)
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("YOU WIN");

  lcd.setCursor(0, 1);
  lcd.print("SCORE:");
  lcd.print(score);
}

void showLose(int score)
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("YOU LOSE");

  lcd.setCursor(0, 1);
  lcd.print("SCORE:");
  lcd.print(score);
}

void showDraw(int score)
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("DRAW");

  lcd.setCursor(0, 1);
  lcd.print("SCORE:");
  lcd.print(score);
}

// ================= SETUP =================

void setup()
{
  Serial.begin(115200);

  // I2C LCD
  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();

  showReady();

  // 조이스틱 버튼
  pinMode(SW, INPUT_PULLUP);

  // 인터럽트 등록
  attachInterrupt(
    digitalPinToInterrupt(SW),
    stopGame,
    FALLING
  );

  // 고정 IP
  WiFi.config(local_IP, gateway, subnet);

  Serial.print("WiFi 연결 중 : ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi 연결 완료");

  Serial.print("ESP32 IP : ");
  Serial.println(WiFi.localIP());

  // LCD 명령 수신 포트
  udp.begin(RECEIVE_PORT);

  Serial.print("UDP 수신 포트 : ");
  Serial.println(RECEIVE_PORT);
}

// ================= LOOP =================

void loop()
{
  // ===== 인터럽트 종료 버튼 =====

  if (endGame)
  {
    udp.beginPacket(serverIP, SEND_PORT);
    udp.print("END");
    udp.endPacket();

    Serial.println("[송신] END");

    endGame = false;
  }

  // ===== 조이스틱 값 전송 =====

  static unsigned long lastSend = 0;

  if (millis() - lastSend >= 50)
  {
    int x = analogRead(VRX);
    int y = analogRead(VRY);

    String msg =
      String(x) +
      ":" +
      String(y);

    udp.beginPacket(serverIP, SEND_PORT);
    udp.print(msg);
    udp.endPacket();

    lastSend = millis();
  }

  // ===== LCD 명령 수신 =====

  int packetSize = udp.parsePacket();

  if (packetSize)
  {
    int len = udp.read(incomingPacket, sizeof(incomingPacket) - 1);

    if (len > 0)
    {
      incomingPacket[len] = '\0';
    }

    String response = String(incomingPacket);
    response.trim();

    Serial.print("[수신] ");
    Serial.println(response);

    if (response == "SRT")
    {
      showRunning();
    }
    else if (response == "STP")
    {
      showPaused();
    }
    else if (response.startsWith("WIN"))
    {
      int commaIndex = response.indexOf(',');
      int score = response.substring(commaIndex + 1).toInt();

      showWin(score);
    }
    else if (response.startsWith("LOSE"))
    {
      int commaIndex = response.indexOf(',');
      int score = response.substring(commaIndex + 1).toInt();

      showLose(score);
    }
    else if (response.startsWith("DRAW"))
    {
      int commaIndex = response.indexOf(',');
      int score = response.substring(commaIndex + 1).toInt();

      showDraw(score);
    }
  }
}