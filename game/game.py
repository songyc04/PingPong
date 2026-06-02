import pygame
import sys
import socket
import threading

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

# 컬러 커스텀을 위한 색상 리스트
COLOR_OPTIONS = [RED, BLUE, GREEN, YELLOW, ORANGE, PURPLE, WHITE]
COLOR_NAMES = ["RED", "BLUE", "GREEN", "YELLOW", "ORANGE", "PURPLE", "WHITE"]

# --- 전역 게임 상태 및 네트워크 데이터 정의 ---
UI_state = "MAIN_MENU"
network_command = ""
command_lock = threading.Lock()

# --- 설정 데이터 변수 ---
sound_enabled = True
p1_color_idx = 0  # 기본 RED
p2_color_idx = 1  # 기본 BLUE

# --- 설정 화면 커서 및 팝업 상태 정의 ---
current_setting_index = 0  # 0: SOUND, 1: COLOR, 2: CREATOR, 3: BACK
popup_type = ""            # "", "SOUND", "COLOR", "CREATOR"
popup_sub_index = 0        # 팝업 내부 커서 인덱스

# --- 백그라운드에서 ESP32/클라이언트 신호를 수신하는 스레드 함수 ---
def socket_server_thread():
   global network_command
   SERVER_IP = "0.0.0.0"
   SERVER_PORT = 10002

   server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
   server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
   
   try:
      server_socket.bind((SERVER_IP, SERVER_PORT))
      server_socket.listen(1)
      print(f"[네트워크] 소켓 서버가 {SERVER_PORT} 포트에서 대기 중입니다...")
   except Exception as e:
      print(f"[네트워크] 서버 바인딩 실패: {e}")
      return

   while True:
      try:
         client_socket, addr = server_socket.accept()
         print(f"[네트워크] 클라이언트 연결됨: {addr}")
         
         while True:
            data = client_socket.recv(1024)
            if not data:
               break
               
            try:
               client_socket.sendall(data)
            except Exception as send_error:
               print(f"[네트워크] 에코 데이터 전송 실패: {send_error}")
               
            raw_msg = data.decode("utf-8").strip()
            
            with command_lock:
               network_command = raw_msg
               
         client_socket.close()
         print("[네트워크] 클라이언트 연결 종료")
      except Exception as e:
         print(f"[네트워크] 통신 오류: {e}")
         break

