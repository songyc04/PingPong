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

# --- 설정 화면 커서 및 팝업 상태 정의 ---
current_setting_index = 0  
popup_type = ""            
popup_sub_index = 0        

def parse_joystick(raw_msg):
   """
   [UDP 매핑 알고리즘] “정수,정수,정수,정수”
   중립 범위: 2630 ~ 2750, 최솟값 0, 최댓값 4095
   """
   global p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y
   
   try:
      data = raw_msg.split(',')
      if len(data) < 4: 
         return
         
      p1_x = int(data[0])
      p1_y = int(data[1])
      p2_x = int(data[2])
      p2_y = int(data[3])
      
      # --- P1 조이스틱 정규화 ---
      if 2630 <= p1_x <= 2750:   p1_jx = 0.0
      elif p1_x < 2630:          p1_jx = (p1_x - 2630) / 2630.0
      else:                      p1_jx = (p1_x - 2750) / (4095.0 - 2750.0)
         
      if 2630 <= p1_y <= 2750:   p1_jy = 0.0
      elif p1_y < 2630:          p1_jy = (p1_y - 2630) / 2630.0
      else:                      p1_jy = (p1_y - 2750) / (4095.0 - 2750.0)

      # --- P2 조이스틱 정규화 ---
      if 2630 <= p2_x <= 2750:   p2_jx = 0.0
      elif p2_x < 2630:          p2_jx = (p2_x - 2630) / 2630.0
      else:                      p2_jx = (p2_x - 2750) / (4095.0 - 2750.0)
         
      if 2630 <= p2_y <= 2750:   p2_jy = 0.0
      elif p2_y < 2630:          p2_jy = (p2_y - 2630) / 2630.0
      else:                      p2_jy = (p2_y - 2750) / (4095.0 - 2750.0)

      # 변수 업데이트
      p1_joy_x, p1_joy_y = p1_jx, p1_jy
      p2_joy_x, p2_joy_y = p2_jx, p2_jy
            
   except Exception:
      pass

def udp_server_thread():
   """
   UDP 수신 전용 스레드
   지연 시간(Latency)이 사실상 제로에 가깝게 작동합니다.
   """
   global network_command
   SERVER_IP = "0.0.0.0"
   SERVER_PORT = 10001

   # SOCK_DGRAM 으로 변경하여 UDP 소켓 생성
   udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
   
   try:
      udp_socket.bind((SERVER_IP, SERVER_PORT))
      print(f"[네트워크] UDP 서버가 {SERVER_PORT} 포트에서 개방되었습니다.")
   except Exception as e:
      print(f"[네트워크] UDP 서버 바인딩 실패: {e}")
      return

   while True:
      try:
         # UDP는 accept() 과정 없이 바로 데이터를 패킷 단위로 수신합니다.
         data, addr = udp_socket.recvfrom(1024)
         if not data:
            continue
            
         raw_msg = data.decode("utf-8").strip()
         
         # 쉼표(,) 개수로 데이터 타입 식별
         if raw_msg.count(',') >= 3:
            with command_lock:
               parse_joystick(raw_msg)
         else:
            # 일반 명령 문자열 처리 (SRT, STP, UP, DN, CLK 등)
            with command_lock:
               network_command = raw_msg
               
      except Exception as e:
         print(f"[네트워크] UDP 수신 오류: {e}")
         break

