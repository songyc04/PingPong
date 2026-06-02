import pygame
import sys

# --- 전역 게임 상태 정의 ---
UI_state = "MAIN_MENU"

def run_game():
   global UI_state
   pygame.init()
   
   info = pygame.display.Info()
   WIDTH, HEIGHT = info.current_w, info.current_h
   
   screen = pygame.display.set_mode((WIDTH, HEIGHT), pygame.FULLSCREEN | pygame.HWSURFACE | pygame.DOUBLEBUF)
   pygame.display.set_caption("ESP32 하키 핑퐁 - 완벽한 4면 충돌")
   clock = pygame.time.Clock()
   
   WHITE = (255, 255, 255)
   BLACK = (0, 0, 0)
   RED = (255, 50, 50)
   BLUE = (50, 50, 255)
   GRAY = (100, 100, 100)
   GREEN = (50, 200, 50)

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

   btn_width, btn_height = int(WIDTH * 0.2), int(HEIGHT * 0.08)
   btn_start_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.45), btn_width, btn_height)
   btn_setting_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.58), btn_width, btn_height)
   btn_exit_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.71), btn_width, btn_height)

   running = True
   while running:
      mouse_pos = pygame.mouse.get_pos()
      
      for event in pygame.event.get():
         if event.type == pygame.QUIT:
            running = False
         elif event.type == pygame.KEYDOWN:
            if event.key == pygame.K_ESCAPE:
               if UI_state == "GAME_PLAY":
                  UI_state = "MAIN_MENU"
               else:
                  running = False
         
         elif event.type == pygame.MOUSEBUTTONDOWN and UI_state == "MAIN_MENU":
            if event.button == 1:
               if btn_start_rect.collidepoint(mouse_pos):
                  p1_score, p2_score = 0, 0
                  ball_x, ball_y = WIDTH // 2, HEIGHT // 2
                  p1_x = int(WIDTH * 0.04)
                  p2_x = WIDTH - int(WIDTH * 0.04) - paddle_width
                  UI_state = "GAME_PLAY"
               elif btn_setting_rect.collidepoint(mouse_pos):
                  print("[설정] 버튼이 탭되었습니다.")
               elif btn_exit_rect.collidepoint(mouse_pos):
                  running = False

      if not running:
         break

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

      elif UI_state == "GAME_PLAY":
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

         # 겹치는 깊이(Overlap)를 측정하여 가장 얕게 겹친 면을 충돌면으로 판정
         # --- Player 1 충돌 판정 ---
         if ball_rect.colliderect(p1_rect):
            overlap_left = ball_rect.right - p1_rect.left
            overlap_right = p1_rect.right - ball_rect.left
            overlap_top = ball_rect.bottom - p1_rect.top
            overlap_bottom = p1_rect.bottom - ball_rect.top

            min_overlap = min(overlap_left, overlap_right, overlap_top, overlap_bottom)

            if min_overlap == overlap_left: # 패들의 왼쪽 면 충돌 (앞면)
               ball_speed_x = -abs(ball_speed_x)
               ball_x = p1_rect.left - ball_size
            elif min_overlap == overlap_right: # 패들의 오른쪽 면 충돌 (뒷면)
               ball_speed_x = abs(ball_speed_x)
               ball_x = p1_rect.right
            elif min_overlap == overlap_top: # 패들의 위쪽 면 충돌 (옆면)
               ball_speed_y = -abs(ball_speed_y)
               ball_y = p1_rect.top - ball_size
            elif min_overlap == overlap_bottom: # 패들의 아래쪽 면 충돌 (옆면)
               ball_speed_y = abs(ball_speed_y)
               ball_y = p1_rect.bottom

         # --- Player 2 충돌 판정 ---
         if ball_rect.colliderect(p2_rect):
            overlap_left = ball_rect.right - p2_rect.left
            overlap_right = p2_rect.right - ball_rect.left
            overlap_top = ball_rect.bottom - p2_rect.top
            overlap_bottom = p2_rect.bottom - ball_rect.top

            min_overlap = min(overlap_left, overlap_right, overlap_top, overlap_bottom)

            if min_overlap == overlap_left: # 패들의 왼쪽 면 충돌 (뒷면)
               ball_speed_x = -abs(ball_speed_x)
               ball_x = p2_rect.left - ball_size
            elif min_overlap == overlap_right: # 패들의 오른쪽 면 충돌 (앞면)
               ball_speed_x = abs(ball_speed_x)
               ball_x = p2_rect.right
            elif min_overlap == overlap_top: # 패들의 위쪽 면 충돌 (옆면)
               ball_speed_y = -abs(ball_speed_y)
               ball_y = p2_rect.top - ball_size
            elif min_overlap == overlap_bottom: # 패들의 아래쪽 면 충돌 (옆면)
               ball_speed_y = abs(ball_speed_y)
               ball_y = p2_rect.bottom

         # 데이터 동기화용 변수 업데이트
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

      pygame.display.flip()
      clock.tick(60)

   pygame.quit()
   sys.exit()

if __name__ == "__main__":
   run_game()