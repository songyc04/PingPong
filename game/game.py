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

# --- 전역 게임 상태 및 네트워크 데이터 정의 ---
UI_state = "MAIN_MENU"
network_command = ""
command_lock = threading.Lock()

# --- 백그라운드에서 ESP32/클라이언트 신호를 수신하는 스레드 함수 ---
def socket_server_thread():
   global network_command
   SERVER_IP = "0.0.0.0"
   SERVER_PORT = 10001

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
               
            # [에코 기능] 수신한 바이트 데이터를 그대로 클라이언트(ESP32)에 재전송
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
   global UI_state, network_command
   pygame.init()
   
   info = pygame.display.Info()
   WIDTH, HEIGHT = info.current_w, info.current_h
   
   screen = pygame.display.set_mode((WIDTH, HEIGHT), pygame.NOFRAME | pygame.HWSURFACE | pygame.DOUBLEBUF)
   pygame.display.set_caption("ESP32 하키 핑퐁 - 완벽한 4면 충돌")
   clock = pygame.time.Clock()

   paddle_width = int(WIDTH * 0.005)
   paddle_height = int(HEIGHT * 0.12)
   goal_width = int(WIDTH * 0.008)
   goal_height = int(HEIGHT * 0.3)
   ball_size = int(HEIGHT * 0.03)
   
   paddle_speed = int(HEIGHT * 0.012)
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
   
   title_font = pygame.font.SysFont("Pretendard", int(HEIGHT * 0.1))
   font = pygame.font.SysFont("Pretendard", int(HEIGHT * 0.06))
   btn_font = pygame.font.SysFont("Pretendard", int(HEIGHT * 0.04))

   # 메인 메뉴 버튼 레이아웃
   btn_width, btn_height = int(WIDTH * 0.2), int(HEIGHT * 0.08)
   btn_start_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.45), btn_width, btn_height)
   btn_setting_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.58), btn_width, btn_height)
   btn_exit_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.71), btn_width, btn_height)

   # 설정창 UI 레이아웃 요소 정의
   setting_box_width, setting_box_height = int(WIDTH * 0.6), int(HEIGHT * 0.5)
   setting_box_rect = pygame.Rect(WIDTH // 2 - setting_box_width // 2, int(HEIGHT * 0.3), setting_box_width, setting_box_height)

   btn_back_width, btn_back_height = int(WIDTH * 0.15), int(HEIGHT * 0.06)
   btn_back_rect = pygame.Rect(WIDTH // 2 - btn_back_width // 2, int(HEIGHT * 0.72), btn_back_width, btn_back_height)

   # 설정 세부 버튼 구역 구획화
   sound_rect = pygame.Rect(WIDTH // 2 - int(WIDTH * 0.1), int(HEIGHT * 0.38), int(WIDTH * 0.2), int(HEIGHT * 0.05))
   color_p1_rect = pygame.Rect(WIDTH // 2 - int(WIDTH * 0.15), int(HEIGHT * 0.48), int(WIDTH * 0.08), int(HEIGHT * 0.05))
   color_p2_rect = pygame.Rect(WIDTH // 2 + int(WIDTH * 0.07), int(HEIGHT * 0.48), int(WIDTH * 0.08), int(HEIGHT * 0.05))

   running = True
   while running:
      mouse_pos = pygame.mouse.get_pos()
      
      for event in pygame.event.get():
         if event.type == pygame.QUIT:
            running = False
         elif event.type == pygame.KEYDOWN:
            if event.key == pygame.K_ESCAPE:
               if UI_state in ["GAME_PLAY", "PAUSE", "SETTINGS"]:
                  UI_state = "MAIN_MENU"
               else:
                  running = False
         
         elif event.type == pygame.MOUSEBUTTONDOWN:
            if event.button == 1:
               if UI_state == "MAIN_MENU":
                  if btn_start_rect.collidepoint(mouse_pos):
                     p1_score, p2_score = 0, 0
                     ball_x, ball_y = WIDTH // 2, HEIGHT // 2
                     p1_x = int(WIDTH * 0.04)
                     p2_x = WIDTH - int(WIDTH * 0.04) - paddle_width
                     UI_state = "GAME_PLAY"
                  elif btn_setting_rect.collidepoint(mouse_pos):
                     UI_state = "SETTINGS"
                  elif btn_exit_rect.collidepoint(mouse_pos):
                     running = False
                     
               elif UI_state == "SETTINGS":
                  if btn_back_rect.collidepoint(mouse_pos):
                     UI_state = "MAIN_MENU"
                  elif sound_rect.collidepoint(mouse_pos):
                     print("[설정] 소리 설정 토글 이벤트 발생")
                  elif color_p1_rect.collidepoint(mouse_pos):
                     print("[설정] Player 1 패들 색상 커스텀 이벤트 발생")
                  elif color_p2_rect.collidepoint(mouse_pos):
                     print("[설정] Player 2 패들 색상 커스텀 이벤트 발생")

      if not running:
         break

      # --- 네트워크 명령어 동기화 및 처리 ---
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
         elif current_cmd == "SET":
            # [기능 추가] SET 명령 수신 시 어떤 화면에서든 설정창으로 이동
            UI_state = "SETTINGS"

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
            if keys[pygame.K_w] and p1_y > 0:
               p1_y -= paddle_speed
            if keys[pygame.K_s] and p1_y < HEIGHT - paddle_height:
               p1_y += paddle_speed
            if keys[pygame.K_a] and p1_x > 0:
               p1_x -= paddle_speed
            if keys[pygame.K_d] and (p1_x + paddle_width) < center_line_x:
               p1_x += paddle_speed
               
            # --- Player 2 (방향키) 제어 ---
            if keys[pygame.K_UP] and p2_y > 0:
               p2_y -= paddle_speed
            if keys[pygame.K_DOWN] and p2_y < HEIGHT - paddle_height:
               p2_y += paddle_speed
            if keys[pygame.K_LEFT] and p2_x > center_line_x:
               p2_x -= paddle_speed
            if keys[pygame.K_RIGHT] and p2_x < WIDTH - paddle_width:
               p2_x += paddle_speed

            # --- 공 이동 ---
            ball_x += ball_speed_x
            ball_y += ball_speed_y

            if ball_y <= 0 or ball_y >= HEIGHT - ball_size:
               ball_speed_y *= -1
               ball_y = max(0, min(ball_y, HEIGHT - ball_size))

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

            # --- 골인 판정 ---
            if ball_rect.colliderect(p1_goal):
               p2_score += 1
               ball_x, ball_y = WIDTH // 2, HEIGHT // 2
               ball_speed_x = base_ball_speed_x
            elif ball_rect.colliderect(p2_goal):
               p1_score += 1
               ball_x, ball_y = WIDTH // 2, HEIGHT // 2
               ball_speed_x = -base_ball_speed_x
            elif ball_x <= 0 or ball_x >= WIDTH - ball_size:
               ball_speed_x *= -1
         else:
            p1_rect = pygame.Rect(p1_x, p1_y, paddle_width, paddle_height)
            p2_rect = pygame.Rect(p2_x, p2_y, paddle_width, paddle_height)
            ball_rect = pygame.Rect(ball_x, ball_y, ball_size, ball_size)

         # --- 인게임 그래픽 렌더링 ---
         screen.fill(BLACK)
         pygame.draw.line(screen, GRAY, (center_line_x, 0), (center_line_x, HEIGHT), 2)
         pygame.draw.rect(screen, RED, p1_goal, 4)
         pygame.draw.rect(screen, BLUE, p2_goal, 4)
         
         pygame.draw.rect(screen, RED, p1_rect)
         pygame.draw.rect(screen, BLUE, p2_rect)
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
         
         # 1. 소리 설정 레이아웃
         sound_label = font.render("1. SOUND", True, WHITE)
         screen.blit(sound_label, (setting_box_rect.left + int(WIDTH * 0.04), int(HEIGHT * 0.36)))
         
         pygame.draw.rect(screen, GRAY, sound_rect, border_radius=5)
         sound_status = btn_font.render("ON / OFF", True, WHITE)
         screen.blit(sound_status, (sound_rect.centerx - sound_status.get_width() // 2, sound_rect.centery - sound_status.get_height() // 2))
         
         # 2. 색상 커스텀 설정 레이아웃
         color_label = font.render("2. COLOR CUSTOM", True, WHITE)
         screen.blit(color_label, (setting_box_rect.left + int(WIDTH * 0.04), int(HEIGHT * 0.46)))
         
         pygame.draw.rect(screen, RED, color_p1_rect, border_radius=5)
         p1_c_txt = btn_font.render("P1 COLOR", True, WHITE)
         screen.blit(p1_c_txt, (color_p1_rect.centerx - p1_c_txt.get_width() // 2, color_p1_rect.centery - p1_c_txt.get_height() // 2))
         
         pygame.draw.rect(screen, BLUE, color_p2_rect, border_radius=5)
         p2_c_txt = btn_font.render("P2 COLOR", True, WHITE)
         screen.blit(p2_c_txt, (color_p2_rect.centerx - p2_c_txt.get_width() // 2, color_p2_rect.centery - p2_c_txt.get_height() // 2))

         # 3. 게임 제작자 크레딧 레이아웃 (형식 고정)
         creator_label = font.render("3. CREATOR", True, WHITE)
         screen.blit(creator_label, (setting_box_rect.left + int(WIDTH * 0.04), int(HEIGHT * 0.56)))
         
         creator_name_text = btn_font.render("TEAM NAME / DEVELOPER INFO HERE", True, GRAY)
         screen.blit(creator_name_text, (WIDTH // 2 - creator_name_text.get_width() // 2, int(HEIGHT * 0.62)))

         # BACK (돌아가기) 버튼 처리
         is_hover = btn_back_rect.collidepoint(mouse_pos)
         back_color = [min(c + 40, 255) if is_hover else c for c in GRAY]
         pygame.draw.rect(screen, back_color, btn_back_rect, border_radius=8)
         back_txt = btn_font.render("BACK", True, WHITE)
         screen.blit(back_txt, (btn_back_rect.centerx - back_txt.get_width() // 2, btn_back_rect.centery - back_txt.get_height() // 2))

      pygame.display.flip()
      clock.tick(60)

   pygame.quit()
   sys.exit()

if __name__ == "__main__":
   net_thread = threading.Thread(target=socket_server_thread, daemon=True)
   net_thread.start()
   
   run_game()