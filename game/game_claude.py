import pygame
import sys
import socket
import threading
import math
import random
import time

# ============================================================
#  색상 팔레트 — 사이버펑크 네온 테마
# ============================================================
BG_DEEP       = (4,   4,  12)       # 거의 검정인 진한 남색
BG_MID        = (8,  10,  24)
NEON_CYAN     = (0,  240, 255)
NEON_MAGENTA  = (255,  0, 180)
NEON_YELLOW   = (255, 230,  0)
NEON_GREEN    = (0,  255, 120)
NEON_ORANGE   = (255, 120,  0)
NEON_PURPLE   = (160,  0, 255)
NEON_WHITE    = (220, 235, 255)
DIM_GRAY      = (50,  55,  80)
GLASS         = (20,  25,  55)
GLASS_BORDER  = (60,  70, 140)

WHITE  = (255, 255, 255)
BLACK  = (0,   0,   0)
RED    = NEON_MAGENTA
BLUE   = NEON_CYAN
GREEN  = NEON_GREEN
YELLOW = NEON_YELLOW
GRAY   = DIM_GRAY
ORANGE = NEON_ORANGE
PURPLE = NEON_PURPLE

COLOR_OPTIONS = [NEON_CYAN, NEON_MAGENTA, NEON_GREEN, NEON_YELLOW, NEON_ORANGE, NEON_PURPLE, NEON_WHITE]
COLOR_NAMES   = ["CYAN",    "MAGENTA",    "GREEN",    "YELLOW",    "ORANGE",    "PURPLE",    "WHITE"]

# ============================================================
#  전역 게임 / 네트워크 상태
# ============================================================
UI_state        = "MAIN_MENU"
network_command = ""
command_lock    = threading.Lock()

p1_joy_x, p1_joy_y = 0.0, 0.0
p2_joy_x, p2_joy_y = 0.0, 0.0

sound_enabled   = True
p1_color_idx    = 0
p2_color_idx    = 1
game_time_limit = 30000
time_limit_idx  = 1
TIME_LIMITS     = [10000, 30000, 60000, 120000]

current_setting_index = 0
popup_type            = ""
popup_sub_index       = 0
countdown_active      = False

# ============================================================
#  네트워크 설정
# ============================================================
ESP32_IP        = "192.168.0.207"
ESP32_SEND_PORT = 10002
PYTHON_RCV_PORT = 10001

tx_socket       = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
last_joystick_ip = None


def send_to_esp32(message):
    global last_joystick_ip
    try:
        tx_socket.sendto(message.encode('utf-8'), (ESP32_IP, ESP32_SEND_PORT))
        if last_joystick_ip:
            tx_socket.sendto(message.encode('utf-8'), (last_joystick_ip, PYTHON_RCV_PORT))
    except Exception as e:
        print(f"[네트워크] ESP32 송신 오류: {e}")


def parse_joystick(raw_msg):
    global p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y
    try:
        data = raw_msg.split(',')
        def norm(v):
            v = int(v)
            if 2630 <= v <= 2750: return 0.0
            elif v < 2630:        return (v - 2630) / 2630.0
            else:                 return (v - 2750) / (4095.0 - 2750.0)

        if len(data) == 2:
            p1_jx, p1_jy = norm(data[0]), norm(data[1])
            print(f"[UDP 수신(2P)] P1: ({p1_jx:+.2f}, {p1_jy:+.2f}) | Raw: {raw_msg}")
            p1_joy_x, p1_joy_y = p1_jx, p1_jy
        elif len(data) >= 4:
            p1_jx, p1_jy = norm(data[0]), norm(data[1])
            p2_jx, p2_jy = norm(data[2]), norm(data[3])
            print(f"[UDP 수신(4P)] P1: ({p1_jx:+.2f}, {p1_jy:+.2f}) | P2: ({p2_jx:+.2f}, {p2_jy:+.2f}) | Raw: {raw_msg}")
            p1_joy_x, p1_joy_y = p1_jx, p1_jy
            p2_joy_x, p2_joy_y = p2_jx, p2_jy
    except Exception:
        pass


def udp_server_thread():
    global network_command, last_joystick_ip, countdown_active, UI_state
    rx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        rx_socket.bind(("0.0.0.0", PYTHON_RCV_PORT))
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
            if ',' in raw_msg:
                if UI_state == "GAME_PLAY" and countdown_active: continue
                with command_lock: parse_joystick(raw_msg)
            else:
                print(f"Command: {raw_msg}")
                with command_lock: network_command = raw_msg
        except Exception as e:
            print(f"[네트워크] UDP 수신 오류: {e}")
            break


# ============================================================
#  그래픽 유틸리티
# ============================================================

def draw_neon_circle(surf, color, cx, cy, radius, width=2, glow_radius=None, glow_alpha=60):
    """네온 글로우 효과가 있는 원 그리기"""
    if glow_radius is None:
        glow_radius = radius + 8
    # 글로우 레이어 (반투명 서피스)
    glow_surf = pygame.Surface((glow_radius * 2 + 4, glow_radius * 2 + 4), pygame.SRCALPHA)
    glow_color = (*color, glow_alpha)
    pygame.draw.circle(glow_surf, glow_color, (glow_radius + 2, glow_radius + 2), glow_radius)
    surf.blit(glow_surf, (cx - glow_radius - 2, cy - glow_radius - 2))
    pygame.draw.circle(surf, color, (cx, cy), radius, width)