def run_game():
   global UI_state, network_command, current_setting_index, popup_type, popup_sub_index
   global sound_enabled, p1_color_idx, p2_color_idx
   
   pygame.init()
   
   info = pygame.display.Info()
   WIDTH, HEIGHT = info.current_w, info.current_h
   
   screen = pygame.display.set_mode((WIDTH, HEIGHT), pygame.NOFRAME | pygame.HWSURFACE | pygame.DOUBLEBUF)
   pygame.display.set_caption("ESP32 하키 핑퐁 - 벽 충돌 완벽 보정")
   clock = pygame.time.Clock()

   # 해상도 기반 크기 및 속도 최적화
   paddle_width = int(WIDTH * 0.005)
   paddle_height = int(HEIGHT * 0.12)
   goal_width = int(WIDTH * 0.008)
   goal_height = int(HEIGHT * 0.3)
   ball_size = int(HEIGHT * 0.03)
   
   paddle_speed = int(HEIGHT * 0.02)
   base_ball_speed_x = int(WIDTH * 0.008)
   base_ball_speed_y = int(HEIGHT * 0.008)
   
   p1_x = int(WIDTH * 0.04)
   p1_y = HEIGHT // 2 - paddle_height // 2
   p2_x = WIDTH - int(WIDTH * 0.04) - paddle_width
   p2_y = HEIGHT // 2 - paddle_height // 2
   center_line_x = WIDTH // 2
   
   p1_goal = pygame.Rect(0, HEIGHT // 2 - goal_height // 2, goal_width, goal_height)
   p2_goal = pygame.Rect(WIDTH - goal_width, HEIGHT // 2 - goal_height // 2, goal_width, goal_height)
   
   ball_x, ball_y = WIDTH // 2, HEIGHT // 2
   ball_speed_x, ball_speed_y = base_ball_speed_x, base_ball_speed_y
   
   p1_score = 0
   p2_score = 0
   
   # 폰트 설정
   title_font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.08))
   font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.05))
   btn_font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.035))

   # 메인 메뉴 레이아웃
   btn_width, btn_height = int(WIDTH * 0.22), int(HEIGHT * 0.08)
   btn_start_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.45), btn_width, btn_height)
   btn_setting_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.58), btn_width, btn_height)
   btn_exit_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.71), btn_width, btn_height)

   # 설정 메뉴 레이아웃
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
               if popup_type:
                  popup_type = ""
               elif UI_state in ["GAME_PLAY", "PAUSE", "SETTINGS"]:
                  UI_state = "MAIN_MENU"
               else:
                  running = False
         
         elif event.type == pygame.MOUSEBUTTONDOWN:
            if event.button == 1:
               if popup_type: 
                  popup_type = ""
                  continue

               if UI_state == "MAIN_MENU":
                  if btn_start_rect.collidepoint(mouse_pos):
                     p1_score, p2_score = 0, 0
                     ball_x, ball_y = WIDTH // 2, HEIGHT // 2
                     p1_x = int(WIDTH * 0.04)
                     p2_x = WIDTH - int(WIDTH * 0.04) - paddle_width
                     UI_state = "GAME_PLAY"
                  elif btn_setting_rect.collidepoint(mouse_pos):
                     UI_state = "SETTINGS"
                     current_setting_index = 0
                  elif btn_exit_rect.collidepoint(mouse_pos):
                     running = False
                     
               elif UI_state == "SETTINGS":
                  if btn_back_rect.collidepoint(mouse_pos):
                     UI_state = "MAIN_MENU"
                  elif setting_box_rect.left <= mouse_pos[0] <= setting_box_rect.right:
                     if setting_y_positions[0] <= mouse_pos[1] <= setting_y_positions[0] + 40:
                        current_setting_index = 0
                        popup_type = "SOUND"
                        popup_sub_index = 0 if sound_enabled else 1
                     elif setting_y_positions[1] <= mouse_pos[1] <= setting_y_positions[1] + 40:
                        current_setting_index = 1
                        popup_type = "COLOR"
                        popup_sub_index = 0
                     elif setting_y_positions[2] <= mouse_pos[1] <= setting_y_positions[2] + 40:
                        current_setting_index = 2
                        popup_type = "CREATOR"

      if not running:
         break

      # --- 네트워크 명령어 처리 ---
      current_cmd = ""
      with command_lock:
         if network_command:
            current_cmd = network_command
            network_command = ""

      if current_cmd:
         print(f"[네트워크 수신] {current_cmd}")
         
         if current_cmd == "SRT":
            if UI_state == "MAIN_MENU":
               p1_score, p2_score = 0, 0
               ball_x, ball_y = WIDTH // 2, HEIGHT // 2
               p1_x = int(WIDTH * 0.04)
               p2_x = WIDTH - int(WIDTH * 0.04) - paddle_width
               UI_state = "GAME_PLAY"
            elif UI_state == "PAUSE":
               UI_state = "GAME_PLAY"
         elif current_cmd == "STP":
            if UI_state == "GAME_PLAY":
               UI_state = "PAUSE"
         elif current_cmd == "END":
            UI_state = "MAIN_MENU"
            popup_type = ""
         elif current_cmd == "SET":
            UI_state = "SETTINGS"
            current_setting_index = 0
            popup_type = ""
            
         elif UI_state == "SETTINGS":
            if popup_type:
               # 팝업 켜진 상태 조작 분기
               if popup_type == "SOUND":
                  if current_cmd in ["UP", "DN"]:
                     popup_sub_index = (popup_sub_index + 1) % 2
                  elif current_cmd == "CLK":
                     sound_enabled = (popup_sub_index == 0)
                     popup_type = ""
                     print(f"[설정 변경] SOUND : {sound_enabled}")
                     
               elif popup_type == "COLOR":
                  if current_cmd == "UP":
                     popup_sub_index = (popup_sub_index - 1) % 4
                  elif current_cmd == "DN":
                     popup_sub_index = (popup_sub_index + 1) % 4
                  elif current_cmd == "CLK":
                     if popup_sub_index == 0:
                        p1_color_idx = (p1_color_idx + 1) % len(COLOR_OPTIONS)
                     elif popup_sub_index == 1:
                        p2_color_idx = (p2_color_idx + 1) % len(COLOR_OPTIONS)
                     elif popup_sub_index == 2:  # SAVE
                        popup_type = ""
                     elif popup_sub_index == 3:  # CANCEL
                        popup_type = ""
                        
               elif popup_type == "CREATOR":
                  if current_cmd == "CLK":
                     popup_type = ""
            else:
               # 팝업 없는 메인 설정창 조작
               if current_cmd == "UP":
                  current_setting_index = (current_setting_index - 1) % 4
               elif current_cmd == "DN":
                  current_setting_index = (current_setting_index + 1) % 4
               elif current_cmd == "CLK":
                  if current_setting_index == 0:
                     popup_type = "SOUND"
                     popup_sub_index = 0 if sound_enabled else 1
                  elif current_setting_index == 1:
                     popup_type = "COLOR"
                     popup_sub_index = 0
                  elif current_setting_index == 2:
                     popup_type = "CREATOR"
                  elif current_setting_index == 3:
                     UI_state = "MAIN_MENU"

      # --- [1] 메인 메뉴 화면 처리 ---
      if UI_state == "MAIN_MENU":
         screen.fill(BLACK)
         title_text = title_font.render("AIR HOCKEY PONG", True, WHITE)
         screen.blit(title_text, (WIDTH // 2 - title_text.get_width() // 2, int(HEIGHT * 0.15)))
         
         for rect, text, color in [(btn_start_rect, "START GAME", GREEN), 
                                   (btn_setting_rect, "SETTINGS", GRAY), 
                                   (btn_exit_rect, "EXIT", RED)]:
            is_hover = rect.collidepoint(mouse_pos)
            draw_color = [min(c + 40, 255) if is_hover else c for c in color]
            
            pygame.draw.rect(screen, draw_color, rect, border_radius=10)
            txt_surf = btn_font.render(text, True, WHITE)
            screen.blit(txt_surf, (rect.centerx - txt_surf.get_width() // 2, rect.centery - txt_surf.get_height() // 2))

      # --- [2] 게임 플레이 또는 일시정지 화면 처리 ---
      elif UI_state in ["GAME_PLAY", "PAUSE"]:
         if UI_state == "GAME_PLAY":
            keys = pygame.key.get_pressed()
            
            # --- Player 1 (WSAD) 제어 ---
            if keys[pygame.K_w] and p1_y > 0: p1_y -= paddle_speed
            if keys[pygame.K_s] and p1_y < HEIGHT - paddle_height: p1_y += paddle_speed
            if keys[pygame.K_a] and p1_x > 0: p1_x -= paddle_speed
            if keys[pygame.K_d] and (p1_x + paddle_width) < center_line_x: p1_x += paddle_speed
               
            # --- Player 2 (방향키) 제어 ---
            if keys[pygame.K_UP] and p2_y > 0: p2_y -= paddle_speed
            if keys[pygame.K_DOWN] and p2_y < HEIGHT - paddle_height: p2_y += paddle_speed
            if keys[pygame.K_LEFT] and p2_x > center_line_x: p2_x -= paddle_speed
            if keys[pygame.K_RIGHT] and p2_x < WIDTH - paddle_width: p2_x += paddle_speed

            # --- 공 이동 ---
            ball_x += ball_speed_x
            ball_y += ball_speed_y

            # --- [수정] 상하 벽면 예외처리 고정 (튕김 보정) ---
            if ball_y <= 0:
               ball_speed_y = abs(ball_speed_y)
               ball_y = 0
            elif ball_y >= HEIGHT - ball_size:
               ball_speed_y = -abs(ball_speed_y)
               ball_y = HEIGHT - ball_size

            p1_rect = pygame.Rect(p1_x, p1_y, paddle_width, paddle_height)
            p2_rect = pygame.Rect(p2_x, p2_y, paddle_width, paddle_height)
            ball_rect = pygame.Rect(ball_x, ball_y, ball_size, ball_size)

            # --- Player 1 충돌 판정 ---
            if ball_rect.colliderect(p1_rect):
               overlap_left = ball_rect.right - p1_rect.left
               overlap_right = p1_rect.right - ball_rect.left
               overlap_top = ball_rect.bottom - p1_rect.top
               overlap_bottom = p1_rect.bottom - ball_rect.top
               min_overlap = min(overlap_left, overlap_right, overlap_top, overlap_bottom)

               if min_overlap == overlap_left:
                  ball_speed_x = -abs(ball_speed_x)
                  ball_x = p1_rect.left - ball_size
               elif min_overlap == overlap_right:
                  ball_speed_x = abs(ball_speed_x)
                  ball_x = p1_rect.right
               elif min_overlap == overlap_top:
                  ball_speed_y = -abs(ball_speed_y)
                  ball_y = p1_rect.top - ball_size
               elif min_overlap == overlap_bottom:
                  ball_speed_y = abs(ball_speed_y)
                  ball_y = p1_rect.bottom

            # --- Player 2 충돌 판정 ---
            if ball_rect.colliderect(p2_rect):
               overlap_left = ball_rect.right - p2_rect.left
               overlap_right = p2_rect.right - ball_rect.left
               overlap_top = ball_rect.bottom - p2_rect.top
               overlap_bottom = p2_rect.bottom - ball_rect.top
               min_overlap = min(overlap_left, overlap_right, overlap_top, overlap_bottom)

               if min_overlap == overlap_left:
                  ball_speed_x = -abs(ball_speed_x)
                  ball_x = p2_rect.left - ball_size
               elif min_overlap == overlap_right:
                  ball_speed_x = abs(ball_speed_x)
                  ball_x = p2_rect.right
               elif min_overlap == overlap_top:
                  ball_speed_y = -abs(ball_speed_y)
                  ball_y = p2_rect.top - ball_size
               elif min_overlap == overlap_bottom:
                  ball_speed_y = abs(ball_speed_y)
                  ball_y = p2_rect.bottom

            ball_x, ball_y = ball_rect.x, ball_rect.y

            # --- [수정] 골인 판정 및 좌우 일반벽면 충돌 완전 분기 ---
            if ball_rect.colliderect(p1_goal):
               p2_score += 1
               ball_x, ball_y = WIDTH // 2, HEIGHT // 2
               ball_speed_x = base_ball_speed_x
               ball_speed_y = base_ball_speed_y
            elif ball_rect.colliderect(p2_goal):
               p1_score += 1
               ball_x, ball_y = WIDTH // 2, HEIGHT // 2
               ball_speed_x = -base_ball_speed_x
               ball_speed_y = base_ball_speed_y
            else:
               # 골대가 아닌 일반 좌우 벽면 충돌 처리 (화면 밖 튕김 억제)
               if ball_x <= 0:
                  ball_speed_x = abs(ball_speed_x)
                  ball_x = 0
               elif ball_x >= WIDTH - ball_size:
                  ball_speed_x = -abs(ball_speed_x)
                  ball_x = WIDTH - ball_size
         else:
            p1_rect = pygame.Rect(p1_x, p1_y, paddle_width, paddle_height)
            p2_rect = pygame.Rect(p2_x, p2_y, paddle_width, paddle_height)
            ball_rect = pygame.Rect(ball_x, ball_y, ball_size, ball_size)

         # --- 인게임 그래픽 렌더링 ---
         screen.fill(BLACK)
         pygame.draw.line(screen, GRAY, (center_line_x, 0), (center_line_x, HEIGHT), 2)
         pygame.draw.rect(screen, COLOR_OPTIONS[p1_color_idx], p1_goal, 4)
         pygame.draw.rect(screen, COLOR_OPTIONS[p2_color_idx], p2_goal, 4)
         
         pygame.draw.rect(screen, COLOR_OPTIONS[p1_color_idx], p1_rect)
         pygame.draw.rect(screen, COLOR_OPTIONS[p2_color_idx], p2_rect)
         pygame.draw.ellipse(screen, WHITE, ball_rect)
         
         score_text = font.render(f"{p1_score}   :   {p2_score}", True, WHITE)
         screen.blit(score_text, (WIDTH // 2 - score_text.get_width() // 2, int(HEIGHT * 0.04)))

         if UI_state == "PAUSE":
            pause_text = title_font.render("PAUSED", True, GRAY)
            screen.blit(pause_text, (WIDTH // 2 - pause_text.get_width() // 2, HEIGHT // 2 - pause_text.get_height() // 2))

      # --- [3] 설정 화면 처리 ---
      elif UI_state == "SETTINGS":
         screen.fill(BLACK)
         
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
            if current_setting_index == 3:
               cursor_x = btn_back_rect.left - int(WIDTH * 0.03)
               cursor_y = setting_y_positions[3] + (btn_back_rect.height // 2) - (cursor_text.get_height() // 2)
            else:
               cursor_x = setting_box_rect.left + int(WIDTH * 0.01)
               cursor_y = setting_y_positions[current_setting_index]
            screen.blit(cursor_text, (cursor_x, cursor_y))

         # ==================== 개별 독립 팝업창 모달 렌더링 ====================
         if popup_type:
            popup_w, popup_h = int(WIDTH * 0.45), int(HEIGHT * 0.35)
            popup_rect = pygame.Rect(WIDTH // 2 - popup_w // 2, HEIGHT // 2 - popup_h // 2, popup_w, popup_h)
            
            pygame.draw.rect(screen, BLACK, popup_rect)
            pygame.draw.rect(screen, YELLOW, popup_rect, 4, border_radius=12)
            
            if popup_type == "SOUND":
               p_title = font.render(" [ SOUND CONFIG ] ", True, YELLOW)
               screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.03)))
               
               for idx, text in enumerate(["SOUND: ON", "SOUND: OFF"]):
                  btn_x = popup_rect.left + int(popup_w * 0.15) if idx == 0 else popup_rect.left + int(popup_w * 0.55)
                  btn_y = popup_rect.top + int(HEIGHT * 0.14)
                  rect_w, rect_h = int(popup_w * 0.3), int(HEIGHT * 0.06)
                  target_rect = pygame.Rect(btn_x, btn_y, rect_w, rect_h)
                  
                  bg_color = GREEN if popup_sub_index == idx else GRAY
                  pygame.draw.rect(screen, bg_color, target_rect, border_radius=6)
                  
                  txt_s = btn_font.render(text, True, WHITE)
                  screen.blit(txt_s, (target_rect.centerx - txt_s.get_width() // 2, target_rect.centery - txt_s.get_height() // 2))
                  
               info_s = btn_font.render("Press UP/DN to toggle | CLK to Save", True, WHITE)
               screen.blit(info_s, (popup_rect.centerx - info_s.get_width() // 2, popup_rect.bottom - int(HEIGHT * 0.05)))

            elif popup_type == "COLOR":
               p_title = font.render(" [ PADDLE COLOR CUSTOM ] ", True, YELLOW)
               screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.02)))
               
               opt_y_start = popup_rect.top + int(HEIGHT * 0.09)
               options_text = [
                  f"P1 COLOR : < {COLOR_NAMES[p1_color_idx]} >",
                  f"P2 COLOR : < {COLOR_NAMES[p2_color_idx]} >",
                  "[ SAVE & EXIT ]",
                  "[ CANCEL ]"
               ]
               
               for idx, text in enumerate(options_text):
                  current_y = opt_y_start + (idx * int(HEIGHT * 0.05))
                  is_selected = (popup_sub_index == idx)
                  text_color = YELLOW if is_selected else WHITE
                  
                  if idx == 0:
                     pygame.draw.rect(screen, COLOR_OPTIONS[p1_color_idx], (popup_rect.right - int(popup_w * 0.18), current_y + 5, 25, 25))
                  elif idx == 1:
                     pygame.draw.rect(screen, COLOR_OPTIONS[p2_color_idx], (popup_rect.right - int(popup_w * 0.18), current_y + 5, 25, 25))
                     
                  txt_s = btn_font.render(text, True, text_color)
                  screen.blit(txt_s, (popup_rect.left + int(popup_w * 0.1), current_y))

            elif popup_type == "CREATOR":
               p_title = font.render(" [ TEAM CREDITS ] ", True, YELLOW)
               screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.03)))
               
               lines = [
                  "DEVELOPER: Team Hockey Pong",
                  "HARDWARE: ESP32 Wi-Fi Socket",
                  "GRAPHICS: Pygame Native Framework"
               ]
               for idx, line in enumerate(lines):
                  txt_s = btn_font.render(line, True, WHITE)
                  screen.blit(txt_s, (popup_rect.centerx - txt_s.get_width() // 2, popup_rect.top + int(HEIGHT * 0.12) + (idx * 30)))
                  
               close_s = btn_font.render("[ Press CLK to close ]", True, GRAY)
               screen.blit(close_s, (popup_rect.centerx - close_s.get_width() // 2, popup_rect.bottom - int(HEIGHT * 0.05)))

      pygame.display.flip()
      clock.tick(60)

   pygame.quit()
   sys.exit()

if __name__ == "__main__":
   net_thread = threading.Thread(target=socket_server_thread, daemon=True)
   net_thread.start()
   run_game()