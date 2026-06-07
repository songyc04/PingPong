import pygame
import sys
import socket
import threading
import math
import random

# --- [색상값 정의 - 사이버펑크 네온 스타일로 변경] ---
WHITE = (255, 255, 255)
BLACK = (10, 10, 18)        # 깊은 우주 느낌의 다크 네이비 블렌딩
GRAY = (60, 65, 80)
DARK_BG = (18, 18, 28)

# 네온 컬러 스펙트럼 업그레이드
RED = (255, 45, 85)         # 네온 핑크 레드
BLUE = (0, 240, 255)        # 사이안 블루
GREEN = (57, 255, 20)       # 네온 그린
YELLOW = (255, 230, 0)      # 네온 옐로우
ORANGE = (255, 110, 0)      # 네온 오렌지
PURPLE = (186, 12, 247)     # 일렉트릭 퍼플

COLOR_OPTIONS = [RED, BLUE, GREEN, YELLOW, ORANGE, PURPLE, WHITE]
COLOR_NAMES = ["CYAN RED", "NEON BLUE", "LIGHT GREEN", "NEON YELLOW", "ORANGE", "PURPLE", "WHITE"]

# --- 전역 게임 상태 및 네트워크 데이터 정의 ---
UI_state = "MAIN_MENU"
network_command = ""
command_lock = threading.Lock()

# 조이스틱 입력값 전역 변수
p1_joy_x, p1_joy_y = 0.0, 0.0
p2_joy_x, p2_joy_y = 0.0, 0.0

# --- 설정 데이터 변수 ---
sound_enabled = True
p1_color_idx = 0
p2_color_idx = 1
game_time_limit = 30000
time_limit_idx = 1
TIME_LIMITS = [10000, 30000, 60000, 120000]

# --- 설정 화면 커서 및 팝업 상태 ---
current_setting_index = 0
popup_type = ""
popup_sub_index = 0
countdown_active = False

# --- 비주얼 효과 레이어용 전역 변수 ---
ball_trail = []  # 공 잔상 데이터 저장용 리스트
global_timer = 0  # 네온 깜빡임 등을 제어하기 위한 프레임 카운터

# --- [네트워크 분리 설정] ---
ESP32_IP = "192.168.0.207"      
ESP32_SEND_PORT = 10002         
PYTHON_RCV_PORT = 10001         

tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
last_joystick_ip = None

def parse_joystick(raw_msg):
    global p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y, countdown_active
    if countdown_active:
        return
    try:
        cleaned_msg = ''.join(c for c in raw_msg if c.isdigit() or c == ',' or c == '-')
        data = cleaned_msg.split(',')
        
        if len(data) >= 4:
            p1_x, p1_y, p2_x, p2_y = int(data[0]), int(data[1]), int(data[2]), int(data[3])
            p1_jx, p1_jy = 0.0, 0.0
            p2_jx, p2_jy = 0.0, 0.0

            p1_center_min, p1_center_max = 1800, 2300
            if p1_center_min <= p1_x <= p1_center_max: p1_jx = 0.0
            elif p1_x < p1_center_min:                 p1_jx = (p1_x - p1_center_min) / float(p1_center_min)
            else:                                      p1_jx = (p1_x - p1_center_max) / float(4095.0 - p1_center_max)
                
            if p1_center_min <= p1_y <= p1_center_max: p1_jy = 0.0
            elif p1_y < p1_center_min:                 p1_jy = (p1_y - p1_center_min) / float(p1_center_min)
            else:                                      p1_jy = (p1_y - p1_center_max) / float(4095.0 - p1_center_max)

            p2_center_min, p2_center_max = 1800, 2300
            if p2_center_min <= p2_x <= p2_center_max: p2_jx = 0.0
            elif p2_x < p2_center_min:                 p2_jx = (p2_x - p2_center_min) / float(p2_center_min)
            else:                                      p2_jx = (p2_x - p2_center_max) / float(4095.0 - p2_center_max)
                
            if p2_center_min <= p2_y <= p2_center_max: p2_jy = 0.0
            elif p2_y < p2_center_min:                 p2_jy = (p2_y - p2_center_min) / float(p2_center_min)
            else:                                      p2_jy = (p2_y - p2_center_max) / float(4095.0 - p2_center_max)

            p1_joy_x, p1_joy_y = max(-1.0, min(1.0, p1_jx)), max(-1.0, min(1.0, p1_jy))
            p2_joy_x, p2_joy_y = max(-1.0, min(1.0, p2_jx)), max(-1.0, min(1.0, p2_jy))
                
        elif len(data) == 2:
            p1_x, p1_y = int(data[0]), int(data[1])
            p1_jx, p1_jy = 0.0, 0.0
            if 1800 <= p1_x <= 2300:   p1_jx = 0.0
            elif p1_x < 1800:          p1_jx = (p1_x - 1800) / 1800.0
            else:                      p1_jx = (p1_x - 2300) / (4095.0 - 2300.0)
                
            if 1800 <= p1_y <= 2300:   p1_jy = 0.0
            elif p1_y < 1800:          p1_jy = (p1_y - 1800) / 1800.0
            else:                      p1_jy = (p1_y - 2300) / (4095.0 - 2300.0)
            
            p1_joy_x, p1_joy_y = max(-1.0, min(1.0, p1_jx)), max(-1.0, min(1.0, p1_jy))
    except Exception as e:
        print(f"[파서 에러] 데이터 변환 에러 발생: {e} | 원문: {raw_msg}")