def draw_neon_line(surf, color, start, end, width=2, glow=True):
    if glow:
        glow_color = (*color, 60)
        glow_surf  = pygame.Surface(surf.get_size(), pygame.SRCALPHA)
        pygame.draw.line(glow_surf, glow_color, start, end, width + 6)
        surf.blit(glow_surf, (0, 0))
    pygame.draw.line(surf, color, start, end, width)


def draw_neon_rect(surf, color, rect, radius=12, width=2, glow=True):
    if glow:
        gs = pygame.Surface((rect.width + 20, rect.height + 20), pygame.SRCALPHA)
        gc = (*color, 50)
        pygame.draw.rect(gs, gc, (0, 0, rect.width + 20, rect.height + 20), border_radius=radius + 6)
        surf.blit(gs, (rect.x - 10, rect.y - 10))
    pygame.draw.rect(surf, color, rect, width, border_radius=radius)


def draw_filled_neon_rect(surf, color, rect, radius=12, glow=True):
    """채워진 네온 버튼"""
    if glow:
        gs = pygame.Surface((rect.width + 16, rect.height + 16), pygame.SRCALPHA)
        gc = (*color, 40)
        pygame.draw.rect(gs, gc, (0, 0, rect.width + 16, rect.height + 16), border_radius=radius + 4)
        surf.blit(gs, (rect.x - 8, rect.y - 8))
    # 유리 배경
    glass_surf = pygame.Surface((rect.width, rect.height), pygame.SRCALPHA)
    pygame.draw.rect(glass_surf, (*GLASS, 200), (0, 0, rect.width, rect.height), border_radius=radius)
    surf.blit(glass_surf, (rect.topleft))
    # 테두리
    pygame.draw.rect(surf, color, rect, 2, border_radius=radius)


def draw_scanlines(surf, alpha=18):
    """CRT 스캔라인 효과"""
    h = surf.get_height()
    w = surf.get_width()
    sl = pygame.Surface((w, 2), pygame.SRCALPHA)
    sl.fill((0, 0, 0, alpha))
    for y in range(0, h, 4):
        surf.blit(sl, (0, y))


def draw_grid(surf, color, spacing=60, alpha=25):
    """배경 그리드"""
    w, h = surf.get_size()
    grid_surf = pygame.Surface((w, h), pygame.SRCALPHA)
    c = (*color, alpha)
    for x in range(0, w, spacing):
        pygame.draw.line(grid_surf, c, (x, 0), (x, h), 1)
    for y in range(0, h, spacing):
        pygame.draw.line(grid_surf, c, (0, y), (w, y), 1)
    surf.blit(grid_surf, (0, 0))


def draw_corner_deco(surf, rect, color, size=18, width=3):
    """패널 모서리 장식"""
    x, y, w, h = rect.x, rect.y, rect.width, rect.height
    corners = [(x, y), (x + w, y), (x, y + h), (x + w, y + h)]
    dirs    = [(1, 1), (-1, 1), (1, -1), (-1, -1)]
    for (cx, cy), (dx, dy) in zip(corners, dirs):
        pygame.draw.line(surf, color, (cx, cy), (cx + dx * size, cy),             width)
        pygame.draw.line(surf, color, (cx, cy), (cx,             cy + dy * size), width)


class Particle:
    """골 득점 시 파티클 이펙트"""
    def __init__(self, x, y, color):
        angle  = random.uniform(0, math.pi * 2)
        speed  = random.uniform(2, 8)
        self.x = x
        self.y = y
        self.vx = math.cos(angle) * speed
        self.vy = math.sin(angle) * speed
        self.color  = color
        self.life   = random.randint(30, 60)
        self.maxlife = self.life
        self.size = random.randint(2, 6)

    def update(self):
        self.x  += self.vx
        self.y  += self.vy
        self.vy += 0.15
        self.life -= 1

    def draw(self, surf):
        if self.life <= 0: return
        alpha = int(255 * (self.life / self.maxlife))
        ps = pygame.Surface((self.size * 2, self.size * 2), pygame.SRCALPHA)
        pygame.draw.circle(ps, (*self.color, alpha), (self.size, self.size), self.size)
        surf.blit(ps, (int(self.x) - self.size, int(self.y) - self.size))


# ============================================================
#  메인 게임 루프
# ============================================================