def run_game():
   global UI_state, network_command, current_setting_index, popup_type, popup_sub_index
   global sound_enabled, p1_color_idx, p2_color_idx
   global p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y
   
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
   
   title_font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.08))
   font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.05))
   btn_font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.035))

   btn_width, btn_height = int(WIDTH * 0.22), int(HEIGHT * 0.08)
   btn_start_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.45), btn_width, btn_height)
   btn_setting_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.58), btn_width, btn_height)
   btn_exit_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.71), btn_width, btn_height)

   setting_box_width, setting_box_height = int(WIDTH * 0.6), int(HEIGHT * 0.5)
   setting_box_rect = pygame.Rect(WIDTH // 2 - setting_box_width // 2, int(HEIGHT * 0.3), setting_box_width, setting_box_height)

   btn_back_width, btn_back_height = int(WIDTH * 0.15), int(HEIGHT * 0.06)
   btn_back_rect = pygame.Rect(WIDTH // 2 - btn_back_width // 2, int(HEIGHT * 0.72), btn_back_width, btn_back_height)

   setting_y_positions = [int(HEIGHT * 0.38), int(HEIGHT * 0.48), int(HEIGHT * 0.58), int(HEIGHT * 0.72)]

   running = True
   while running:
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
                     UI_state = "GAME_PLAY"
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

      if current_cmd:
         if current_cmd == "SRT":
            if UI_state == "MAIN_MENU":
               p1_score, p2_score = 0, 0
               ball_x = WIDTH // 2 - ball_size // 2
               ball_y = HEIGHT // 2 - ball_size // 2
               ball_active = False  
               p1_cx, p1_cy = int(WIDTH * 0.06), HEIGHT // 2
               p2_cx, p2_cy = WIDTH - int(WIDTH * 0.06), HEIGHT // 2
               UI_state = "GAME_PLAY"
            elif UI_state == "PAUSE": UI_state = "GAME_PLAY"
         elif current_cmd == "STP":
            if UI_state == "GAME_PLAY": UI_state = "PAUSE"
         elif current_cmd == "END":
            UI_state = "MAIN_MENU"
            popup_type = ""
         elif current_cmd == "SET":
            UI_state = "SETTINGS"
            current_setting_index = 0
            popup_type = ""
         elif UI_state == "SETTINGS":
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
               elif popup_type == "CREATOR":
                  if current_cmd == "CLK": popup_type = ""
            else:
               if current_cmd == "UP": current_setting_index = (current_setting_index - 1) % 4
               elif current_cmd == "DN": current_setting_index = (current_setting_index + 1) % 4
               elif current_cmd == "CLK":
                  if current_setting_index == 0:
                     popup_type = "SOUND"
                     popup_sub_index = 0 if sound_enabled else 1
                  elif current_setting_index == 1:
                     popup_type = "COLOR"
                     popup_sub_index = 0
                  elif current_setting_index == 2: popup_type = "CREATOR"
                  elif current_setting_index == 3: UI_state = "MAIN_MENU"

      # --- 패들 실시간 이동 판정 ---
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

         # 공 역학 물리 연산
         if ball_active:
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
            if not ball_active:  
               ball_active = True
               ball_speed_x = abs(base_ball_speed_x)
               ball_speed_y = base_ball_speed_y if dy1 >= 0 else -base_ball_speed_y
               ball_x = p1_cx + p1_radius + 2
            else:
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
            if not ball_active:  
               ball_active = True
               ball_speed_x = -abs(base_ball_speed_x)
               ball_speed_y = base_ball_speed_y if dy2 >= 0 else -base_ball_speed_y
               ball_x = p2_cx - p2_radius - ball_size - 2
            else:
               if distance2 == 0: distance2 = 0.1
               nx, ny = dx2 / distance2, dy2 / distance2
               dot_product = ball_speed_x * nx + ball_speed_y * ny
               ball_speed_x = ball_speed_x - 2 * dot_product * nx
               ball_speed_y = ball_speed_y - 2 * dot_product * ny
               ball_cx = p2_cx + nx * min_dist2
               ball_cy = p2_cy + ny * min_dist2
               ball_x, ball_y = ball_cx - ball_radius, ball_cy - ball_radius

         ball_rect = pygame.Rect(ball_x, ball_y, ball_size, ball_size)

         if ball_rect.colliderect(p1_goal):
            p2_score += 1
            ball_x = int(WIDTH * 0.25) - ball_size // 2
            ball_y = HEIGHT // 2 - ball_size // 2
            ball_speed_x, ball_speed_y = base_ball_speed_x, base_ball_speed_y
            ball_active = False
         elif ball_rect.colliderect(p2_goal):
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
         if UI_state == "GAME_PLAY" and not ball_active:
            sub_text = btn_font.render("Hit the ball to serve!", True, YELLOW)
            screen.blit(sub_text, (WIDTH // 2 - sub_text.get_width() // 2, int(HEIGHT * 0.12)))
         if UI_state == "PAUSE":
            pause_text = title_font.render("PAUSED", True, GRAY)
            screen.blit(pause_text, (WIDTH // 2 - pause_text.get_width() // 2, HEIGHT // 2 - pause_text.get_height() // 2))

      elif UI_state == "SETTINGS":
         set_title_text = title_font.render("SETTINGS", True, WHITE)
         screen.blit(set_title_text, (WIDTH // 2 - set_title_text.get_width() // 2, int(HEIGHT * 0.15)))
         pygame.draw.rect(screen, GRAY, setting_box_rect, 2, border_radius=15)
         labels = ["1. SOUND SETTINGS", "2. COLOR CUSTOM", "3. CREATOR CREDITS"]
         for i, label_text in enumerate(labels):
            color_preset = YELLOW if (current_setting_index == i and not popup_type) else WHITE
            lbl_surf = font.render(label_text, True, color_preset)
            screen.blit(lbl_surf, (setting_box_rect.left + int(WIDTH * 0.05), setting_y_positions[i]))
         is_hover = btn_back_rect.collidepoint(mouse_pos) or (current_setting_index == 3 and not popup_type)
         back_color = [min(c + 40, 255) if is_hover else c for c in GRAY]
         pygame.draw.rect(screen, back_color, btn_back_rect, border_radius=8)
         back_txt = btn_font.render("BACK", True, WHITE)
         screen.blit(back_txt, (btn_back_rect.centerx - back_txt.get_width() // 2, btn_back_rect.centery - back_txt.get_height() // 2))

         if not popup_type:
            cursor_text = font.render("->", True, YELLOW)
            cursor_x = btn_back_rect.left - int(WIDTH * 0.03) if current_setting_index == 3 else setting_box_rect.left + int(WIDTH * 0.01)
            cursor_y = setting_y_positions[3] + (btn_back_rect.height // 2) - (cursor_text.get_height() // 2) if current_setting_index == 3 else setting_y_positions[current_setting_index]
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
            elif popup_type == "CREATOR":
               p_title = font.render(" [ TEAM CREDITS ] ", True, YELLOW)
               screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.03)))
               for idx, line in enumerate(["DEVELOPER: Team Hockey Pong", "HARDWARE: ESP32 Wi-Fi UDP", "GRAPHICS: Pygame Native Framework"]):
                  txt_s = btn_font.render(line, True, WHITE)
                  screen.blit(txt_s, (popup_rect.centerx - txt_s.get_width() // 2, popup_rect.top + int(HEIGHT * 0.12) + (idx * 30)))

      pygame.display.flip()
      clock.tick(60)

   pygame.quit()
   sys.exit()

if __name__ == "__main__":
   net_thread = threading.Thread(target=udp_server_thread, daemon=True)
   net_thread.start()
   run_game()