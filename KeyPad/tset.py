import pygame
import sys
import socket
import threading
import math

# --- 색상값 정의 ---
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
RED = (255, 50, 50)
BLUE = (50, 50, 255)
GRAY = (100, 100, 100)
GREEN = (50, 200, 50)
YELLOW = (255, 215, 0)
ORANGE = (255, 165, 0)
PURPLE = (128, 0, 128)

COLOR_OPTIONS = [RED, BLUE, GREEN, YELLOW, ORANGE, PURPLE, WHITE]
COLOR_NAMES = ["RED", "BLUE", "GREEN", "YELLOW", "ORANGE", "PURPLE", "WHITE"]

# --- 전역 게임 상태 및 네트워크 데이터 정의 ---
UI_state = "MAIN_MENU"
network_command = ""
command_lock = threading.Lock()

# 조이스틱 입력값을 저장할 전역 변수 (X, Y 비율: -1.0 ~ 1.0)
p1_joy_x, p1_joy_y = 0.0, 0.0
p2_joy_x, p2_joy_y = 0.0, 0.0

# --- 설정 데이터 변수 ---
sound_enabled = True
p1_color_idx = 0  
p2_color_idx = 1  
game_time_limit = 30000
time_limit_idx = 1
TIME_LIMITS = [10000, 30000, 60000, 120000]

# --- 설정 화면 커서 및 팝업 상태 정의 ---
current_setting_index = 0  
popup_type = ""            
popup_sub_index = 0        

# --- 카운트다운 및 입력 제어 플래그 전역 배치 ---
countdown_active = False

# --- [네트워크 분리 설정] ---
ESP32_IP = "192.168.0.207"
ESP32_SEND_PORT = 10002     # 파이썬이 '송신'할 목적지 포트
PYTHON_RCV_PORT = 10001     # 파이썬이 '수신'할 본인 포트

# 송신 전용 UDP 소켓 독립 생성
tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
last_joystick_ip = None

def parse_joystick(raw_msg):
   """
   [UDP 매핑 알고리즘] 
   ESP32의 하드웨어 변경(P1=3.3V, P2=5V)에 따른 아날로그 전압 스케일 차이를 완벽히 보정합니다.
   간섭과 노이즈를 제어하고 두 패들이 완전히 독립적으로 움직이도록 빌드되었습니다.
   """
   global p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y, countdown_active, UI_state
   
   if countdown_active:
      return
   
   try:
      data = raw_msg.split(',')
      
      # 1. 예외 케이스 처리 (만약 데이터가 2개만 들어올 경우 기존 매핑 유지)
      if len(data) == 2:
         p1_x = int(data[0])
         p1_y = int(data[1])
         
         if 1900 <= p1_x <= 2200:   p1_jx = 0.0
         elif p1_x < 1900:          p1_jx = (p1_x - 1900) / 1900.0
         else:                      p1_jx = (p1_x - 2200) / (4095.0 - 2200.0)
            
         if 1900 <= p1_y <= 2200:   p1_jy = 0.0
         elif p1_y < 1900:          p1_jy = (p1_y - 1900) / 1900.0
         else:                      p1_jy = (p1_y - 2200) / (4095.0 - 2200.0)
         
         p1_joy_x, p1_joy_y = p1_jx, p1_jy
         
      # 2. 메인 정상 케이스 처리 (P1, P2 데이터가 4개 모두 전송되었을 때)
      elif len(data) >= 4:
         p1_x = int(data[0])
         p1_y = int(data[1])
         p2_x = int(data[2])
         p2_y = int(data[3])
         
         # -----------------------------------------------------------------
         # [보정 변환] 1P 조이스틱 정규화 (ESP32 전원: 3.3V 전용 기준)
         # 중앙값 오차가 적고 0 ~ 4095 풀 스케일이 깔끔하게 나옵니다.
         # -----------------------------------------------------------------
         p1_center_min, p1_center_max = 1850, 2250 # 3.3V에 최적화된 하드웨어 중립 존
         
         if p1_center_min <= p1_x <= p1_center_max: p1_jx = 0.0
         elif p1_x < p1_center_min:                 p1_jx = (p1_x - p1_center_min) / float(p1_center_min)
         else:                                      p1_jx = (p1_x - p1_center_max) / float(4095.0 - p1_center_max)
            
         if p1_center_min <= p1_y <= p1_center_max: p1_jy = 0.0
         elif p1_y < p1_center_min:                 p1_jy = (p1_y - p1_center_min) / float(p1_center_min)
         else:                                      p1_jy = (p1_y - p1_center_max) / float(4095.0 - p1_center_max)

         # -----------------------------------------------------------------
         # [보정 변환] 2P 조이스틱 정규화 (ESP32 전원: 5V 사용에 따른 ADC 변환 보정)
         # 5V 가변 전압 특성상 중앙값이 위로 치우치거나 상단 포화가 빨리 올 수 있습니다.
         # 데드존 반경을 약간 더 넓혀 불필요한 떨림과 간섭을 철저하게 잡아냅니다.
         # -----------------------------------------------------------------
         p2_center_min, p2_center_max = 1750, 2350 # 5V 인가 시 전압 레벨 변동에 따른 최적화 데드존
         
         if p2_center_min <= p2_x <= p2_center_max: p2_jx = 0.0
         elif p2_x < p2_center_min:                 p2_jx = (p2_x - p2_center_min) / float(p2_center_min)
         else:                                      p2_jx = (p2_x - p2_center_max) / float(4095.0 - p2_center_max)
            
         if p2_center_min <= p2_y <= p2_center_max: p2_jy = 0.0
         elif p2_y < p2_center_min:                 p2_jy = (p2_y - p2_center_min) / float(p2_center_min)
         else:                                      p2_jy = (p2_y - p2_center_max) / float(4095.0 - p2_center_max)

         # 디버그 검증용 로그 출력 추가
         print(f"[전압분리 독립 검증] 1P: ({p1_jx:+.2f}, {p1_jy:+.2f}) | 2P: ({p2_jx:+.2f}, {p2_jy:+.2f})")

         # 최종 게임 오브젝트 연동 전역 변수 업데이트
         p1_joy_x, p1_joy_y = p1_jx, p1_jy
         p2_joy_x, p2_joy_y = p2_jx, p2_jy
            
   except Exception as e:
      print(f"[파서 에러] 데이터 변환 에러 발생: {e}")

def send_to_esp32(message):
   """
   10002 포트(LCD/조이스틱 제어 포트)로 데이터 송신
   """
   try:
      tx_socket.sendto(message.encode('utf-8'), (ESP32_IP, ESP32_SEND_PORT))
   except Exception as e:
      print(f"[네트워크] ESP32 송신 오류: {e}")

def udp_server_thread():
   """
   UDP 수신 전용 스레드 (10001번 포트 수신)
   """
   global network_command, last_joystick_ip
   SERVER_IP = "0.0.0.0"
   
   rx_socket = socket.socket