def run_game():
    global UI_state, network_command, current_setting_index, popup_type, popup_sub_index
    global sound_enabled, p1_color_idx, p2_color_idx
    global p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y
    global game_time_limit, time_limit_idx, countdown_active

    pygame.init()
    info   = pygame.display.Info()
    WIDTH  = info.current_w
    HEIGHT = info.current_h

    screen = pygame.display.set_mode((WIDTH, HEIGHT),
                                     pygame.NOFRAME | pygame.HWSURFACE | pygame.DOUBLEBUF)
    pygame.display.set_caption("AIR HOCKEY PONG — CYBER EDITION")
    clock  = pygame.time.Clock()

    # ── 폰트 로드 ──────────────────────────────────────────
    try:
        font_title  = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.09))
        font_large  = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.055))
        font_mid    = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.038))
        font_small  = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.026))
        font_count  = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.20))
    except Exception:
        font_title  = pygame.font.SysFont("monospace", int(HEIGHT * 0.09), bold=True)
        font_large  = pygame.font.SysFont("monospace", int(HEIGHT * 0.055), bold=True)
        font_mid    = pygame.font.SysFont("monospace", int(HEIGHT * 0.038), bold=True)
        font_small  = pygame.font.SysFont("monospace", int(HEIGHT * 0.026))
        font_count  = pygame.font.SysFont("monospace", int(HEIGHT * 0.20), bold=True)

    # ── 게임 오브젝트 크기 ──────────────────────────────────
    ball_size          = int(HEIGHT * 0.032)
    ball_radius        = ball_size // 2
    p1_radius          = int(ball_radius * 2.7)
    p2_radius          = int(ball_radius * 2.7)
    goal_width         = int(WIDTH  * 0.009)
    goal_height        = int(HEIGHT * 0.30)
    paddle_speed       = int(HEIGHT * 0.020)
    base_ball_speed_x  = int(WIDTH  * 0.008)
    base_ball_speed_y  = int(HEIGHT * 0.008)

    # ── 초기 위치 ───────────────────────────────────────────
    p1_cx = int(WIDTH * 0.06);   p1_cy = HEIGHT // 2
    p2_cx = WIDTH - int(WIDTH * 0.06);  p2_cy = HEIGHT // 2
    center_line_x = WIDTH // 2

    p1_goal = pygame.Rect(0, HEIGHT // 2 - goal_height // 2, goal_width,          goal_height)
    p2_goal = pygame.Rect(WIDTH - goal_width, HEIGHT // 2 - goal_height // 2, goal_width, goal_height)

    ball_x, ball_y     = WIDTH // 2 - ball_size // 2, HEIGHT // 2 - ball_size // 2
    ball_speed_x       = base_ball_speed_x
    ball_speed_y       = base_ball_speed_y
    ball_active        = False

    p1_score = 0
    p2_score = 0

    # ── UI 레이아웃 ─────────────────────────────────────────
    BW = int(WIDTH * 0.26)
    BH = int(HEIGHT * 0.085)
    btn_start_rect   = pygame.Rect(WIDTH // 2 - BW // 2, int(HEIGHT * 0.43), BW, BH)
    btn_setting_rect = pygame.Rect(WIDTH // 2 - BW // 2, int(HEIGHT * 0.56), BW, BH)
    btn_exit_rect    = pygame.Rect(WIDTH // 2 - BW // 2, int(HEIGHT * 0.69), BW, BH)

    SBW = int(WIDTH * 0.56)
    SBH = int(HEIGHT * 0.52)
    setting_box_rect = pygame.Rect(WIDTH // 2 - SBW // 2, int(HEIGHT * 0.28), SBW, SBH)

    BBW = int(WIDTH * 0.16)
    BBH = int(HEIGHT * 0.065)
    btn_back_rect    = pygame.Rect(WIDTH // 2 - BBW // 2, int(HEIGHT * 0.72), BBW, BBH)

    setting_y_positions = [
        int(HEIGHT * 0.34), int(HEIGHT * 0.42), int(HEIGHT * 0.50),
        int(HEIGHT * 0.58), int(HEIGHT * 0.72)
    ]
    setting_labels = [
        "01  /  SOUND CONFIG",
        "02  /  PADDLE COLOR",
        "03  /  GAME CUSTOM",
        "04  /  CREDITS",
    ]

    # ── 파티클 + 글로우 애니메이션 상태 ────────────────────
    particles: list[Particle] = []
    anim_tick    = 0
    title_glow   = 0.0
    title_glow_d = 0.02

    game_elapsed_time = 0.0
    game_timer_active = False
    countdown_timer   = 0

    # 스코어 플래시 애니메이션
    score_flash = 0

    # 배경 파티클 (메인메뉴 장식용)
    bg_stars = [(random.randint(0, WIDTH), random.randint(0, HEIGHT),
                 random.uniform(0.3, 1.5), random.choice([NEON_CYAN, NEON_MAGENTA, NEON_PURPLE]))
                for _ in range(80)]

    running = True
    while running:
        dt         = clock.tick(60)
        mouse_pos  = pygame.mouse.get_pos()
        anim_tick += 1

        # ── 타이틀 글로우 애니메이션 ──────────────────────
        title_glow += title_glow_d
        if title_glow >= 1.0 or title_glow <= 0.0:
            title_glow_d *= -1

        # ── 이벤트 처리 ────────────────────────────────────
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
                            ball_x = WIDTH // 2 - ball_size // 2
                            ball_y = HEIGHT // 2 - ball_size // 2
                            ball_active = False
                            p1_cx = int(WIDTH * 0.06);  p1_cy = HEIGHT // 2
                            p2_cx = WIDTH - int(WIDTH * 0.06);  p2_cy = HEIGHT // 2
                            p1_joy_x = p1_joy_y = p2_joy_x = p2_joy_y = 0.0
                            UI_state          = "GAME_PLAY"
                            game_elapsed_time = 0.0
                            game_timer_active = False
                            countdown_timer   = 3000
                            countdown_active  = True
                        elif btn_setting_rect.collidepoint(mouse_pos):
                            UI_state = "SETTINGS"
                            current_setting_index = 0
                        elif btn_exit_rect.collidepoint(mouse_pos):
                            running = False
                    elif UI_state == "SETTINGS":
                        if btn_back_rect.collidepoint(mouse_pos):
                            UI_state = "MAIN_MENU"

        if not running:
            break

        # ── 네트워크 커맨드 수신 ───────────────────────────
        current_cmd = ""
        with command_lock:
            if network_command:
                current_cmd    = network_command
                network_command = ""

        # ── 카운트다운 ─────────────────────────────────────
        if UI_state == "GAME_PLAY" and countdown_active:
            countdown_timer -= dt
            if countdown_timer <= 0:
                countdown_active = False
                countdown_timer  = 0
                p1_joy_x = p1_joy_y = p2_joy_x = p2_joy_y = 0.0
                print("[시스템] 3초 카운트다운 종료.")
                send_to_esp32("SRT")
                game_timer_active = True

        # ── 게임 타이머 ────────────────────────────────────
        if UI_state == "GAME_PLAY" and game_timer_active:
            game_elapsed_time += dt
            if game_elapsed_time >= game_time_limit:
                print(f"[게임] {game_time_limit/1000:.0f}초 경과, 게임 종료")
                current_cmd = "END"

        # ── 외부 커맨드 처리 ──────────────────────────────
        if current_cmd:
            if current_cmd == "SRT":
                if UI_state == "MAIN_MENU":
                    p1_score = p2_score = 0
                    ball_x = WIDTH // 2 - ball_size // 2
                    ball_y = HEIGHT // 2 - ball_size // 2
                    ball_active = False
                    p1_cx = int(WIDTH * 0.06);  p1_cy = HEIGHT // 2
                    p2_cx = WIDTH - int(WIDTH * 0.06);  p2_cy = HEIGHT // 2
                    p1_joy_x = p1_joy_y = p2_joy_x = p2_joy_y = 0.0
                    UI_state = "GAME_PLAY"
                    game_elapsed_time = 0.0
                    game_timer_active = False
                    countdown_timer  = 3000
                    countdown_active = True
                elif UI_state == "PAUSE":
                    UI_state = "GAME_PLAY"
                    game_timer_active = False
                    countdown_timer  = 3000
                    countdown_active = True
            else:
                send_to_esp32(current_cmd)

            if current_cmd == "STP":
                if UI_state == "GAME_PLAY":
                    UI_state = "PAUSE"
                    game_timer_active = False
                    countdown_active  = False
            elif current_cmd == "END":
                if   p1_score > p2_score: result_msg = f"PLAYER1,{p1_score}"
                elif p2_score > p1_score: result_msg = f"PLAYER2,{p2_score}"
                else:                     result_msg = f"DRAW,{p1_score}"
                send_to_esp32(result_msg)
                UI_state          = "MAIN_MENU"
                popup_type        = ""
                game_elapsed_time = 0.0
                game_timer_active = False
                countdown_active  = False
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
                            popup_type    = ""
                    elif popup_type == "COLOR":
                        if   current_cmd == "UP":  popup_sub_index = (popup_sub_index - 1) % 4
                        elif current_cmd == "DN":  popup_sub_index = (popup_sub_index + 1) % 4
                        elif current_cmd == "CLK":
                            if   popup_sub_index == 0: p1_color_idx = (p1_color_idx + 1) % len(COLOR_OPTIONS)
                            elif popup_sub_index == 1: p2_color_idx = (p2_color_idx + 1) % len(COLOR_OPTIONS)
                            elif popup_sub_index in [2, 3]: popup_type = ""
                    elif popup_type == "GAME_CUSTOM":
                        if current_cmd in ["UP", "DN"]: popup_sub_index = (popup_sub_index + 1) % 2
                        elif current_cmd == "CLK":
                            if popup_sub_index == 0:
                                popup_type      = "SETTING_TIMES"
                                popup_sub_index = time_limit_idx
                            else:
                                popup_type = ""
                    elif popup_type == "SETTING_TIMES":
                        if   current_cmd == "UP":  popup_sub_index = (popup_sub_index - 1) % 5
                        elif current_cmd == "DN":  popup_sub_index = (popup_sub_index + 1) % 5
                        elif current_cmd == "CLK":
                            if popup_sub_index < 4:
                                time_limit_idx  = popup_sub_index
                                game_time_limit = TIME_LIMITS[time_limit_idx]
                                print(f"[설정] 제한시간 → {game_time_limit/1000:.0f}초")
                            else:
                                popup_type      = "GAME_CUSTOM"
                                popup_sub_index = 0
                    elif popup_type == "CREATOR":
                        if current_cmd == "CLK": popup_type = ""
                else:
                    if   current_cmd == "UP":  current_setting_index = (current_setting_index - 1) % 5
                    elif current_cmd == "DN":  current_setting_index = (current_setting_index + 1) % 5
                    elif current_cmd == "CLK":
                        if   current_setting_index == 0: popup_type = "SOUND";       popup_sub_index = 0 if sound_enabled else 1
                        elif current_setting_index == 1: popup_type = "COLOR";       popup_sub_index = 0
                        elif current_setting_index == 2: popup_type = "GAME_CUSTOM"; popup_sub_index = 0
                        elif current_setting_index == 3: popup_type = "CREATOR"
                        elif current_setting_index == 4: UI_state   = "MAIN_MENU"

        # ── 패들 이동 ──────────────────────────────────────
        if UI_state == "GAME_PLAY" and not countdown_active:
            keys = pygame.key.get_pressed()

            mx1 = p1_joy_x * paddle_speed
            my1 = p1_joy_y * paddle_speed
            if keys[pygame.K_w]: my1 = -paddle_speed
            if keys[pygame.K_s]: my1 =  paddle_speed
            if keys[pygame.K_a]: mx1 = -paddle_speed
            if keys[pygame.K_d]: mx1 =  paddle_speed

            p1_cx = max(p1_radius,           min(center_line_x - p1_radius, p1_cx + int(mx1)))
            p1_cy = max(p1_radius,           min(HEIGHT - p1_radius,         p1_cy + int(my1)))

            mx2 = p2_joy_x * paddle_speed
            my2 = p2_joy_y * paddle_speed
            if keys[pygame.K_UP]:    my2 = -paddle_speed
            if keys[pygame.K_DOWN]:  my2 =  paddle_speed
            if keys[pygame.K_LEFT]:  mx2 = -paddle_speed
            if keys[pygame.K_RIGHT]: mx2 =  paddle_speed

            p2_cx = max(center_line_x + p2_radius, min(WIDTH - p2_radius,  p2_cx + int(mx2)))
            p2_cy = max(p2_radius,                 min(HEIGHT - p2_radius,  p2_cy + int(my2)))

            # ── 공 물리 ─────────────────────────────────
            if ball_active:
                ball_x += ball_speed_x
                ball_y += ball_speed_y

            if ball_y <= 0:
                ball_speed_y = abs(ball_speed_y);  ball_y = 0
            elif ball_y >= HEIGHT - ball_size:
                ball_speed_y = -abs(ball_speed_y); ball_y = HEIGHT - ball_size

            ball_cx, ball_cy = ball_x + ball_radius, ball_y + ball_radius

            for (pcx, pcy, pr, sidedir) in [(p1_cx, p1_cy, p1_radius, 1),
                                             (p2_cx, p2_cy, p2_radius, -1)]:
                dx   = ball_cx - pcx;  dy = ball_cy - pcy
                dist = math.hypot(dx, dy)
                md   = pr + ball_radius
                if dist < md:
                    if not ball_active:
                        ball_active   = True
                        ball_speed_x  = sidedir * abs(base_ball_speed_x)
                        ball_speed_y  = base_ball_speed_y if dy >= 0 else -base_ball_speed_y
                        ball_x = pcx + sidedir * (pr + 2) - ball_radius
                    else:
                        if dist == 0: dist = 0.1
                        nx, ny = dx / dist, dy / dist
                        dp = ball_speed_x * nx + ball_speed_y * ny
                        ball_speed_x -= 2 * dp * nx
                        ball_speed_y -= 2 * dp * ny
                        ball_cx = pcx + nx * md;  ball_cy = pcy + ny * md
                        ball_x  = ball_cx - ball_radius; ball_y = ball_cy - ball_radius

            ball_rect = pygame.Rect(ball_x, ball_y, ball_size, ball_size)

            scored_player = None
            if ball_rect.colliderect(p1_goal):
                p2_score  += 1
                ball_x = int(WIDTH * 0.25) - ball_size // 2
                ball_y = HEIGHT // 2 - ball_size // 2
                ball_speed_x, ball_speed_y = base_ball_speed_x, base_ball_speed_y
                ball_active  = False
                scored_player = 2
                score_flash  = 30
            elif ball_rect.colliderect(p2_goal):
                p1_score  += 1
                ball_x = int(WIDTH * 0.75) - ball_size // 2
                ball_y = HEIGHT // 2 - ball_size // 2
                ball_speed_x, ball_speed_y = -base_ball_speed_x, base_ball_speed_y
                ball_active  = False
                scored_player = 1
                score_flash  = 30
            else:
                if ball_x <= 0:
                    ball_speed_x = abs(ball_speed_x);  ball_x = 0
                elif ball_x >= WIDTH - ball_size:
                    ball_speed_x = -abs(ball_speed_x); ball_x = WIDTH - ball_size

            if scored_player:
                col = COLOR_OPTIONS[p1_color_idx] if scored_player == 1 else COLOR_OPTIONS[p2_color_idx]
                gx  = ball_x + ball_radius
                gy  = ball_y + ball_radius
                for _ in range(40):
                    particles.append(Particle(gx, gy, col))
        else:
            ball_rect = pygame.Rect(ball_x, ball_y, ball_size, ball_size)

        # ── 파티클 업데이트 ────────────────────────────────
        for p in particles: p.update()
        particles = [p for p in particles if p.life > 0]

        if score_flash > 0: score_flash -= 1

        # ============================================================
        #  렌더링
        # ============================================================
        screen.fill(BG_DEEP)

        # 배경 그리드
        draw_grid(screen, NEON_CYAN, spacing=55, alpha=14)

        # ── 메인 메뉴 ─────────────────────────────────────
        if UI_state == "MAIN_MENU":
            # 떠다니는 배경 별
            for i, (sx, sy, sp, sc) in enumerate(bg_stars):
                ny = (sy + sp) % HEIGHT
                bg_stars[i] = (sx, ny, sp, sc)
                alpha = int(80 + 120 * math.sin(anim_tick * 0.03 + i))
                ps = pygame.Surface((3, 3), pygame.SRCALPHA)
                pygame.draw.circle(ps, (*sc, alpha), (1, 1), 1)
                screen.blit(ps, (sx, int(ny)))

            # 타이틀 글로우
            glow_alpha = int(40 + 60 * title_glow)
            title_glow_surf = pygame.Surface((WIDTH, int(HEIGHT * 0.18)), pygame.SRCALPHA)
            pygame.draw.ellipse(title_glow_surf,
                                (*NEON_CYAN, glow_alpha),
                                (WIDTH // 2 - 320, 10, 640, int(HEIGHT * 0.14)))
            screen.blit(title_glow_surf, (0, int(HEIGHT * 0.08)))

            # 타이틀
            title_surf = font_title.render("AIR HOCKEY PONG", True, NEON_CYAN)
            screen.blit(title_surf, (WIDTH // 2 - title_surf.get_width() // 2, int(HEIGHT * 0.10)))

            # 서브타이틀
            sub_surf = font_small.render("CYBER  EDITION  //  ESP32  UDP", True, NEON_MAGENTA)
            screen.blit(sub_surf, (WIDTH // 2 - sub_surf.get_width() // 2, int(HEIGHT * 0.23)))

            # 수평선
            draw_neon_line(screen, NEON_CYAN,
                           (WIDTH // 2 - 300, int(HEIGHT * 0.28)),
                           (WIDTH // 2 + 300, int(HEIGHT * 0.28)), 1)

            # 버튼
            btn_defs = [
                (btn_start_rect,   "▶  START GAME",  NEON_GREEN),
                (btn_setting_rect, "⚙  SETTINGS",    NEON_CYAN),
                (btn_exit_rect,    "✕  EXIT",         NEON_MAGENTA),
            ]
            for rect, label, col in btn_defs:
                hover = rect.collidepoint(mouse_pos)
                draw_filled_neon_rect(screen, col, rect, radius=6, glow=hover)
                txt = font_mid.render(label, True, col if not hover else WHITE)
                screen.blit(txt, (rect.centerx - txt.get_width() // 2,
                                  rect.centery - txt.get_height() // 2))

            # 하단 힌트
            hint = font_small.render("ESP32  →  192.168.0.207   |   UDP  10001 / 10002", True, DIM_GRAY)
            screen.blit(hint, (WIDTH // 2 - hint.get_width() // 2, int(HEIGHT * 0.90)))

        # ── 게임 플레이 & 일시정지 ────────────────────────
        elif UI_state in ["GAME_PLAY", "PAUSE"]:
            # 센터라인
            draw_neon_line(screen, (*DIM_GRAY, 180),
                           (center_line_x, 0), (center_line_x, HEIGHT), 2, glow=False)
            # 센터 원
            draw_neon_circle(screen, DIM_GRAY, center_line_x, HEIGHT // 2,
                             int(HEIGHT * 0.10), width=1, glow_alpha=20)

            # 골대
            draw_neon_rect(screen, COLOR_OPTIONS[p1_color_idx], p1_goal, radius=4, width=3)
            draw_neon_rect(screen, COLOR_OPTIONS[p2_color_idx], p2_goal, radius=4, width=3)

            # 패들
            pc1 = COLOR_OPTIONS[p1_color_idx]
            pc2 = COLOR_OPTIONS[p2_color_idx]

            # 패들 그림자
            draw_neon_circle(screen, pc1, p1_cx, p1_cy, p1_radius, width=3,
                             glow_radius=p1_radius + 14, glow_alpha=70)
            pygame.draw.circle(screen, pc1, (p1_cx, p1_cy), p1_radius)
            pygame.draw.circle(screen, WHITE, (p1_cx, p1_cy), int(p1_radius * 0.35))

            draw_neon_circle(screen, pc2, p2_cx, p2_cy, p2_radius, width=3,
                             glow_radius=p2_radius + 14, glow_alpha=70)
            pygame.draw.circle(screen, pc2, (p2_cx, p2_cy), p2_radius)
            pygame.draw.circle(screen, WHITE, (p2_cx, p2_cy), int(p2_radius * 0.35))

            # 공
            draw_neon_circle(screen, NEON_WHITE, ball_x + ball_radius, ball_y + ball_radius,
                             ball_radius, width=0, glow_radius=ball_radius + 6, glow_alpha=80)
            pygame.draw.circle(screen, NEON_WHITE,
                               (ball_x + ball_radius, ball_y + ball_radius), ball_radius)

            # 파티클
            for p in particles: p.draw(screen)

            # ── HUD ────────────────────────────────────────
            # 상단 HUD 배경 바
            hud_h = int(HEIGHT * 0.10)
            hud_surf = pygame.Surface((WIDTH, hud_h), pygame.SRCALPHA)
            hud_surf.fill((*GLASS, 180))
            screen.blit(hud_surf, (0, 0))
            pygame.draw.line(screen, NEON_CYAN, (0, hud_h), (WIDTH, hud_h), 1)

            # P1 레이블
            p1_lbl = font_small.render("◀  P1", True, COLOR_OPTIONS[p1_color_idx])
            screen.blit(p1_lbl, (int(WIDTH * 0.04), int(hud_h * 0.15)))

            # P2 레이블
            p2_lbl = font_small.render("P2  ▶", True, COLOR_OPTIONS[p2_color_idx])
            screen.blit(p2_lbl, (WIDTH - int(WIDTH * 0.04) - p2_lbl.get_width(), int(hud_h * 0.15)))

            # 스코어
            flash_col = NEON_YELLOW if score_flash > 0 else NEON_WHITE
            score_surf = font_large.render(f"{p1_score}  :  {p2_score}", True, flash_col)
            screen.blit(score_surf, (WIDTH // 2 - score_surf.get_width() // 2, int(hud_h * 0.08)))

            # 타이머 바
            elapsed_ratio = min(game_elapsed_time / game_time_limit, 1.0)
            bar_w    = int(WIDTH * 0.50)
            bar_h    = 6
            bar_x    = WIDTH // 2 - bar_w // 2
            bar_y    = hud_h - bar_h - 6
            pygame.draw.rect(screen, DIM_GRAY, (bar_x, bar_y, bar_w, bar_h), border_radius=3)
            filled_w = int(bar_w * (1.0 - elapsed_ratio))
            bar_col  = NEON_GREEN if elapsed_ratio < 0.6 else (NEON_YELLOW if elapsed_ratio < 0.85 else NEON_MAGENTA)
            if filled_w > 0:
                pygame.draw.rect(screen, bar_col, (bar_x, bar_y, filled_w, bar_h), border_radius=3)

            elapsed_sec = game_elapsed_time / 1000.0
            limit_sec   = game_time_limit   / 1000.0
            time_txt    = font_small.render(f"{elapsed_sec:05.1f}  /  {limit_sec:.0f}s", True, bar_col)
            screen.blit(time_txt, (WIDTH // 2 - time_txt.get_width() // 2, bar_y - time_txt.get_height() - 2))

            # ── 카운트다운 오버레이 ────────────────────────
            if UI_state == "GAME_PLAY" and countdown_active:
                ov = pygame.Surface((WIDTH, HEIGHT), pygame.SRCALPHA)
                ov.fill((0, 0, 0, 120))
                screen.blit(ov, (0, 0))
                count_val = math.ceil(countdown_timer / 1000.0)
                if count_val > 0:
                    c_surf = font_count.render(str(count_val), True, NEON_YELLOW)
                    # 글로우
                    gs = pygame.Surface((c_surf.get_width() + 40, c_surf.get_height() + 40), pygame.SRCALPHA)
                    pygame.draw.rect(gs, (*NEON_YELLOW, 30),
                                     (0, 0, gs.get_width(), gs.get_height()), border_radius=20)
                    screen.blit(gs, (WIDTH // 2 - gs.get_width() // 2,
                                     HEIGHT // 2 - gs.get_height() // 2))
                    screen.blit(c_surf, (WIDTH // 2 - c_surf.get_width() // 2,
                                         HEIGHT // 2 - c_surf.get_height() // 2))

            if UI_state == "GAME_PLAY" and not ball_active and not countdown_active:
                hint_txt = font_small.render("//  HIT THE BALL TO SERVE  //", True, NEON_YELLOW)
                screen.blit(hint_txt, (WIDTH // 2 - hint_txt.get_width() // 2, int(HEIGHT * 0.88)))

            if UI_state == "PAUSE":
                ov = pygame.Surface((WIDTH, HEIGHT), pygame.SRCALPHA)
                ov.fill((0, 0, 0, 150))
                screen.blit(ov, (0, 0))
                pause_box = pygame.Rect(WIDTH // 2 - 200, HEIGHT // 2 - 60, 400, 120)
                draw_filled_neon_rect(screen, NEON_CYAN, pause_box, radius=12)
                p_txt = font_large.render("PAUSED", True, NEON_CYAN)
                screen.blit(p_txt, (WIDTH // 2 - p_txt.get_width() // 2,
                                    HEIGHT // 2 - p_txt.get_height() // 2))

        # ── 설정 화면 ─────────────────────────────────────
        elif UI_state == "SETTINGS":
            # 타이틀
            st_surf = font_title.render("SETTINGS", True, NEON_CYAN)
            screen.blit(st_surf, (WIDTH // 2 - st_surf.get_width() // 2, int(HEIGHT * 0.10)))

            # 서브타이틀
            sub2 = font_small.render("//  SYSTEM CONFIGURATION  //", True, NEON_MAGENTA)
            screen.blit(sub2, (WIDTH // 2 - sub2.get_width() // 2, int(HEIGHT * 0.21)))

            # 설정 패널
            draw_neon_rect(screen, NEON_CYAN, setting_box_rect, radius=14, width=2)
            draw_corner_deco(screen, setting_box_rect, NEON_CYAN, size=22, width=2)

            for i, label in enumerate(setting_labels):
                selected = (current_setting_index == i) and not popup_type
                col = NEON_YELLOW if selected else NEON_WHITE
                row_y = setting_y_positions[i]
                if selected:
                    sel_rect = pygame.Rect(setting_box_rect.left + 10, row_y - 6,
                                           setting_box_rect.width - 20, int(HEIGHT * 0.047))
                    draw_filled_neon_rect(screen, NEON_YELLOW, sel_rect, radius=6, glow=True)
                lbl_s = font_mid.render(label, True, col)
                screen.blit(lbl_s, (setting_box_rect.left + int(WIDTH * 0.045), row_y))

            # 뒤로가기 버튼
            back_hover = btn_back_rect.collidepoint(mouse_pos) or (current_setting_index == 4 and not popup_type)
            draw_filled_neon_rect(screen, NEON_MAGENTA if back_hover else DIM_GRAY,
                                  btn_back_rect, radius=6, glow=back_hover)
            back_txt = font_mid.render("◀  BACK", True, NEON_WHITE)
            screen.blit(back_txt, (btn_back_rect.centerx - back_txt.get_width() // 2,
                                   btn_back_rect.centery - back_txt.get_height() // 2))

            # ── 팝업 ──────────────────────────────────────
            if popup_type:
                # 반투명 오버레이
                ov = pygame.Surface((WIDTH, HEIGHT), pygame.SRCALPHA)
                ov.fill((0, 0, 0, 160))
                screen.blit(ov, (0, 0))

                pw, ph = int(WIDTH * 0.46), int(HEIGHT * 0.38)
                pr = pygame.Rect(WIDTH // 2 - pw // 2, HEIGHT // 2 - ph // 2, pw, ph)
                draw_filled_neon_rect(screen, NEON_CYAN, pr, radius=16, glow=True)
                draw_corner_deco(screen, pr, NEON_CYAN, size=20)

                if popup_type == "SOUND":
                    t = font_mid.render("//  SOUND CONFIG  //", True, NEON_CYAN)
                    screen.blit(t, (pr.centerx - t.get_width() // 2, pr.top + int(ph * 0.08)))
                    for idx, (txt, col) in enumerate([("ON", NEON_GREEN), ("OFF", NEON_MAGENTA)]):
                        bw2, bh2 = int(pw * 0.32), int(ph * 0.18)
                        bx2 = pr.left + int(pw * (0.10 if idx == 0 else 0.58))
                        by2 = pr.top + int(ph * 0.38)
                        br2 = pygame.Rect(bx2, by2, bw2, bh2)
                        draw_filled_neon_rect(screen, col, br2, radius=8,
                                             glow=(popup_sub_index == idx))
                        s = font_mid.render(f"SOUND: {txt}", True, col if popup_sub_index != idx else WHITE)
                        screen.blit(s, (br2.centerx - s.get_width() // 2,
                                        br2.centery - s.get_height() // 2))

                elif popup_type == "COLOR":
                    t = font_mid.render("//  PADDLE COLOR  //", True, NEON_CYAN)
                    screen.blit(t, (pr.centerx - t.get_width() // 2, pr.top + int(ph * 0.06)))
                    opts = [f"P1  ▸  {COLOR_NAMES[p1_color_idx]}",
                            f"P2  ▸  {COLOR_NAMES[p2_color_idx]}",
                            "[ SAVE & EXIT ]", "[ CANCEL ]"]
                    swatch_cols = [COLOR_OPTIONS[p1_color_idx], COLOR_OPTIONS[p2_color_idx], None, None]
                    for idx, (opt, sc) in enumerate(zip(opts, swatch_cols)):
                        oy = pr.top + int(ph * 0.22) + idx * int(ph * 0.17)
                        sel = popup_sub_index == idx
                        col = NEON_YELLOW if sel else NEON_WHITE
                        if sc:
                            pygame.draw.circle(screen, sc,
                                               (pr.left + int(pw * 0.08), oy + 14), 10)
                        s = font_mid.render(opt, True, col)
                        screen.blit(s, (pr.left + int(pw * 0.16), oy))

                elif popup_type == "GAME_CUSTOM":
                    t = font_mid.render("//  GAME CUSTOM  //", True, NEON_CYAN)
                    screen.blit(t, (pr.centerx - t.get_width() // 2, pr.top + int(ph * 0.08)))
                    for idx, opt in enumerate(["01  /  SETTING TIMES", "[ BACK ]"]):
                        oy  = pr.top + int(ph * 0.34) + idx * int(ph * 0.24)
                        col = NEON_YELLOW if popup_sub_index == idx else NEON_WHITE
                        s   = font_mid.render(opt, True, col)
                        screen.blit(s, (pr.centerx - s.get_width() // 2, oy))

                elif popup_type == "SETTING_TIMES":
                    t = font_mid.render("//  TIME LIMIT  //", True, NEON_CYAN)
                    screen.blit(t, (pr.centerx - t.get_width() // 2, pr.top + int(ph * 0.05)))
                    time_opts = ["10 SECONDS", "30 SECONDS", "1 MINUTE", "2 MINUTES", "[ SAVE & EXIT ]"]
                    for idx, opt in enumerate(time_opts):
                        oy     = pr.top + int(ph * 0.20) + idx * int(ph * 0.14)
                        active = idx == time_limit_idx
                        sel    = popup_sub_index == idx
                        col    = NEON_GREEN if active else (NEON_YELLOW if sel else NEON_WHITE)
                        marker = "★ " if active else "  "
                        s      = font_small.render(marker + opt, True, col)
                        screen.blit(s, (pr.left + int(pw * 0.12), oy))

                elif popup_type == "CREATOR":
                    t = font_mid.render("//  CREDITS  //", True, NEON_CYAN)
                    screen.blit(t, (pr.centerx - t.get_width() // 2, pr.top + int(ph * 0.08)))
                    lines = [
                        ("DEVELOPER",  "Team Hockey Pong"),
                        ("HARDWARE",   "ESP32  Wi-Fi  UDP"),
                        ("ENGINE",     "Pygame  Framework"),
                    ]
                    for i, (k, v) in enumerate(lines):
                        ky  = pr.top + int(ph * 0.28) + i * int(ph * 0.20)
                        ks  = font_small.render(k + "  :", True, NEON_CYAN)
                        vs  = font_small.render(v,         True, NEON_WHITE)
                        screen.blit(ks, (pr.left + int(pw * 0.08), ky))
                        screen.blit(vs, (pr.left + int(pw * 0.46), ky))

        # ── CRT 스캔라인 오버레이 ─────────────────────────
        draw_scanlines(screen, alpha=20)

        pygame.display.flip()

    tx_socket.close()
    pygame.quit()
    sys.exit()


# ============================================================
if __name__ == "__main__":
    net_thread = threading.Thread(target=udp_server_thread, daemon=True)
    net_thread.start()
    run_game()