def send_to_esp32(message):
    try:
        tx_socket.sendto(message.encode('utf-8'), (ESP32_IP, ESP32_SEND_PORT))
        print(f"[네트워크 송신 -> ESP32]: {message}")
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
            if not data: continue
            raw_msg = data.decode("utf-8").strip()
            last_joystick_ip = addr[0]
            
            if ',' in raw_msg and (raw_msg[0].isdigit() or raw_msg[0] == '-'):
                with command_lock:
                    parse_joystick(raw_msg)
            else:
                print(f"[네트워크 수신 Command]: {raw_msg}")
                with command_lock:
                    network_command = raw_msg
        except Exception as e:
            print(f"[네트워크] UDP 수신 오류: {e}")
            break

# --- 고급 그래픽 연출을 위한 특수 헬퍼 함수 ---
def draw_neon_circle(surface, color, center, radius, thickness=3, glow_layers=3):
    """도형 주위에 은은하게 퍼지는 멀티 레이어 네온 글로우 효과 렌더링"""
    for i in range(glow_layers, 0, -1):
        alpha = int(55 / i)
        glow_radius = radius + (i * 3)
        glow_surf = pygame.Surface((glow_radius * 2 + 10, glow_radius * 2 + 10), pygame.SRCALPHA)
        pygame.draw.circle(glow_surf, (*color, alpha), (glow_radius + 5, glow_radius + 5), glow_radius, thickness + i * 2)
        surface.blit(glow_surf, (center[0] - glow_radius - 5, center[1] - glow_radius - 5), special_flags=pygame.BLEND_RGBA_ADD)
    pygame.draw.circle(surface, WHITE if thickness > 1 else color, center, radius, thickness)

def draw_neon_rect(surface, color, rect, thickness=3, glow_layers=3, border_radius=0):
    """사각형 격자 주위에 사이버펑크 네온 윤곽선 레이어를 생성"""
    inflated_rect = rect.inflate(20, 20)
    glow_surf = pygame.Surface((inflated_rect.width, inflated_rect.height), pygame.SRCALPHA)
    for i in range(glow_layers, 0, -1):
        alpha = int(45 / i)
        r_glow = rect.inflate(i * 4, i * 4)
        r_glow.topleft = (r_glow.left - inflated_rect.left, r_glow.top - inflated_rect.top)
        pygame.draw.rect(glow_surf, (*color, alpha), r_glow, thickness + i * 2, border_radius=border_radius)
    surface.blit(glow_surf, inflated_rect.topleft, special_flags=pygame.BLEND_RGBA_ADD)
    pygame.draw.rect(surface, color, rect, thickness, border_radius=border_radius)

def draw_scanlines(surface, width, height):
    """오락실 스캔라인 분위기를 재현해 화면에 투사"""
    for y in range(0, height, 4):
        line_surf = pygame.Surface((width, 1), pygame.SRCALPHA)
        line_surf.fill((0, 0, 0, 35))
        surface.blit(line_surf, (0, y))

