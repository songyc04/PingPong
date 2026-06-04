#include <WiFi.h>
#include <WiFiUdp.h>
#include <LiquidCrystal_I2C.h>

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= WIFI =================
const char* ssid = "fusion";
const char* password = "12345678";

// 고정 IP
IPAddress local_IP(192, 168, 0, 207);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

// ================= UDP =================
WiFiUDP udp;
const int localPort = 10002;

char incomingPacket[255];

// ================= LCD 함수 =================

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
  lcd.print("playing");

  
}

void showPaused()
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("STOP");
}

void showResult(int score, String winner)
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("SCORE:");
  lcd.print(score);

  lcd.setCursor(0, 1);
  lcd.print("WINNER:");
  lcd.print(winner);
}

// ================= SETUP =================

void setup()
{
  Serial.begin(115200);

  lcd.init();
  lcd.backlight();

  showReady();

  if (!WiFi.config(local_IP, gateway, subnet))
  {
    Serial.println("고정 IP 설정 실패");
  }

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

  Serial.print("ESP32 IP 주소 : ");
  Serial.println(WiFi.localIP());

  udp.begin(localPort);

  Serial.print("UDP 수신 대기 포트 : ");
  Serial.println(localPort);
}

// ================= LOOP =================

void loop()
{
  int packetSize = udp.parsePacket();

  if (packetSize)
  {
    int len = udp.read(incomingPacket, 254);

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
      //showCountDown();
      showRunning();
    }
    else if (response == "STP")
    {
      showPaused();
    }
    else if (response.startsWith("PLAYER"))
    {
      int commaIndex = response.indexOf(',');

      String winner = response.substring(0, commaIndex);
      String scoreStr = response.substring(commaIndex + 1);

      int score = scoreStr.toInt();

      showResult(score, winner);
    }
  }
}