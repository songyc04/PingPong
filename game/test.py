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
   [UDP 매핑 알고리즘 - 데이터 상호 간섭 차단 완벽 보정 버전] 
   들여쓰기 구조를 전면 개편하여 1P와 2P 데이터를 물리적/논리적으로 완벽히 격리합니다.
   """
   global p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y, countdown_active, UI_state
   
   if countdown_active:
      return
   
   try:
      data = raw_msg.split(',')
      
      # 1. 예외 케이스 처리 (만약 데이터가 2개만 들어올 경우 1P만 업데이트하고 종료)
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
         
      # 2. 메인 정상 케이스 처리 (P1, P2 데이터가 4개 모두 전송되었을 때 블록 안에서만 연산)
      elif len(data) >= 4:
         p1_x = int(data[0])
         p1_y = int(data[1])
         p2_x = int(data[2])
         p2_y = int(data[3])
         
         # 초기화 후 연산 진행하여 잔상 데이터 간섭 방지
         p1_jx, p1_jy = 0.0, 0.0
         p2_jx, p2_jy = 0.0, 0.0

         # --- [1P 조이스틱 정규화] ---
         p1_center_min, p1_center_max = 1850, 2250 
         
         if p1_center_min <= p1_x <= p1_center_max: p1_jx = 0.0
         elif p1_x < p1_center_min:                 p1_jx = (p1_x - p1_center_min) / float(p1_center_min)
         else:                                      p1_jx = (p1_x - p1_center_max) / float(4095.0 - p1_center_max)
            
         if p1_center_min <= p1_y <= p1_center_max: p1_jy = 0.0
         elif p1_y < p1_center_min:                 p1_jy = (p1_y - p1_center_min) / float(p1_center_min)
         else:                                      p1_jy = (p1_y - p1_center_max) / float(4095.0 - p1_center_max)

         # --- [2P 조이스틱 정규화 - 들여쓰기 완벽 격리] ---
         p2_center_min, p2_center_max = 1750, 2350 
         
         if p2_center_min <= p2_x <= p2_center_max: p2_jx = 0.0
         elif p2_x < p2_center_min:                 p2_jx = (p2_x - p2_center_min) / float(p2_center_min)
         else:                                      p2_jx = (p2_x - p2_center_max) / float(4095.0 - p2_center_max)
            
         if p2_center_min <= p2_y <= p2_center_max: p2_jy = 0.0
         elif p2_y < p2_center_min:                 p2_jy = (p2_y - p2_center_min) / float(p2_center_min)
         else:                                      p2_jy = (p2_y - p2_center_max) / float(4095.0 - p2_center_max)

         # 연산 완료된 격리 데이터를 전역변수에 안전하게 동기화
         p1_joy_x, p1_joy_y = p1_jx, p1_jy
         p2_joy_x, p2_joy_y = p2_jx, p2_jy
            
   except Exception as e:
      print(f"[파서 에러] 데이터 변환 에러 발생: {e}")

def send_to_esp32(message):
   try:
      tx_socket.sendto(message.encode('utf-8'), (ESP32_IP, ESP32_SEND_PORT))
   except Exception as e:
      print(f"[네트워크] ESP32 송신 오류: {e}")

def udp_server_thread():
   global network_command, last_joystick_ip
   SERVER_IP = "0.0.0.0"
   rx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
   
   try:
      rx_socket.bind((SERVER_IP, PYTHON_RCV_PORT))
      print(f"[네트워크] 파이썬 UDP 수신 포트({PYTHON_RCV_PORT})가 정상 개방되었습니다.")
   except Exception as e:
      print(f"[네트워크] UDP 수신 포트 바인딩 실패: {e}")
      return

   while True:
      try:
         data, addr = rx_socket.recvfrom(1024)
         if not data:
            continue
            
         raw_msg = data.decode("utf-8").strip()
         last_joystick_ip = addr[0]
         
         if ',' in raw_msg:
            with command_lock:
               parse_joystick(raw_msg)
         else:
            print(f"Command 수신: {raw_msg}")
            with command_lock:
               network_command = raw_msg
               
      except Exception as e:
         print(f"[네트워크] UDP 수신 오류: {e}")
         break

def run_game():
   global UI_state, network_command, current_setting_index, popup_type, popup_sub_index
   global sound_enabled, p1_color_idx, p2_color_idx
   global p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y
   global game_time_limit, time_limit_idx, countdown_active
   
   pygame.init()
   
   info = pygame.display.Info()
   WIDTH, HEIGHT = info.current_w, info.current_h
   
   screen = pygame.display.set_mode((WIDTH, HEIGHT), pygame.NOFRAME | pygame.HWSURFACE | pygame.DOUBLEBUF)
   pygame.display.set_caption("ESP32 하키 핑퐁 - 초고속 UDP 버전")
   clock = pygame.time.Clock()

   ball_size = int(HEIGHT * 0.03)
   ball_radius = ball_size // 2
   
   p1_radius = int(ball_radius * 2.5)
   p2_radius = int(ball_radius * 2.5)
   
   goal_width = int(WIDTH * 0.008)
   goal_height = int(HEIGHT * 0.3)
   
   paddle_speed = int(HEIGHT * 0.02)
   base_ball_speed_x = int(WIDTH * 0.008)
   base_ball_speed_y = int(HEIGHT * 0.008)
   
   p1_cx = int(WIDTH * 0.06)
   p1_cy = HEIGHT // 2
   p2_cx = WIDTH - int(WIDTH * 0.06)
   p2_cy = HEIGHT // 2
   center_line_x = WIDTH // 2
   
   p1_goal = pygame.Rect(0, HEIGHT // 2 - goal_height // 2, goal_width, goal_height)
   p2_goal = pygame.Rect(WIDTH - goal_width, HEIGHT // 2 - goal_height // 2, goal_width, goal_height)
   
   ball_x, ball_y = WIDTH // 2 - ball_size // 2, HEIGHT // 2 - ball_size // 2
   ball_speed_x, ball_speed_y = base_ball_speed_x, base_ball_speed_y
   ball_active = False  
   
   p1_score = 0
   p2_score = 0
   
   try:
      title_font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.08))
      font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.05))
      btn_font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.035))
      countdown_font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.18))
   except:
      title_font = pygame.font.SysFont("malgungothic", int(HEIGHT * 0.08), bold=True)
      font = pygame.font.SysFont("malgungothic", int(HEIGHT * 0.05))
      btn_font = pygame.font.SysFont("malgungothic", int(HEIGHT * 0.035))
      countdown_font = pygame.font.SysFont("malgungothic", int(HEIGHT * 0.18), bold=True)

   btn_width, btn_height = int(WIDTH * 0.22), int(HEIGHT * 0.08)
   btn_start_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.45), btn_width, btn_height)
   btn_setting_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.58), btn_width, btn_height)
   btn_exit_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.71), btn_width, btn_height)

   setting_box_width, setting_box_height = int(WIDTH * 0.6), int(HEIGHT * 0.5)
   setting_box_rect = pygame.Rect(WIDTH // 2 - setting_box_width // 2, int(HEIGHT * 0.3), setting_box_width, setting_box_height)

   btn_back_width, btn_back_height = int(WIDTH * 0.15), int(HEIGHT * 0.06)
   btn_back_rect = pygame.Rect(WIDTH // 2 - btn_back_width // 2, int(HEIGHT * 0.72), btn_back_width, btn_back_height)

   setting_y_positions = [int(HEIGHT * 0.35), int(HEIGHT * 0.43), int(HEIGHT * 0.51), int(HEIGHT * 0.59), int(HEIGHT * 0.72)]

   game_elapsed_time = 0.0
   game_timer_active = False
   countdown_timer = 0

   running = True
   while running:
      dt = clock.tick(60) 
      mouse_pos = pygame.mouse.get_pos()
      
      for event in pygame.event.get():
         if event.type == pygame.QUIT:
            running = False
         elif event.type == pygame.KEYDOWN:
            if event.key == pygame.K_ESCAPE:
               if popup_type: popup_type = ""
               elif UI_state in ["GAME_PLAY", "PAUSE", "SETTINGS"]: UI_state = "MAIN_MENU"
               else: running = False
         elif event.type == pygame.MOUSEBUTTONDOWN:
            if event.button == 1:
               if popup_type: 
                  popup_type = ""
                  continue
               if UI_state == "MAIN_MENU":
                  if btn_start_rect.collidepoint(mouse_pos):
                     p1_score, p2_score = 0, 0
                     ball_x = WIDTH // 2 - ball_size // 2
                     ball_y = HEIGHT // 2 - ball_size // 2
                     ball_active = False  
                     p1_cx, p1_cy = int(WIDTH * 0.06), HEIGHT // 2
                     p2_cx, p2_cy = WIDTH - int(WIDTH * 0.06), HEIGHT // 2
                     p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y = 0.0, 0.0, 0.0, 0.0
                     UI_state = "GAME_PLAY"
                     game_elapsed_time = 0.0
                     game_timer_active = False
                     countdown_timer = 3000
                     countdown_active = True
                  elif btn_setting_rect.collidepoint(mouse_pos):
                     UI_state = "SETTINGS"
                     current_setting_index = 0
                  elif btn_exit_rect.collidepoint(mouse_pos):
                     running = False
               elif UI_state == "SETTINGS":
                  if btn_back_rect.collidepoint(mouse_pos):
                     UI_state = "MAIN_MENU"

      if not running: break

      current_cmd = ""
      with command_lock:
         if network_command:
            current_cmd = network_command
            network_command = ""

      if UI_state == "GAME_PLAY" and countdown_active:
         countdown_timer -= dt
         if countdown_timer <= 0:
            countdown_active = False
            countdown_timer = 0
            print("[시스템] 카운트다운 완료 -> SRT 송신.")
            send_to_esp32("SRT")  
            game_timer_active = True

      if UI_state == "GAME_PLAY" and game_timer_active:
         game_elapsed_time += dt
         if game_elapsed_time >= game_time_limit:
            current_cmd = "END"

      if current_cmd:
         if current_cmd == "SRT":
            if UI_state in ["MAIN_MENU", "PAUSE"]:
               p1_score, p2_score = 0, 0
               ball_x = WIDTH // 2 - ball_size // 2
               ball_y = HEIGHT // 2 - ball_size // 2
               ball_active = False  
               p1_cx, p1_cy = int(WIDTH * 0.06), HEIGHT // 2
               p2_cx, p2_cy = WIDTH - int(WIDTH * 0.06), HEIGHT // 2
               
               p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y = 0.0, 0.0, 0.0, 0.0
               UI_state = "GAME_PLAY"
               game_elapsed_time = 0.0
               game_timer_active = False
               countdown_timer = 3000
               countdown_active = True
         
         elif current_cmd == "STP":
            if UI_state == "GAME_PLAY":
               UI_state = "PAUSE"
               game_timer_active = False
               countdown_active = False
         elif current_cmd == "END":
            if p1_score > p2_score:
               result_msg = f"PLAYER1,{p1_score}"
            elif p2_score > p1_score:
               result_msg = f"PLAYER2,{p2_score}"
            else:
               result_msg = f"DRAW,{p1_score}"
            
            send_to_esp32(result_msg) 
            
            UI_state = "MAIN_MENU"
            popup_type = ""
            game_elapsed_time = 0.0
            game_timer_active = False
            countdown_active = False
         elif current_cmd == "SET":
            UI_state = "SETTINGS"
            current_setting_index = 0
            popup_type = ""
         elif UI_state == "SETTINGS":
            # --- 상위 if 블록들과 완벽히 라인을 일치시킵니다 ---
            if popup_type:
               if popup_type == "SOUND":
                  if current_cmd in ["UP", "DN"]: popup_sub_index = (popup_sub_index + 1) % 2
                  elif current_cmd == "CLK":
                     sound_enabled = (popup_sub_index == 0)
                     popup_type = ""
               elif popup_type == "COLOR":
                  if current_cmd == "UP": popup_sub_index = (popup_sub_index - 1) % 4
                  elif current_cmd == "DN": popup_sub_index = (popup_sub_index + 1) % 4
                  elif current_cmd == "CLK":
                     if popup_sub_index == 0: p1_color_idx = (p1_color_idx + 1) % len(COLOR_OPTIONS)
                     elif popup_sub_index == 1: p2_color_idx = (p2_color_idx + 1) % len(COLOR_OPTIONS)
                     elif popup_sub_index in [2, 3]: popup_type = ""
               elif popup_type == "GAME_CUSTOM":
                  if current_cmd in ["UP", "DN"]: popup_sub_index = (popup_sub_index + 1) % 2
                  elif current_cmd == "CLK":
                     if popup_sub_index == 0:
                        popup_type = "SETTING_TIMES"
                        popup_sub_index = time_limit_idx
                     else:
                        popup_type = ""
               elif popup_type == "SETTING_TIMES":
                  if current_cmd == "UP": popup_sub_index = (popup_sub_index - 1) % 5
                  elif current_cmd == "DN": popup_sub_index = (popup_sub_index + 1) % 5
                  elif current_cmd == "CLK":
                     if popup_sub_index < 4:
                        time_limit_idx = popup_sub_index
                        game_time_limit = TIME_LIMITS[time_limit_idx]
                     else:
                        popup_type = "GAME_CUSTOM"
                        popup_sub_index = 0
               elif popup_type == "CREATOR":
                  if current_cmd == "CLK": popup_type = ""
            else:
               if current_cmd == "UP": current_setting_index = (current_setting_index - 1) % 5
               elif current_cmd == "DN": current_setting_index = (current_setting_index + 1) % 5
               elif current_cmd == "CLK":
                  if current_setting_index == 0:
                     popup_type = "SOUND"
                     popup_sub_index = 0 if sound_enabled else 1
                  elif current_setting_index == 1:
                     popup_type = "COLOR"
                     popup_sub_index = 0
                  elif current_setting_index == 2:
                     popup_type = "GAME_CUSTOM"
                     popup_sub_index = 0
                  elif current_setting_index == 3:
                     popup_type = "CREATOR"
                     popup_sub_index = 0
                  elif current_setting_index == 4:
                     UI_state = "MAIN_MENU"

      # --- 패들 실시간 업데이트 ---
      if UI_state == "GAME_PLAY":
         keys = pygame.key.get_pressed()
         
         # P1 처리
         move_p1_x = p1_joy_x * paddle_speed
         move_p1_y = p1_joy_y * paddle_speed
         if keys[pygame.K_w]: move_p1_y = -paddle_speed
         if keys[pygame.K_s]: move_p1_y = paddle_speed
         if keys[pygame.K_a]: move_p1_x = -paddle_speed
         if keys[pygame.K_d]: move_p1_x = paddle_speed
         
         p1_cx += int(move_p1_x)
         p1_cy += int(move_p1_y)
         
         if p1_cy < p1_radius: p1_cy = p1_radius
         if p1_cy > HEIGHT - p1_radius: p1_cy = HEIGHT - p1_radius
         if p1_cx < p1_radius: p1_cx = p1_radius
         if p1_cx > center_line_x - p1_radius: p1_cx = center_line_x - p1_radius
            
         # P2 처리
         move_p2_x = p2_joy_x * paddle_speed
         move_p2_y = p2_joy_y * paddle_speed
         if keys[pygame.K_UP]:    move_p2_y = -paddle_speed
         if keys[pygame.K_DOWN]:  move_p2_y = paddle_speed
         if keys[pygame.K_LEFT]:  move_p2_x = -paddle_speed
         if keys[pygame.K_RIGHT]: move_p2_x = paddle_speed
         
         p2_cx += int(move_p2_x)
         p2_cy += int(move_p2_y)
         
         if p2_cy < p2_radius: p2_cy = p2_radius
         if p2_cy > HEIGHT - p2_radius: p2_cy = HEIGHT - p2_radius
         if p2_cx < center_line_x + p2_radius: p2_cx = center_line_x + p2_radius
         if p2_cx > WIDTH - p2_radius: p2_cx = WIDTH - p2_radius

         # 공 역학 (카운트다운 완료 후에만 동작)
         if ball_active and not countdown_active:
            ball_x += ball_speed_x
            ball_y += ball_speed_y

         if ball_y <= 0:
            ball_speed_y = abs(ball_speed_y)
            ball_y = 0
         elif ball_y >= HEIGHT - ball_size:
            ball_speed_y = -abs(ball_speed_y)
            ball_y = HEIGHT - ball_size

         ball_cx, ball_cy = ball_x + ball_radius, ball_y + ball_radius

         # P1 충돌 판정
         dx1, dy1 = ball_cx - p1_cx, ball_cy - p1_cy
         distance1 = math.hypot(dx1, dy1)
         min_dist1 = p1_radius + ball_radius

         if distance1 < min_dist1:
            if not ball_active and not countdown_active:  
               ball_active = True
               ball_speed_x = abs(base_ball_speed_x)
               ball_speed_y = base_ball_speed_y if dy1 >= 0 else -base_ball_speed_y
               ball_x = p1_cx + p1_radius + 2
            elif not countdown_active:
               if distance1 == 0: distance1 = 0.1
               nx, ny = dx1 / distance1, dy1 / distance1
               dot_product = ball_speed_x * nx + ball_speed_y * ny
               ball_speed_x = ball_speed_x - 2 * dot_product * nx
               ball_speed_y = ball_speed_y - 2 * dot_product * ny
               ball_cx = p1_cx + nx * min_dist1
               ball_cy = p1_cy + ny * min_dist1
               ball_x, ball_y = ball_cx - ball_radius, ball_cy - ball_radius

         # P2 충돌 판정
         dx2, dy2 = ball_cx - p2_cx, ball_cy - p2_cy
         distance2 = math.hypot(dx2, dy2)
         min_dist2 = p2_radius + ball_radius

         if distance2 < min_dist2:
            if not ball_active and not countdown_active:  
               ball_active = True
               ball_speed_x = -abs(base_ball_speed_x)
               ball_speed_y = base_ball_speed_y if dy2 >= 0 else -base_ball_speed_y
               ball_x = p2_cx - p2_radius - ball_size - 2
            elif not countdown_active:
               if distance2 == 0: distance2 = 0.1
               nx, ny = dx2 / distance2, dy2 / distance2
               dot_product = ball_speed_x * nx + ball_speed_y * ny
               ball_speed_x = ball_speed_x - 2 * dot_product * nx
               ball_speed_y = ball_speed_y - 2 * dot_product * ny
               ball_cx = p2_cx + nx * min_dist2
               ball_cy = p2_cy + ny * min_dist2
               ball_x, ball_y = ball_cx - ball_radius, ball_cy - ball_radius

         ball_rect = pygame.Rect(ball_x, ball_y, ball_size, ball_size)

         if ball_rect.colliderect(p1_goal) and not countdown_active:
            p2_score += 1
            ball_x = int(WIDTH * 0.25) - ball_size // 2
            ball_y = HEIGHT // 2 - ball_size // 2
            ball_speed_x, ball_speed_y = base_ball_speed_x, base_ball_speed_y
            ball_active = False
         elif ball_rect.colliderect(p2_goal) and not countdown_active:
            p1_score += 1
            ball_x = int(WIDTH * 0.75) - ball_size // 2
            ball_y = HEIGHT // 2 - ball_size // 2
            ball_speed_x, ball_speed_y = -base_ball_speed_x, base_ball_speed_y
            ball_active = False
         else:
            if ball_x <= 0:
               ball_speed_x = abs(ball_speed_x)
               ball_x = 0
            elif ball_x >= WIDTH - ball_size:
               ball_speed_x = -abs(ball_speed_x)
               ball_x = WIDTH - ball_size
      else:
         ball_rect = pygame.Rect(ball_x, ball_y, ball_size, ball_size)

      # --- 그래픽 렌더링 ---
      screen.fill(BLACK)
      pygame.draw.line(screen, GRAY, (center_line_x, 0), (center_line_x, HEIGHT), 2)
      pygame.draw.rect(screen, COLOR_OPTIONS[p1_color_idx], p1_goal, 4)
      pygame.draw.rect(screen, COLOR_OPTIONS[p2_color_idx], p2_goal, 4)
      
      if UI_state in ["GAME_PLAY", "PAUSE"]:
         pygame.draw.circle(screen, COLOR_OPTIONS[p1_color_idx], (p1_cx, p1_cy), p1_radius)
         pygame.draw.circle(screen, COLOR_OPTIONS[p2_color_idx], (p2_cx, p2_cy), p2_radius)
      
      pygame.draw.ellipse(screen, WHITE, ball_rect)
      
      if UI_state == "MAIN_MENU":
         title_text = title_font.render("AIR HOCKEY PONG", True, WHITE)
         screen.blit(title_text, (WIDTH // 2 - title_text.get_width() // 2, int(HEIGHT * 0.15)))
         for rect, text, color in [(btn_start_rect, "START GAME", GREEN), (btn_setting_rect, "SETTINGS", GRAY), (btn_exit_rect, "EXIT", RED)]:
            is_hover = rect.collidepoint(mouse_pos)
            draw_color = [min(c + 40, 255) if is_hover else c for c in color]
            pygame.draw.rect(screen, draw_color, rect, border_radius=10)
            txt_surf = btn_font.render(text, True, WHITE)
            screen.blit(txt_surf, (rect.centerx - txt_surf.get_width() // 2, rect.centery - txt_surf.get_height() // 2))

      elif UI_state in ["GAME_PLAY", "PAUSE"]:
         score_text = font.render(f"{p1_score}   :   {p2_score}", True, WHITE)
         screen.blit(score_text, (WIDTH // 2 - score_text.get_width() // 2, int(HEIGHT * 0.04)))
         
         elapsed_sec = game_elapsed_time / 1000.0
         limit_sec = game_time_limit / 1000.0
         time_text = btn_font.render(f"TIME: {elapsed_sec:.1f} / {limit_sec:.0f}s", True, YELLOW)
         screen.blit(time_text, (WIDTH // 2 - time_text.get_width() // 2, int(HEIGHT * 0.12)))

         if UI_state == "GAME_PLAY" and countdown_active:
            count_val = math.ceil(countdown_timer / 1000.0)
            if count_val > 0:
               count_text = countdown_font.render(str(count_val), True, YELLOW)
               screen.blit(count_text, (WIDTH // 2 - count_text.get_width() // 2, HEIGHT // 2 - count_text.get_height() // 2))

         if UI_state == "GAME_PLAY" and not ball_active and not countdown_active:
            sub_text = btn_font.render("Hit the ball to serve!", True, YELLOW)
            screen.blit(sub_text, (WIDTH // 2 - sub_text.get_width() // 2, int(HEIGHT * 0.18)))
         if UI_state == "PAUSE":
            pause_text = title_font.render("PAUSED", True, GRAY)
            screen.blit(pause_text, (WIDTH // 2 - pause_text.get_width() // 2, HEIGHT // 2 - pause_text.get_height() // 2))

      elif UI_state == "SETTINGS":
         set_title_text = title_font.render("SETTINGS", True, WHITE)
         screen.blit(set_title_text, (WIDTH // 2 - set_title_text.get_width() // 2, int(HEIGHT * 0.15)))
         pygame.draw.rect(screen, GRAY, setting_box_rect, 2, border_radius=15)
         labels = ["1. SOUND SETTINGS", "2. COLOR CUSTOM", "3. GAME CUSTOM", "4. CREATOR CREDITS"]
         for i, label_text in enumerate(labels):
            color_preset = YELLOW if (current_setting_index == i and not popup_type) else WHITE
            lbl_surf = font.render(label_text, True, color_preset)
            screen.blit(lbl_surf, (setting_box_rect.left + int(WIDTH * 0.05), setting_y_positions[i]))
         is_hover = btn_back_rect.collidepoint(mouse_pos) or (current_setting_index == 4 and not popup_type)
         back_color = [min(c + 40, 255) if is_hover else c for c in GRAY]
         pygame.draw.rect(screen, back_color, btn_back_rect, border_radius=8)
         back_txt = btn_font.render("BACK", True, WHITE)
         screen.blit(back_txt, (btn_back_rect.centerx - back_txt.get_width() // 2, btn_back_rect.centery - back_txt.get_height() // 2))

         if not popup_type:
            cursor_text = font.render("->", True, YELLOW)
            cursor_x = btn_back_rect.left - int(WIDTH * 0.03) if current_setting_index == 4 else setting_box_rect.left + int(WIDTH * 0.01)
            cursor_y = setting_y_positions[4] + (btn_back_rect.height // 2) - (cursor_text.get_height() // 2) if current_setting_index == 4 else setting_y_positions[current_setting_index]
            screen.blit(cursor_text, (cursor_x, cursor_y))

         if popup_type:
            popup_w, popup_h = int(WIDTH * 0.45), int(HEIGHT * 0.35)
            popup_rect = pygame.Rect(WIDTH // 2 - popup_w // 2, HEIGHT // 2 - popup_h // 2, popup_w, popup_h)
            pygame.draw.rect(screen, BLACK, popup_rect)
            pygame.draw.rect(screen, YELLOW, popup_rect, 4, border_radius=12)
            if popup_type == "SOUND":
               p_title = font.render(" [ SOUND CONFIG ] ", True, YELLOW)
               screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.03)))
               for idx, text in enumerate(["SOUND: ON", "SOUND: OFF"]):
                  target_rect = pygame.Rect(popup_rect.left + int(popup_w * (0.15 if idx==0 else 0.55)), popup_rect.top + int(HEIGHT * 0.14), int(popup_w * 0.3), int(HEIGHT * 0.06))
                  pygame.draw.rect(screen, GREEN if popup_sub_index == idx else GRAY, target_rect, border_radius=6)
                  txt_s = btn_font.render(text, True, WHITE)
                  screen.blit(txt_s, (target_rect.centerx - txt_s.get_width() // 2, target_rect.centery - txt_s.get_height() // 2))
            elif popup_type == "COLOR":
               p_title = font.render(" [ PADDLE COLOR CUSTOM ] ", True, YELLOW)
               screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.02)))
               options_text = [f"P1 COLOR : < {COLOR_NAMES[p1_color_idx]} >", f"P2 COLOR : < {COLOR_NAMES[p2_color_idx]} >", "[ SAVE & EXIT ]", "[ CANCEL ]"]
               for idx, text in enumerate(options_text):
                  current_y = popup_rect.top + int(HEIGHT * 0.09) + (idx * int(HEIGHT * 0.05))
                  if idx == 0: pygame.draw.rect(screen, COLOR_OPTIONS[p1_color_idx], (popup_rect.right - int(popup_w * 0.18), current_y + 5, 25, 25))
                  elif idx == 1: pygame.draw.rect(screen, COLOR_OPTIONS[p2_color_idx], (popup_rect.right - int(popup_w * 0.18), current_y + 5, 25, 25))
                  txt_s = btn_font.render(text, True, YELLOW if popup_sub_index == idx else WHITE)
                  screen.blit(txt_s, (popup_rect.left + int(popup_w * 0.1), current_y))
            elif popup_type == "GAME_CUSTOM":
               p_title = font.render(" [ GAME CUSTOM ] ", True, YELLOW)
               screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.03)))
               options_text = ["1. SETTING TIMES", "[ BACK ]"]
               for idx, text in enumerate(options_text):
                  current_y = popup_rect.top + int(HEIGHT * 0.12) + (idx * int(HEIGHT * 0.08))
                  color_preset = YELLOW if popup_sub_index == idx else WHITE
                  txt_s = btn_font.render(text, True, color_preset)
                  screen.blit(txt_s, (popup_rect.centerx - txt_s.get_width() // 2, current_y))
            elif popup_type == "SETTING_TIMES":
               p_title = font.render(" [ SETTING TIMES ] ", True, YELLOW)
               screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.02)))
               options_text = ["10 SECONDS", "30 SECONDS", "1 MINUTE", "2 MINUTES", "[ SAVE & EXIT ]"]
               for idx, text in enumerate(options_text):
                  current_y = popup_rect.top + int(HEIGHT * 0.08) + (idx * int(HEIGHT * 0.05))
                  marker = "* " if idx == time_limit_idx else "  "
                  color_val = GREEN if idx == time_limit_idx else (YELLOW if popup_sub_index == idx else WHITE)
                  txt_s = btn_font.render(marker + text, True, color_val)
                  screen.blit(txt_s, (popup_rect.left + int(popup_w * 0.1), current_y))
            elif popup_type == "CREATOR":
               p_title = font.render(" [ TEAM CREDITS ] ", True, YELLOW)
               screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.03)))
               for idx, line in enumerate(["DEVELOPER: Team Hockey Pong", "HARDWARE: ESP32 Wi-Fi UDP", "GRAPHICS: Pygame Native Framework"]):
                  txt_s = btn_font.render(line, True, WHITE)
                  screen.blit(txt_s, (popup_rect.centerx - txt_s.get_width() // 2, popup_rect.top + int(HEIGHT * 0.12) + (idx * 30)))

      pygame.display.flip()

   tx_socket.close()
   pygame.quit()
   sys.exit()

if __name__ == "__main__":
   net_thread = threading.Thread(target=udp_server_thread, daemon=True)
   net_thread.start()
   run_game()