def run_game():
    global UI_state, network_command, current_setting_index, popup_type, popup_sub_index
    global sound_enabled, p1_color_idx, p2_color_idx
    global p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y
    global game_time_limit, time_limit_idx, countdown_active, ball_trail, global_timer
    
    pygame.init()
    info = pygame.display.Info()
    WIDTH, HEIGHT = info.current_w, info.current_h
    
    screen = pygame.display.set_mode((WIDTH, HEIGHT), pygame.NOFRAME | pygame.HWSURFACE | pygame.DOUBLEBUF)
    pygame.display.set_caption("Cyberpunk Hockey Pong - Hyper UDP")
    clock = pygame.time.Clock()

    # 인게임 고정 수치 기하학 정의
    ball_size = int(HEIGHT * 0.03)
    ball_radius = ball_size // 2
    p1_radius = int(ball_radius * 2.6)
    p2_radius = int(ball_radius * 2.6)
    goal_width = int(WIDTH * 0.01)
    goal_height = int(HEIGHT * 0.35)
    
    paddle_speed = int(HEIGHT * 0.022)
    base_ball_speed_x = int(WIDTH * 0.009)
    base_ball_speed_y = int(HEIGHT * 0.009)
    
    p1_cx, p1_cy = int(WIDTH * 0.08), HEIGHT // 2
    p2_cx, p2_cy = WIDTH - int(WIDTH * 0.08), HEIGHT // 2
    center_line_x = WIDTH // 2
    
    p1_goal = pygame.Rect(0, HEIGHT // 2 - goal_height // 2, goal_width, goal_height)
    p2_goal = pygame.Rect(WIDTH - goal_width, HEIGHT // 2 - goal_height // 2, goal_width, goal_height)
    
    ball_x, ball_y = WIDTH // 2 - ball_size // 2, HEIGHT // 2 - ball_size // 2
    ball_speed_x, ball_speed_y = base_ball_speed_x, base_ball_speed_y
    ball_active = False
    
    p1_score, p2_score = 0, 0
    
    # 폰트 구성
    try:
        title_font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.09))
        font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.05))
        btn_font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.032))
        countdown_font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.22))
    except:
        title_font = pygame.font.SysFont("impact", int(HEIGHT * 0.09))
        font = pygame.font.SysFont("malgungothic", int(HEIGHT * 0.05), bold=True)
        btn_font = pygame.font.SysFont("malgungothic", int(HEIGHT * 0.032), bold=True)
        countdown_font = pygame.font.SysFont("impact", int(HEIGHT * 0.22))

    # 메뉴 UI 배치 레이아웃 정의
    btn_width, btn_height = int(WIDTH * 0.25), int(HEIGHT * 0.075)
    btn_start_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.46), btn_width, btn_height)
    btn_setting_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.58), btn_width, btn_height)
    btn_exit_rect = pygame.Rect(WIDTH // 2 - btn_width // 2, int(HEIGHT * 0.70), btn_width, btn_height)

    setting_box_width, setting_box_height = int(WIDTH * 0.65), int(HEIGHT * 0.52)
    setting_box_rect = pygame.Rect(WIDTH // 2 - setting_box_width // 2, int(HEIGHT * 0.28), setting_box_width, setting_box_height)

    btn_back_width, btn_back_height = int(WIDTH * 0.16), int(HEIGHT * 0.055)
    btn_back_rect = pygame.Rect(WIDTH // 2 - btn_back_width // 2, int(HEIGHT * 0.73), btn_back_width, btn_back_height)

    setting_y_positions = [int(HEIGHT * 0.33), int(HEIGHT * 0.42), int(HEIGHT * 0.51), int(HEIGHT * 0.60), int(HEIGHT * 0.73)]

    game_elapsed_time = 0.0
    game_timer_active = False
    countdown_timer = 0

    running = True
    while running:
        dt = clock.tick(60)
        global_timer += 1
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
                            ball_x, ball_y = WIDTH // 2 - ball_size // 2, HEIGHT // 2 - ball_size // 2
                            ball_active = False
                            p1_cx, p1_cy = int(WIDTH * 0.08), HEIGHT // 2
                            p2_cx, p2_cy = WIDTH - int(WIDTH * 0.08), HEIGHT // 2
                            p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y = 0.0, 0.0, 0.0, 0.0
                            UI_state = "GAME_PLAY"
                            game_elapsed_time = 0.0
                            game_timer_active = False
                            countdown_timer = 3000
                            countdown_active = True
                            ball_trail.clear()
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
                    ball_x, ball_y = WIDTH // 2 - ball_size // 2, HEIGHT // 2 - ball_size // 2
                    ball_active = False
                    p1_cx, p1_cy = int(WIDTH * 0.08), HEIGHT // 2
                    p2_cx, p2_cy = WIDTH - int(WIDTH * 0.08), HEIGHT // 2
                    p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y = 0.0, 0.0, 0.0, 0.0
                    UI_state = "GAME_PLAY"
                    game_elapsed_time = 0.0
                    game_timer_active = False
                    countdown_timer = 3000
                    countdown_active = True
                    ball_trail.clear()
                    send_to_esp32("SRT")
            
            elif current_cmd == "STP":
                if UI_state == "GAME_PLAY":
                    UI_state = "PAUSE"
                    game_timer_active = False
                    countdown_active = False
                    send_to_esp32("STP")
                    
            elif current_cmd == "END":
                result_msg = f"PLAYER1,{p1_score}" if p1_score > p2_score else (f"PLAYER2,{p2_score}" if p2_score > p1_score else f"DRAW,{p1_score}")
                send_to_esp32(result_msg)
                UI_state = "MAIN_MENU"
                popup_type = ""
                game_elapsed_time = 0.0
                game_timer_active = False
                countdown_active = False
                
            elif current_cmd == "SET":
                if UI_state != "SETTINGS":
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

        # --- 패들 실시간 역학 처리 및 가두기 ---
        if UI_state == "GAME_PLAY":
            keys = pygame.key.get_pressed()
            
            # P1 물리 조작
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
                
            # P2 물리 조작
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

            # 공 이동 및 실시간 네온 트레일 스냅샷 수집
            if ball_active and not countdown_active:
                ball_x += ball_speed_x
                ball_y += ball_speed_y
                ball_trail.append((ball_x + ball_radius, ball_y + ball_radius))
                if len(ball_trail) > 12:  # 트레일 최대 길이 확보
                    ball_trail.pop(0)
            else:
                if len(ball_trail) > 0: ball_trail.pop(0)

            if ball_y <= 0:
                ball_speed_y = abs(ball_speed_y)
                ball_y = 0
            elif ball_y >= HEIGHT - ball_size:
                ball_speed_y = -abs(ball_speed_y)
                ball_y = HEIGHT - ball_size

            ball_cx, ball_cy = ball_x + ball_radius, ball_y + ball_radius

            # P1 탄성 구형 충돌
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

            # P2 탄성 구형 충돌
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

            # 골인 판정 및 서브 세팅
            if ball_rect.colliderect(p1_goal) and not countdown_active:
                p2_score += 1
                ball_x, ball_y = int(WIDTH * 0.25) - ball_size // 2, HEIGHT // 2 - ball_size // 2
                ball_speed_x, ball_speed_y = base_ball_speed_x, base_ball_speed_y
                ball_active = False
                ball_trail.clear()
            elif ball_rect.colliderect(p2_goal) and not countdown_active:
                p1_score += 1
                ball_x, ball_y = int(WIDTH * 0.75) - ball_size // 2, HEIGHT // 2 - ball_size // 2
                ball_speed_x, ball_speed_y = -base_ball_speed_x, base_ball_speed_y
                ball_active = False
                ball_trail.clear()
            else:
                if ball_x <= 0:
                    ball_speed_x = abs(ball_speed_x)
                    ball_x = 0
                elif ball_x >= WIDTH - ball_size:
                    ball_speed_x = -abs(ball_speed_x)
                    ball_x = WIDTH - ball_size
        else:
            ball_rect = pygame.Rect(ball_x, ball_y, ball_size, ball_size)

        # ==========================================
        # --- [고급 그래픽 엔진 프레임 렌더링 파트] ---
        # ==========================================
        screen.fill(BLACK)
        
        # 경기장 격자 및 중앙선 네온 처리
        pygame.draw.rect(screen, DARK_BG, (0, 0, WIDTH, HEIGHT))
        grid_color = (30, 35, 50)
        pygame.draw.line(screen, grid_color, (center_line_x, 0), (center_line_x, HEIGHT), 2)
        pygame.draw.circle(screen, grid_color, (WIDTH // 2, HEIGHT // 2), int(HEIGHT * 0.2), 2)

        # 골 포스트 사이버펑크 글로우 스타일 렌더링
        draw_neon_rect(screen, COLOR_OPTIONS[p1_color_idx], p1_goal, thickness=4)
        draw_neon_rect(screen, COLOR_OPTIONS[p2_color_idx], p2_goal, thickness=4)
        
        # 공 이동 궤적 잔상(Trail) 레이어 그리기
        for i, pos in enumerate(ball_trail):
            ratio = (i + 1) / len(ball_trail)
            t_radius = int(ball_radius * ratio * 0.9)
            alpha_color = (255, 255, 255, int(160 * ratio))
            trail_surf = pygame.Surface((t_radius * 2, t_radius * 2), pygame.SRCALPHA)
            pygame.draw.circle(trail_surf, alpha_color, (t_radius, t_radius), t_radius)
            screen.blit(trail_surf, (pos[0] - t_radius, pos[1] - t_radius), special_flags=pygame.BLEND_RGBA_ADD)

        # 인게임 활성화 상태 패들 그리기
        if UI_state in ["GAME_PLAY", "PAUSE"]:
            draw_neon_circle(screen, COLOR_OPTIONS[p1_color_idx], (p1_cx, p1_cy), p1_radius, thickness=4)
            draw_neon_circle(screen, COLOR_OPTIONS[p1_color_idx], (p1_cx, p1_cy), int(p1_radius*0.4), thickness=2)
            
            draw_neon_circle(screen, COLOR_OPTIONS[p2_color_idx], (p2_cx, p2_cy), p2_radius, thickness=4)
            draw_neon_circle(screen, COLOR_OPTIONS[p2_color_idx], (p2_cx, p2_cy), int(p2_radius*0.4), thickness=2)
        
        # 메인 오브젝트 공(Ball) 렌더링
        draw_neon_circle(screen, WHITE, (int(ball_x + ball_radius), int(ball_y + ball_radius)), ball_radius, thickness=2, glow_layers=4)
        
        # --- [UI 디스플레이 상태별 렌더링 세부 구현] ---
        if UI_state == "MAIN_MENU":
            # 메인 타이틀 네온 깜빡임(Flicker) 애니메이션 구현
            flicker = random.choice([True, True, True, False]) if global_timer % 40 == 0 else True
            t_color = YELLOW if flicker else (100, 90, 20)
            title_text = title_font.render("AIR HOCKEY PONG", True, t_color)
            screen.blit(title_text, (WIDTH // 2 - title_text.get_width() // 2, int(HEIGHT * 0.15)))
            
            # 메뉴 버튼 그라데이션 및 반응형 호버링 렌더링
            for rect, text, color in [(btn_start_rect, "START GAME", GREEN), (btn_setting_rect, "SETTINGS", GRAY), (btn_exit_rect, "EXIT", RED)]:
                is_hover = rect.collidepoint(mouse_pos)
                target_rect = rect.inflate(10, 6) if is_hover else rect
                draw_color = [min(c + 50, 255) if is_hover else c for c in color]
                
                # 버튼 백그라운드 입체감을 위한 반투명 아웃라인
                pygame.draw.rect(screen, (20, 20, 30), target_rect, border_radius=12)
                draw_neon_rect(screen, draw_color, target_rect, thickness=3, glow_layers=3 if is_hover else 1, border_radius=12)
                
                txt_surf = btn_font.render(text, True, WHITE if not is_hover else DRAW_COLOR if 'draw_color' in locals() else draw_color)
                screen.blit(txt_surf, (target_rect.centerx - txt_surf.get_width() // 2, target_rect.centery - txt_surf.get_height() // 2))

        elif UI_state in ["GAME_PLAY", "PAUSE"]:
            # 스코어 보드 헤드업 디자인 (HUD) 변경
            score_text = font.render(f"P1  {p1_score}   |   {p2_score}  P2", True, WHITE)
            screen.blit(score_text, (WIDTH // 2 - score_text.get_width() // 2, int(HEIGHT * 0.04)))
            
            elapsed_sec = game_elapsed_time / 1000.0
            limit_sec = game_time_limit / 1000.0
            time_text = btn_font.render(f"TIME: {elapsed_sec:.1f} / {limit_sec:.0f}s", True, PURPLE)
            screen.blit(time_text, (WIDTH // 2 - time_text.get_width() // 2, int(HEIGHT * 0.11)))

            # 매치 카운트다운 맥박 치는 스케일 업 애니메이션 효과
            if UI_state == "GAME_PLAY" and countdown_active:
                count_val = math.ceil(countdown_timer / 1000.0)
                if count_val > 0:
                    pulse_scale = 1.0 + (countdown_timer % 1000) / 1000.0 * 0.25
                    c_font_size = int(HEIGHT * 0.22 * pulse_scale)
                    try:
                        dynamic_countdown_font = pygame.font.Font("Moneygraphy-Rounded.otf", c_font_size)
                    except:
                        dynamic_countdown_font = pygame.font.SysFont("impact", c_font_size)
                    count_text = dynamic_countdown_font.render(str(count_val), True, YELLOW)
                    screen.blit(count_text, (WIDTH // 2 - count_text.get_width() // 2, HEIGHT // 2 - count_text.get_height() // 2))

            if UI_state == "GAME_PLAY" and not ball_active and not countdown_active:
                sub_text = btn_font.render("Hit the ball to serve!", True, GREEN)
                screen.blit(sub_text, (WIDTH // 2 - sub_text.get_width() // 2, int(HEIGHT * 0.18)))
            if UI_state == "PAUSE":
                pause_text = title_font.render("PAUSED", True, GRAY)
                screen.blit(pause_text, (WIDTH // 2 - pause_text.get_width() // 2, HEIGHT // 2 - pause_text.get_height() // 2))

        elif UI_state == "SETTINGS":
            set_title_text = title_font.render("SETTINGS", True, WHITE)
            screen.blit(set_title_text, (WIDTH // 2 - set_title_text.get_width() // 2, int(HEIGHT * 0.15)))
            draw_neon_rect(screen, BLUE, setting_box_rect, thickness=2, border_radius=16)
            
            labels = ["1. SOUND SETTINGS", "2. COLOR CUSTOM", "3. GAME CUSTOM", "4. CREATOR CREDITS"]
            for i, label_text in enumerate(labels):
                is_selected = (current_setting_index == i and not popup_type)
                color_preset = BLUE if is_selected else WHITE
                lbl_surf = font.render(label_text, True, color_preset)
                # 선택 시 오른쪽으로 살짝 밀려나는 패딩 인덴트 효과 적용
                x_offset = int(WIDTH * 0.06) if is_selected else int(WIDTH * 0.05)
                screen.blit(lbl_surf, (setting_box_rect.left + x_offset, setting_y_positions[i]))
            
            is_back_hover = btn_back_rect.collidepoint(mouse_pos) or (current_setting_index == 4 and not popup_type)
            back_color = BLUE if is_back_hover else GRAY
            draw_neon_rect(screen, back_color, btn_back_rect, thickness=2, border_radius=8)
            back_txt = btn_font.render("BACK", True, WHITE)
            screen.blit(back_txt, (btn_back_rect.centerx - back_txt.get_width() // 2, btn_back_rect.centery - back_txt.get_height() // 2))

            if not popup_type:
                cursor_text = font.render(">", True, BLUE)
                cursor_x = btn_back_rect.left - int(WIDTH * 0.02) if current_setting_index == 4 else setting_box_rect.left + int(WIDTH * 0.02)
                cursor_y = setting_y_positions[4] + (btn_back_rect.height // 2) - (cursor_text.get_height() // 2) if current_setting_index == 4 else setting_y_positions[current_setting_index]
                screen.blit(cursor_text, (cursor_x, cursor_y))

            # --- 설정 팝업 창 그래픽 리디자인 ---
            if popup_type:
                popup_w, popup_h = int(WIDTH * 0.48), int(HEIGHT * 0.38)
                popup_rect = pygame.Rect(WIDTH // 2 - popup_w // 2, HEIGHT // 2 - popup_h // 2, popup_w, popup_h)
                
                # 내부 불투명 필터 추가해 가독성 확보
                pop_bg_surf = pygame.Surface((popup_w, popup_h))
                pop_bg_surf.fill((15, 15, 25))
                screen.blit(pop_bg_surf, popup_rect.topleft)
                draw_neon_rect(screen, PURPLE, popup_rect, thickness=4, glow_layers=4, border_radius=14)
                
                if popup_type == "SOUND":
                    p_title = font.render(" [ SOUND CONFIG ] ", True, PURPLE)
                    screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.03)))
                    for idx, text in enumerate(["SOUND: ON", "SOUND: OFF"]):
                        target_rect = pygame.Rect(popup_rect.left + int(popup_w * (0.12 if idx==0 else 0.54)), popup_rect.top + int(HEIGHT * 0.15), int(popup_w * 0.34), int(HEIGHT * 0.06))
                        box_color = GREEN if popup_sub_index == idx else GRAY
                        draw_neon_rect(screen, box_color, target_rect, thickness=2, border_radius=6)
                        txt_s = btn_font.render(text, True, WHITE)
                        screen.blit(txt_s, (target_rect.centerx - txt_s.get_width() // 2, target_rect.centery - txt_s.get_height() // 2))
                
                elif popup_type == "COLOR":
                    p_title = font.render(" [ PADDLE COLOR CUSTOM ] ", True, PURPLE)
                    screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.02)))
                    options_text = [f"P1 COLOR : < {COLOR_NAMES[p1_color_idx]} >", f"P2 COLOR : < {COLOR_NAMES[p2_color_idx]} >", "[ SAVE & EXIT ]", "[ CANCEL ]"]
                    for idx, text in enumerate(options_text):
                        current_y = popup_rect.top + int(HEIGHT * 0.09) + (idx * int(HEIGHT * 0.055))
                        if idx == 0: pygame.draw.circle(screen, COLOR_OPTIONS[p1_color_idx], (popup_rect.right - int(popup_w * 0.15), current_y + 12), 12)
                        elif idx == 1: pygame.draw.circle(screen, COLOR_OPTIONS[p2_color_idx], (popup_rect.right - int(popup_w * 0.15), current_y + 12), 12)
                        txt_s = btn_font.render(text, True, PURPLE if popup_sub_index == idx else WHITE)
                        screen.blit(txt_s, (popup_rect.left + int(popup_w * 0.08), current_y))
                
                elif popup_type == "GAME_CUSTOM":
                    p_title = font.render(" [ GAME CUSTOM ] ", True, PURPLE)
                    screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.03)))
                    options_text = ["1. SETTING TIMES", "[ BACK ]"]
                    for idx, text in enumerate(options_text):
                        current_y = popup_rect.top + int(HEIGHT * 0.13) + (idx * int(HEIGHT * 0.08))
                        txt_s = btn_font.render(text, True, PURPLE if popup_sub_index == idx else WHITE)
                        screen.blit(txt_s, (popup_rect.centerx - txt_s.get_width() // 2, current_y))
                
                elif popup_type == "SETTING_TIMES":
                    p_title = font.render(" [ SETTING TIMES ] ", True, PURPLE)
                    screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.02)))
                    options_text = ["10 SECONDS", "30 SECONDS", "1 MINUTE", "2 MINUTES", "[ SAVE & EXIT ]"]
                    for idx, text in enumerate(options_text):
                        current_y = popup_rect.top + int(HEIGHT * 0.08) + (idx * int(HEIGHT * 0.05))
                        marker = ">> " if idx == time_limit_idx else "  "
                        color_val = GREEN if idx == time_limit_idx else (PURPLE if popup_sub_index == idx else WHITE)
                        txt_s = btn_font.render(marker + text, True, color_val)
                        screen.blit(txt_s, (popup_rect.left + int(popup_w * 0.1), current_y))
                
                elif popup_type == "CREATOR":
                    p_title = font.render(" [ TEAM CREDITS ] ", True, PURPLE)
                    screen.blit(p_title, (popup_rect.centerx - p_title.get_width() // 2, popup_rect.top + int(HEIGHT * 0.03)))
                    for idx, line in enumerate(["DEVELOPER: Team Hockey Pong", "HARDWARE: ESP32 Wi-Fi UDP", "GRAPHICS: Pygame Neon Extended"]):
                        txt_s = btn_font.render(line, True, WHITE)
                        screen.blit(txt_s, (popup_rect.centerx - txt_s.get_width() // 2, popup_rect.top + int(HEIGHT * 0.13) + (idx * 35)))

        # 아케이드 전용 최상단 스캔라인 레이어 오버랩 투사
        draw_scanlines(screen, WIDTH, HEIGHT)
        pygame.display.flip()

    tx_socket.close()
    pygame.quit()
    sys.exit()

if __name__ == "__main__":
    net_thread = threading.Thread(target=udp_server_thread, daemon=True)
    net_thread.start()
    run_game()