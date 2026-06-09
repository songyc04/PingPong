import pygame
import sys
import socket
import threading
import math
import random
import time

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
CYAN = (0, 255, 255)
MAGENTA = (255, 0, 255)
NEON_PINK = (255, 20, 147)
NEON_BLUE = (0, 191, 255)
NEON_GREEN = (57, 255, 20)
DARK_BG1 = (10, 10, 30)
DARK_BG2 = (20, 5, 40)

COLOR_OPTIONS = [RED, BLUE, GREEN, YELLOW, ORANGE, PURPLE, WHITE, CYAN, MAGENTA, NEON_PINK]
COLOR_NAMES = ["RED", "BLUE", "GREEN", "YELLOW", "ORANGE", "PURPLE", "WHITE", "CYAN", "MAGENTA", "NEON_PINK"]

# --- 전역 게임 상태 및 네트워크 데이터 정의 ---
UI_state = "MAIN_MENU"
command_lock = threading.Lock()

p1_command = ""
p2_command = ""
p1_cmd_lock = threading.Lock()
p2_cmd_lock = threading.Lock()

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

# --- 카운트다운 및 입력 제어 플래그 ---
countdown_active = False
p1_srt_time = 0.0
p2_srt_time = 0.0
SRT_WINDOW_MS = 1000

# --- [ESP32 듀얼 보드 네트워크 설정] ---
P1_ESP_IP = "192.168.0.200"       # ★ P1 ESP32 보드의 IP 주소를 입력
P2_ESP_IP = "192.168.0.154"       # ★ P2 ESP32 보드의 IP 주소를 입력

P1_RCV_PORT = 10001               # P1 ESP -> Python 수신 포트
P2_RCV_PORT = 10002               # P2 ESP -> Python 수신 포트
P1_SEND_PORT = 10003              # Python -> P1 ESP 송신 포트 (P1 LCD)
P2_SEND_PORT = 10004              # Python -> P2 ESP 송신 포트 (P2 LCD)

p1_tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
p2_tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


class Particle:
	def __init__(self, x, y, color, vx=None, vy=None, lifetime=30):
		self.x = x
		self.y = y
		self.color = color
		self.vx = vx if vx is not None else random.uniform(-4, 4)
		self.vy = vy if vy is not None else random.uniform(-4, 4)
		self.lifetime = lifetime
		self.max_lifetime = lifetime
		self.size = random.randint(2, 6)

	def update(self):
		self.x += self.vx
		self.y += self.vy
		self.vx *= 0.96
		self.vy *= 0.96
		self.lifetime -= 1
		self.size = max(1, self.size * 0.95)

	def draw(self, surface):
		alpha = max(0, self.lifetime / self.max_lifetime)
		r = min(255, int(self.color[0] * alpha))
		g = min(255, int(self.color[1] * alpha))
		b = min(255, int(self.color[2] * alpha))
		if r > 0 or g > 0 or b > 0:
			pygame.draw.circle(surface, (r, g, b), (int(self.x), int(self.y)), max(1, int(self.size)))

	def is_dead(self):
		return self.lifetime <= 0


class Star:
	def __init__(self, width, height):
		self.x = random.randint(0, width)
		self.y = random.randint(0, height)
		self.size = random.uniform(0.5, 2.5)
		self.brightness = random.uniform(0.2, 1.0)
		self.speed = random.uniform(0.005, 0.02)
		self.phase = random.uniform(0, math.pi * 2)

	def update(self, dt):
		self.phase += self.speed * dt
		self.brightness = 0.3 + 0.7 * abs(math.sin(self.phase))

	def draw(self, surface):
		b = int(255 * self.brightness)
		color = (b, b, min(255, b + 30))
		pygame.draw.circle(surface, color, (int(self.x), int(self.y)), max(1, int(self.size)))


class BallTrail:
	def __init__(self, x, y, color, size):
		self.x = x
		self.y = y
		self.color = color
		self.size = size
		self.lifetime = 12
		self.max_lifetime = 12

	def update(self):
		self.lifetime -= 1
		self.size *= 0.9

	def draw(self, surface):
		alpha = max(0, self.lifetime / self.max_lifetime)
		r = min(255, int(self.color[0] * alpha))
		g = min(255, int(self.color[1] * alpha))
		b = min(255, int(self.color[2] * alpha))
		if r > 0 or g > 0 or b > 0 and self.size > 1:
			pygame.draw.circle(surface, (r, g, b), (int(self.x), int(self.y)), max(1, int(self.size)))

	def is_dead(self):
		return self.lifetime <= 0


def draw_glow(surface, color, center, radius, intensity=3):
	for i in range(intensity, 0, -1):
		alpha = max(0, min(255, int(60 / i)))
		glow_r = min(255, int(color[0] * alpha / 255))
		glow_g = min(255, int(color[1] * alpha / 255))
		glow_b = min(255, int(color[2] * alpha / 255))
		glow_radius = radius + i * 6
		pygame.draw.circle(surface, (glow_r, glow_g, glow_b), center, glow_radius)


def draw_gradient_bg(surface, width, height, time_val):
	for y in range(0, height, 4):
		ratio = y / height
		r = int(DARK_BG1[0] * (1 - ratio) + DARK_BG2[0] * ratio)
		g = int(DARK_BG1[1] * (1 - ratio) + DARK_BG2[1] * ratio)
		b = int(DARK_BG1[2] * (1 - ratio) + DARK_BG2[2] * ratio)
		pygame.draw.rect(surface, (r, g, b), (0, y, width, 4))


def draw_neon_text(surface, text, font, color, pos, glow_intensity=2):
	for i in range(glow_intensity, 0, -1):
		offset_color = (min(255, color[0] // (i + 1)), min(255, color[1] // (i + 1)), min(255, color[2] // (i + 1)))
		for dx in range(-i, i + 1):
			for dy in range(-i, i + 1):
				if dx != 0 or dy != 0:
					glow_surf = font.render(text, True, offset_color)
					surface.blit(glow_surf, (pos[0] + dx, pos[1] + dy))
	main_text = font.render(text, True, color)
	surface.blit(main_text, pos)
	return main_text


def draw_neon_rect(surface, color, rect, border_width=2, glow_layers=3, radius=0):
	for i in range(glow_layers, 0, -1):
		glow_color = (min(255, color[0] // (i + 1)), min(255, color[1] // (i + 1)), min(255, color[2] // (i + 1)))
		expanded = rect.inflate(i * 4, i * 4)
		pygame.draw.rect(surface, glow_color, expanded, border_width, border_radius=radius)
	pygame.draw.rect(surface, color, rect, border_width, border_radius=radius)


def draw_fancy_button(surface, rect, text, font, base_color, mouse_pos, is_selected=False):
	is_hover = rect.collidepoint(mouse_pos)
	if is_hover or is_selected:
		bright = [min(255, c + 50) for c in base_color]
	else:
		bright = list(base_color)

	for i in range(3, 0, -1):
		glow_c = (bright[0] // (i + 1), bright[1] // (i + 1), bright[2] // (i + 1))
		expanded = rect.inflate(i * 3, i * 3)
		pygame.draw.rect(surface, glow_c, expanded, border_radius=12)

	inner_rect = rect.inflate(-4, -4)
	grad_steps = 20
	for step in range(grad_steps):
		ratio = step / grad_steps
		r = max(0, bright[0] // 3 - int(ratio * 20))
		g = max(0, bright[1] // 3 - int(ratio * 20))
		b = max(0, bright[2] // 3 - int(ratio * 20))
		y_start = inner_rect.top + int(inner_rect.height * ratio)
		y_h = max(1, inner_rect.height // grad_steps)
		pygame.draw.rect(surface, (r, g, b), (inner_rect.left, y_start, inner_rect.width, y_h), border_radius=10)

	pygame.draw.rect(surface, bright, rect, 2, border_radius=12)

	txt_surf = font.render(text, True, WHITE)
	surface.blit(txt_surf, (rect.centerx - txt_surf.get_width() // 2, rect.centery - txt_surf.get_height() // 2))


def parse_joystick_value(raw_msg, player):
	global p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y

	if countdown_active:
		return

	try:
		parts = raw_msg.split(':')
		if len(parts) == 2:
			x = int(parts[0])
			y = int(parts[1])

			jx, jy = 0.0, 0.0
			center_min, center_max = 1800, 2300

			if center_min <= x <= center_max: jx = 0.0
			elif x < center_min:              jx = (x - center_min) / float(center_min)
			else:                             jx = (x - center_max) / float(4095.0 - center_max)

			if center_min <= y <= center_max: jy = 0.0
			elif y < center_min:              jy = (y - center_min) / float(center_min)
			else:                             jy = (y - center_max) / float(4095.0 - center_max)

			jx = max(-1.0, min(1.0, jx))
			jy = max(-1.0, min(1.0, jy))

			if player == 1:
				p1_joy_x, p1_joy_y = jx, jy
			else:
				p2_joy_x, p2_joy_y = jx, jy

	except Exception as e:
		print(f"[파서 에러] 데이터 변환 에러 발생: {e} | 원문: {raw_msg}")


def send_to_p1(message):
	try:
		p1_tx_socket.sendto(message.encode('utf-8'), (P1_ESP_IP, P1_SEND_PORT))
		print(f"[송신 -> P1 ESP32]: {message}")
	except Exception as e:
		print(f"[네트워크] P1 ESP32 송신 오류: {e}")


def send_to_p2(message):
	try:
		p2_tx_socket.sendto(message.encode('utf-8'), (P2_ESP_IP, P2_SEND_PORT))
		print(f"[송신 -> P2 ESP32]: {message}")
	except Exception as e:
		print(f"[네트워크] P2 ESP32 송신 오류: {e}")


def send_to_all(message):
	send_to_p1(message)
	send_to_p2(message)


def p1_udp_thread():
	global p1_command
	SERVER_IP = "0.0.0.0"
	rx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

	try:
		rx_socket.bind((SERVER_IP, P1_RCV_PORT))
		rx_socket.settimeout(0.001)
		print(f"[네트워크] P1 수신 포트({P1_RCV_PORT}) 개방 완료.")
	except Exception as e:
		print(f"[네트워크] P1 수신 포트 바인딩 실패: {e}")
		return

	while True:
		try:
			data, addr = rx_socket.recvfrom(1024)
			if not data:
				continue

			raw_msg = data.decode("utf-8").strip()

			if ':' in raw_msg:
				with command_lock:
					parse_joystick_value(raw_msg, 1)
			else:
				print(f"[P1 명령어 수신]: {raw_msg}")
				with p1_cmd_lock:
					p1_command = raw_msg

		except socket.timeout:
			continue
		except Exception as e:
			print(f"[네트워크] P1 UDP 수신 오류: {e}")
			break


def p2_udp_thread():
	global p2_command
	SERVER_IP = "0.0.0.0"
	rx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

	try:
		rx_socket.bind((SERVER_IP, P2_RCV_PORT))
		rx_socket.settimeout(0.001)
		print(f"[네트워크] P2 수신 포트({P2_RCV_PORT}) 개방 완료.")
	except Exception as e:
		print(f"[네트워크] P2 수신 포트 바인딩 실패: {e}")
		return

	while True:
		try:
			data, addr = rx_socket.recvfrom(1024)
			if not data:
				continue

			raw_msg = data.decode("utf-8").strip()

			if ':' in raw_msg:
				with command_lock:
					parse_joystick_value(raw_msg, 2)
			else:
				print(f"[P2 명령어 수신]: {raw_msg}")
				with p2_cmd_lock:
					p2_command = raw_msg

		except socket.timeout:
			continue
		except Exception as e:
			print(f"[네트워크] P2 UDP 수신 오류: {e}")
			break


def run_game():
	global UI_state, current_setting_index, popup_type, popup_sub_index
	global sound_enabled, p1_color_idx, p2_color_idx
	global p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y
	global game_time_limit, time_limit_idx, countdown_active
	global p1_command, p2_command
	global p1_srt_time, p2_srt_time

	pygame.init()

	info = pygame.display.Info()
	WIDTH, HEIGHT = info.current_w, info.current_h

	screen = pygame.display.set_mode((WIDTH, HEIGHT), pygame.NOFRAME | pygame.HWSURFACE | pygame.DOUBLEBUF)
	pygame.display.set_caption("ESP32 Air Hockey Pong - Neon Edition")
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
		small_font = pygame.font.Font("Moneygraphy-Rounded.otf", int(HEIGHT * 0.025))
	except:
		title_font = pygame.font.SysFont("malgungothic", int(HEIGHT * 0.08), bold=True)
		font = pygame.font.SysFont("malgungothic", int(HEIGHT * 0.05))
		btn_font = pygame.font.SysFont("malgungothic", int(HEIGHT * 0.035))
		countdown_font = pygame.font.SysFont("malgungothic", int(HEIGHT * 0.18), bold=True)
		small_font = pygame.font.SysFont("malgungothic", int(HEIGHT * 0.025))

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
	paused_from_game = False

	particles = []
	stars = [Star(WIDTH, HEIGHT) for _ in range(120)]
	ball_trail = []
	global_time = 0
	goal_flash_timer = 0
	goal_flash_color = WHITE
	countdown_scale = 1.0

	def reset_game():
		global countdown_active
		nonlocal p1_score, p2_score, ball_x, ball_y, ball_active
		nonlocal p1_cx, p1_cy, p2_cx, p2_cy
		nonlocal ball_speed_x, ball_speed_y
		nonlocal game_elapsed_time, game_timer_active
		nonlocal countdown_timer, goal_flash_timer
		p1_score, p2_score = 0, 0
		ball_x = WIDTH // 2 - ball_size // 2
		ball_y = HEIGHT // 2 - ball_size // 2
		ball_active = False
		ball_speed_x, ball_speed_y = base_ball_speed_x, base_ball_speed_y
		p1_cx, p1_cy = int(WIDTH * 0.06), HEIGHT // 2
		p2_cx, p2_cy = WIDTH - int(WIDTH * 0.06), HEIGHT // 2
		p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y = 0.0, 0.0, 0.0, 0.0
		game_elapsed_time = 0.0
		game_timer_active = False
		countdown_timer = 3000
		countdown_active = True
		goal_flash_timer = 0
		particles.clear()
		ball_trail.clear()

	def resume_game():
		global countdown_active, p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y
		nonlocal countdown_timer, game_timer_active
		countdown_timer = 3000
		countdown_active = True
		game_timer_active = False
		p1_joy_x, p1_joy_y, p2_joy_x, p2_joy_y = 0.0, 0.0, 0.0, 0.0
		ball_trail.clear()

	def end_game():
		if p1_score > p2_score:
			send_to_p1("You Win!")
			send_to_p2("You Lose...")
		elif p2_score > p1_score:
			send_to_p1("You Lose...")
			send_to_p2("You Win!")
		else:
			send_to_all("DRAW -_-")

		send_to_all("END")
		return "MAIN_MENU"

	running = True
	while running:
		dt = clock.tick(60)
		global_time += dt
		mouse_pos = pygame.mouse.get_pos()

		for star in stars:
			star.update(dt)

		for p in particles[:]:
			p.update()
			if p.is_dead():
				particles.remove(p)

		for t in ball_trail[:]:
			t.update()
			if t.is_dead():
				ball_trail.remove(t)

		if goal_flash_timer > 0:
			goal_flash_timer -= dt

		for event in pygame.event.get():
			if event.type == pygame.QUIT:
				running = False
			elif event.type == pygame.KEYDOWN:
				if event.key == pygame.K_ESCAPE:
					if popup_type:
						popup_type = ""
					elif UI_state in ["GAME_PLAY", "PAUSE", "SETTINGS"]:
						if UI_state == "SETTINGS" and paused_from_game:
							resume_game()
							UI_state = "GAME_PLAY"
						else:
							UI_state = "MAIN_MENU"
							countdown_active = False
						paused_from_game = False
					else:
						running = False
			elif event.type == pygame.MOUSEBUTTONDOWN:
				if event.button == 1:
					if popup_type:
						popup_type = ""
						continue
					if UI_state == "MAIN_MENU":
						if btn_start_rect.collidepoint(mouse_pos):
							reset_game()
							UI_state = "GAME_PLAY"
						elif btn_setting_rect.collidepoint(mouse_pos):
							UI_state = "SETTINGS"
							current_setting_index = 0
						elif btn_exit_rect.collidepoint(mouse_pos):
							running = False
					elif UI_state == "SETTINGS":
						if btn_back_rect.collidepoint(mouse_pos):
							if paused_from_game:
								resume_game()
								UI_state = "GAME_PLAY"
							else:
								UI_state = "MAIN_MENU"
							paused_from_game = False

		if not running:
			break

		p1_cmd = ""
		p2_cmd = ""
		with p1_cmd_lock:
			if p1_command:
				p1_cmd = p1_command
				p1_command = ""
		with p2_cmd_lock:
			if p2_command:
				p2_cmd = p2_command
				p2_command = ""

		if UI_state == "MAIN_MENU":
			if p1_cmd == "SET":
				UI_state = "SETTINGS"
				current_setting_index = 0
				popup_type = ""
				send_to_all("SET")

			if p1_cmd == "SRT":
				p1_srt_time = time.time() * 1000
				if p2_srt_time > 0 and abs(p1_srt_time - p2_srt_time) <= SRT_WINDOW_MS:
					reset_game()
					UI_state = "GAME_PLAY"
					p1_srt_time = 0.0
					p2_srt_time = 0.0
				else:
					p2_srt_time = 0.0

			if p2_cmd == "SRT":
				p2_srt_time = time.time() * 1000
				if p1_srt_time > 0 and abs(p1_srt_time - p2_srt_time) <= SRT_WINDOW_MS:
					reset_game()
					UI_state = "GAME_PLAY"
					p1_srt_time = 0.0
					p2_srt_time = 0.0
				else:
					p1_srt_time = 0.0

		elif UI_state in ["GAME_PLAY", "PAUSE"]:
			if UI_state == "GAME_PLAY" and countdown_active:
				countdown_timer -= dt
				countdown_scale = 1.0 + 0.3 * math.sin((countdown_timer / 1000.0) * math.pi * 2)
				if countdown_timer <= 0:
					countdown_active = False
					countdown_timer = 0
					countdown_scale = 1.0
					print("[시스템] 카운트다운 완료 -> SRT 송신.")
					send_to_all("SRT")
					game_timer_active = True

			if UI_state == "GAME_PLAY" and game_timer_active:
				game_elapsed_time += dt
				if game_elapsed_time >= game_time_limit:
					result = end_game()
					UI_state = result
					popup_type = ""
					countdown_active = False

			if p1_cmd == "SET":
				if UI_state == "GAME_PLAY":
					UI_state = "PAUSE"
					game_timer_active = False
					countdown_active = False
				paused_from_game = True
				UI_state = "SETTINGS"
				current_setting_index = 0
				popup_type = ""
				send_to_all("SET")

			if p2_cmd == "END":
				if UI_state in ["GAME_PLAY", "PAUSE"]:
					result = end_game()
					UI_state = result
					popup_type = ""
					game_elapsed_time = 0.0
					game_timer_active = False
					countdown_active = False
					send_to_all("END")

			elif p2_cmd == "STP":
				if UI_state == "GAME_PLAY":
					UI_state = "PAUSE"
					game_timer_active = False
					countdown_active = False
					send_to_all("STP")

		elif UI_state == "SETTINGS":
			if p1_cmd == "SET" or (p1_cmd == "CLK" and current_setting_index == 4 and not popup_type):
				if current_setting_index == 4 or p1_cmd == "SET":
					if paused_from_game:
						resume_game()
						UI_state = "GAME_PLAY"
					else:
						UI_state = "MAIN_MENU"
					paused_from_game = False
					p1_cmd = ""

			if popup_type:
				if popup_type == "SOUND":
					if p1_cmd in ["UP", "DN"]: popup_sub_index = (popup_sub_index + 1) % 2
					elif p1_cmd == "CLK":
						sound_enabled = (popup_sub_index == 0)
						popup_type = ""
				elif popup_type == "COLOR":
					if p1_cmd == "UP": popup_sub_index = (popup_sub_index - 1) % 4
					elif p1_cmd == "DN": popup_sub_index = (popup_sub_index + 1) % 4
					elif p1_cmd == "CLK":
						if popup_sub_index == 0: p1_color_idx = (p1_color_idx + 1) % len(COLOR_OPTIONS)
						elif popup_sub_index == 1: p2_color_idx = (p2_color_idx + 1) % len(COLOR_OPTIONS)
						elif popup_sub_index in [2, 3]: popup_type = ""
				elif popup_type == "GAME_CUSTOM":
					if p1_cmd in ["UP", "DN"]: popup_sub_index = (popup_sub_index + 1) % 2
					elif p1_cmd == "CLK":
						if popup_sub_index == 0:
							popup_type = "SETTING_TIMES"
							popup_sub_index = time_limit_idx
						else:
							popup_type = ""
				elif popup_type == "SETTING_TIMES":
					if p1_cmd == "UP": popup_sub_index = (popup_sub_index - 1) % 5
					elif p1_cmd == "DN": popup_sub_index = (popup_sub_index + 1) % 5
					elif p1_cmd == "CLK":
						if popup_sub_index < 4:
							time_limit_idx = popup_sub_index
							game_time_limit = TIME_LIMITS[time_limit_idx]
						else:
							popup_type = "GAME_CUSTOM"
							popup_sub_index = 0
				elif popup_type == "CREATOR":
					if p1_cmd == "CLK": popup_type = ""
			else:
				if p1_cmd == "UP": current_setting_index = (current_setting_index - 1) % 5
				elif p1_cmd == "DN": current_setting_index = (current_setting_index + 1) % 5
				elif p1_cmd == "CLK":
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
						if paused_from_game:
							resume_game()
							UI_state = "GAME_PLAY"
						else:
							UI_state = "MAIN_MENU"
						paused_from_game = False

		if UI_state == "GAME_PLAY" and not countdown_active:
			keys = pygame.key.get_pressed()

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

			if ball_active:
				ball_x += ball_speed_x
				ball_y += ball_speed_y

				if global_time % 2 < 20:
					ball_color = WHITE
					ball_trail.append(BallTrail(ball_x + ball_radius, ball_y + ball_radius, ball_color, ball_radius))

			if ball_y <= 0:
				ball_speed_y = abs(ball_speed_y)
				ball_y = 0
				for _ in range(5):
					particles.append(Particle(ball_x + ball_radius, 0, CYAN, random.uniform(-3, 3), random.uniform(0, 3)))
			elif ball_y >= HEIGHT - ball_size:
				ball_speed_y = -abs(ball_speed_y)
				ball_y = HEIGHT - ball_size
				for _ in range(5):
					particles.append(Particle(ball_x + ball_radius, HEIGHT, CYAN, random.uniform(-3, 3), random.uniform(-3, 0)))

			ball_cx, ball_cy = ball_x + ball_radius, ball_y + ball_radius

			dx1, dy1 = ball_cx - p1_cx, ball_cy - p1_cy
			distance1 = math.hypot(dx1, dy1)
			min_dist1 = p1_radius + ball_radius

			if distance1 < min_dist1:
				if not ball_active:
					ball_active = True
					ball_speed_x = abs(base_ball_speed_x)
					ball_speed_y = base_ball_speed_y if dy1 >= 0 else -base_ball_speed_y
					ball_x = p1_cx + p1_radius + 2
					for _ in range(15):
						particles.append(Particle(ball_cx, ball_cy, COLOR_OPTIONS[p1_color_idx]))
				else:
					if distance1 == 0: distance1 = 0.1
					nx, ny = dx1 / distance1, dy1 / distance1
					dot_product = ball_speed_x * nx + ball_speed_y * ny
					ball_speed_x = ball_speed_x - 2 * dot_product * nx
					ball_speed_y = ball_speed_y - 2 * dot_product * ny
					ball_cx = p1_cx + nx * min_dist1
					ball_cy = p1_cy + ny * min_dist1
					ball_x, ball_y = ball_cx - ball_radius, ball_cy - ball_radius
					for _ in range(12):
						particles.append(Particle(ball_cx, ball_cy, COLOR_OPTIONS[p1_color_idx]))

			dx2, dy2 = ball_cx - p2_cx, ball_cy - p2_cy
			distance2 = math.hypot(dx2, dy2)
			min_dist2 = p2_radius + ball_radius

			if distance2 < min_dist2:
				if not ball_active:
					ball_active = True
					ball_speed_x = -abs(base_ball_speed_x)
					ball_speed_y = base_ball_speed_y if dy2 >= 0 else -base_ball_speed_y
					ball_x = p2_cx - p2_radius - ball_size - 2
					for _ in range(15):
						particles.append(Particle(ball_cx, ball_cy, COLOR_OPTIONS[p2_color_idx]))
				else:
					if distance2 == 0: distance2 = 0.1
					nx, ny = dx2 / distance2, dy2 / distance2
					dot_product = ball_speed_x * nx + ball_speed_y * ny
					ball_speed_x = ball_speed_x - 2 * dot_product * nx
					ball_speed_y = ball_speed_y - 2 * dot_product * ny
					ball_cx = p2_cx + nx * min_dist2
					ball_cy = p2_cy + ny * min_dist2
					ball_x, ball_y = ball_cx - ball_radius, ball_cy - ball_radius
					for _ in range(12):
						particles.append(Particle(ball_cx, ball_cy, COLOR_OPTIONS[p2_color_idx]))

			ball_rect = pygame.Rect(ball_x, ball_y, ball_size, ball_size)

			if ball_rect.colliderect(p1_goal):
				p2_score += 1
				ball_x = int(WIDTH * 0.25) - ball_size // 2
				ball_y = HEIGHT // 2 - ball_size // 2
				ball_speed_x, ball_speed_y = base_ball_speed_x, base_ball_speed_y
				ball_active = False
				goal_flash_timer = 500
				goal_flash_color = COLOR_OPTIONS[p2_color_idx]
				for _ in range(40):
					particles.append(Particle(p1_goal.centerx, p1_goal.centery, COLOR_OPTIONS[p2_color_idx], random.uniform(-6, 6), random.uniform(-6, 6), 50))
			elif ball_rect.colliderect(p2_goal):
				p1_score += 1
				ball_x = int(WIDTH * 0.75) - ball_size // 2
				ball_y = HEIGHT // 2 - ball_size // 2
				ball_speed_x, ball_speed_y = -base_ball_speed_x, base_ball_speed_y
				ball_active = False
				goal_flash_timer = 500
				goal_flash_color = COLOR_OPTIONS[p1_color_idx]
				for _ in range(40):
					particles.append(Particle(p2_goal.centerx, p2_goal.centery, COLOR_OPTIONS[p1_color_idx], random.uniform(-6, 6), random.uniform(-6, 6), 50))
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
		draw_gradient_bg(screen, WIDTH, HEIGHT, global_time)

		for star in stars:
			star.draw(screen)

		border_pulse = int(80 + 40 * math.sin(global_time * 0.003))
		border_color = (0, border_pulse // 2, border_pulse)
		pygame.draw.rect(screen, border_color, (0, 0, WIDTH, HEIGHT), 3)
		pygame.draw.rect(screen, (0, border_pulse // 4, border_pulse // 2), (2, 2, WIDTH - 4, HEIGHT - 4), 1)

		dash_len = 20
		gap_len = 15
		y = 0
		pulse = int(100 + 50 * math.sin(global_time * 0.004))
		line_color = (pulse // 3, pulse // 3, pulse)
		while y < HEIGHT:
			end_y = min(y + dash_len, HEIGHT)
			pygame.draw.line(screen, line_color, (center_line_x, y), (center_line_x, end_y), 2)
			y += dash_len + gap_len

		p1_glow_pulse = int(4 + 2 * math.sin(global_time * 0.005))
		p2_glow_pulse = int(4 + 2 * math.sin(global_time * 0.005 + math.pi))
		draw_glow(screen, COLOR_OPTIONS[p1_color_idx], p1_goal.center, goal_height // 2, p1_glow_pulse)
		draw_glow(screen, COLOR_OPTIONS[p2_color_idx], p2_goal.center, goal_height // 2, p2_glow_pulse)
		pygame.draw.rect(screen, COLOR_OPTIONS[p1_color_idx], p1_goal, 3)
		pygame.draw.rect(screen, COLOR_OPTIONS[p2_color_idx], p2_goal, 3)

		if goal_flash_timer > 0:
			flash_alpha = min(80, int(goal_flash_timer / 500.0 * 80))
			flash_surf = pygame.Surface((WIDTH, HEIGHT), pygame.SRCALPHA)
			flash_surf.fill((goal_flash_color[0], goal_flash_color[1], goal_flash_color[2], flash_alpha))
			screen.blit(flash_surf, (0, 0))

		for t in ball_trail:
			t.draw(screen)

		if UI_state in ["GAME_PLAY", "PAUSE"]:
			draw_glow(screen, COLOR_OPTIONS[p1_color_idx], (p1_cx, p1_cy), p1_radius, 3)
			pygame.draw.circle(screen, COLOR_OPTIONS[p1_color_idx], (p1_cx, p1_cy), p1_radius)
			inner_r = int(p1_radius * 0.6)
			pygame.draw.circle(screen, (min(255, COLOR_OPTIONS[p1_color_idx][0] + 60), min(255, COLOR_OPTIONS[p1_color_idx][1] + 60), min(255, COLOR_OPTIONS[p1_color_idx][2] + 60)), (p1_cx, p1_cy), inner_r)
			pygame.draw.circle(screen, WHITE, (p1_cx, p1_cy), int(p1_radius * 0.2))

			draw_glow(screen, COLOR_OPTIONS[p2_color_idx], (p2_cx, p2_cy), p2_radius, 3)
			pygame.draw.circle(screen, COLOR_OPTIONS[p2_color_idx], (p2_cx, p2_cy), p2_radius)
			inner_r2 = int(p2_radius * 0.6)
			pygame.draw.circle(screen, (min(255, COLOR_OPTIONS[p2_color_idx][0] + 60), min(255, COLOR_OPTIONS[p2_color_idx][1] + 60), min(255, COLOR_OPTIONS[p2_color_idx][2] + 60)), (p2_cx, p2_cy), inner_r2)
			pygame.draw.circle(screen, WHITE, (p2_cx, p2_cy), int(p2_radius * 0.2))

		draw_glow(screen, WHITE, (ball_x + ball_radius, ball_y + ball_radius), ball_radius, 2)
		pygame.draw.ellipse(screen, WHITE, ball_rect)
		highlight_rect = pygame.Rect(ball_x + ball_size // 4, ball_y + ball_size // 4, ball_size // 3, ball_size // 3)
		pygame.draw.ellipse(screen, (200, 200, 255), highlight_rect)

		for p in particles:
			p.draw(screen)

		if UI_state == "MAIN_MENU":
			title_glow_offset = int(3 * math.sin(global_time * 0.003))
			title_color = (min(255, 100 + title_glow_offset * 20), min(255, 200 + title_glow_offset * 10), 255)
			draw_neon_text(screen, "AIR HOCKEY PONG", title_font, title_color, (WIDTH // 2 - title_font.size("AIR HOCKEY PONG")[0] // 2, int(HEIGHT * 0.15)), 3)

			subtitle_text = small_font.render("NEON EDITION", True, CYAN)
			screen.blit(subtitle_text, (WIDTH // 2 - subtitle_text.get_width() // 2, int(HEIGHT * 0.26)))

			draw_fancy_button(screen, btn_start_rect, "START GAME", btn_font, GREEN, mouse_pos)
			draw_fancy_button(screen, btn_setting_rect, "SETTINGS", btn_font, NEON_BLUE, mouse_pos)
			draw_fancy_button(screen, btn_exit_rect, "EXIT", btn_font, RED, mouse_pos)

		elif UI_state in ["GAME_PLAY", "PAUSE"]:
			score_bg = pygame.Rect(WIDTH // 2 - int(WIDTH * 0.12), int(HEIGHT * 0.02), int(WIDTH * 0.24), int(HEIGHT * 0.08))
			pygame.draw.rect(screen, (10, 10, 30), score_bg, border_radius=10)
			draw_neon_rect(screen, CYAN, score_bg, 1, 2, 10)

			p1_score_text = font.render(f"{p1_score}", True, COLOR_OPTIONS[p1_color_idx])
			p2_score_text = font.render(f"{p2_score}", True, COLOR_OPTIONS[p2_color_idx])
			sep_text = font.render(":", True, WHITE)

			screen.blit(p1_score_text, (WIDTH // 2 - int(WIDTH * 0.06) - p1_score_text.get_width() // 2, int(HEIGHT * 0.03)))
			screen.blit(sep_text, (WIDTH // 2 - sep_text.get_width() // 2, int(HEIGHT * 0.03)))
			screen.blit(p2_score_text, (WIDTH // 2 + int(WIDTH * 0.06) - p2_score_text.get_width() // 2, int(HEIGHT * 0.03)))

			elapsed_sec = game_elapsed_time / 1000.0
			limit_sec = game_time_limit / 1000.0
			time_ratio = min(1.0, elapsed_sec / limit_sec) if limit_sec > 0 else 0

			time_bar_width = int(WIDTH * 0.2)
			time_bar_height = int(HEIGHT * 0.012)
			time_bar_x = WIDTH // 2 - time_bar_width // 2
			time_bar_y = int(HEIGHT * 0.11)

			pygame.draw.rect(screen, (30, 30, 50), (time_bar_x, time_bar_y, time_bar_width, time_bar_height), border_radius=4)

			if time_ratio < 0.5:
				bar_color = NEON_GREEN
			elif time_ratio < 0.8:
				bar_color = YELLOW
			else:
				bar_color = RED
			fill_width = int(time_bar_width * (1.0 - time_ratio))
			if fill_width > 0:
				pygame.draw.rect(screen, bar_color, (time_bar_x, time_bar_y, fill_width, time_bar_height), border_radius=4)

			time_text = small_font.render(f"{elapsed_sec:.1f}s / {limit_sec:.0f}s", True, WHITE)
			screen.blit(time_text, (WIDTH // 2 - time_text.get_width() // 2, time_bar_y + time_bar_height + 4))

			if UI_state == "GAME_PLAY" and countdown_active:
				count_val = math.ceil(countdown_timer / 1000.0)
				if count_val > 0:
					count_str = str(count_val)
					scaled_size = int(HEIGHT * 0.18 * countdown_scale)
					if scaled_size > 10:
						try:
							scaled_font = pygame.font.Font("Moneygraphy-Rounded.otf", scaled_size)
						except:
							scaled_font = pygame.font.SysFont("malgungothic", scaled_size, bold=True)
						pulse_bright = int(200 + 55 * math.sin(global_time * 0.01))
						count_color = (pulse_bright, pulse_bright, 0)
						draw_neon_text(screen, count_str, scaled_font, count_color, (WIDTH // 2 - scaled_font.size(count_str)[0] // 2, HEIGHT // 2 - scaled_font.size(count_str)[1] // 2), 4)

			if UI_state == "GAME_PLAY" and not ball_active and not countdown_active:
				serve_alpha = int(180 + 75 * math.sin(global_time * 0.005))
				serve_color = (serve_alpha, serve_alpha, 0)
				sub_text = btn_font.render("Hit the ball to serve!", True, serve_color)
				screen.blit(sub_text, (WIDTH // 2 - sub_text.get_width() // 2, int(HEIGHT * 0.18)))

			if UI_state == "PAUSE":
				overlay = pygame.Surface((WIDTH, HEIGHT), pygame.SRCALPHA)
				overlay.fill((0, 0, 0, 120))
				screen.blit(overlay, (0, 0))
				draw_neon_text(screen, "PAUSED", title_font, GRAY, (WIDTH // 2 - title_font.size("PAUSED")[0] // 2, HEIGHT // 2 - title_font.size("PAUSED")[1] // 2), 3)

		elif UI_state == "SETTINGS":
			draw_neon_text(screen, "SETTINGS", title_font, NEON_BLUE, (WIDTH // 2 - title_font.size("SETTINGS")[0] // 2, int(HEIGHT * 0.15)), 2)

			draw_neon_rect(screen, (60, 60, 100), setting_box_rect, 2, 2, 15)

			labels = ["1. SOUND SETTINGS", "2. COLOR CUSTOM", "3. GAME CUSTOM", "4. CREATOR CREDITS"]
			for i, label_text in enumerate(labels):
				if current_setting_index == i and not popup_type:
					color_preset = YELLOW
					pulse = int(3 * math.sin(global_time * 0.006))
					indicator_x = setting_box_rect.left + int(WIDTH * 0.01) + pulse
					indicator_text = font.render(">", True, YELLOW)
					screen.blit(indicator_text, (indicator_x, setting_y_positions[i]))
				else:
					color_preset = WHITE
				lbl_surf = font.render(label_text, True, color_preset)
				screen.blit(lbl_surf, (setting_box_rect.left + int(WIDTH * 0.05), setting_y_positions[i]))

			draw_fancy_button(screen, btn_back_rect, "BACK", btn_font, GRAY, mouse_pos, is_selected=(current_setting_index == 4 and not popup_type))

			if not popup_type:
				if current_setting_index < 4:
					cursor_text = font.render(">", True, YELLOW)
					cursor_x = setting_box_rect.left + int(WIDTH * 0.01)
					cursor_y = setting_y_positions[current_setting_index]
					screen.blit(cursor_text, (cursor_x, cursor_y))

			if popup_type:
				popup_w, popup_h = int(WIDTH * 0.45), int(HEIGHT * 0.35)
				popup_rect = pygame.Rect(WIDTH // 2 - popup_w // 2, HEIGHT // 2 - popup_h // 2, popup_w, popup_h)

				dimmed = pygame.Surface((WIDTH, HEIGHT), pygame.SRCALPHA)
				dimmed.fill((0, 0, 0, 100))
				screen.blit(dimmed, (0, 0))

				pygame.draw.rect(screen, (15, 15, 35), popup_rect, border_radius=12)
				draw_neon_rect(screen, YELLOW, popup_rect, 3, 3, 12)

				if popup_type == "SOUND":
					draw_neon_text(screen, "SOUND CONFIG", font, YELLOW, (popup_rect.centerx - font.size("SOUND CONFIG")[0] // 2, popup_rect.top + int(HEIGHT * 0.03)), 1)
					for idx, text in enumerate(["SOUND: ON", "SOUND: OFF"]):
						target_rect = pygame.Rect(popup_rect.left + int(popup_w * (0.15 if idx == 0 else 0.55)), popup_rect.top + int(HEIGHT * 0.14), int(popup_w * 0.3), int(HEIGHT * 0.06))
						c = GREEN if popup_sub_index == idx else GRAY
						draw_fancy_button(screen, target_rect, text, btn_font, c, mouse_pos, popup_sub_index == idx)
				elif popup_type == "COLOR":
					draw_neon_text(screen, "PADDLE COLOR", font, YELLOW, (popup_rect.centerx - font.size("PADDLE COLOR")[0] // 2, popup_rect.top + int(HEIGHT * 0.02)), 1)
					options_text = [f"P1 COLOR : < {COLOR_NAMES[p1_color_idx]} >", f"P2 COLOR : < {COLOR_NAMES[p2_color_idx]} >", "[ SAVE & EXIT ]", "[ CANCEL ]"]
					for idx, text in enumerate(options_text):
						current_y = popup_rect.top + int(HEIGHT * 0.09) + (idx * int(HEIGHT * 0.05))
						if idx == 0:
							pygame.draw.rect(screen, COLOR_OPTIONS[p1_color_idx], (popup_rect.right - int(popup_w * 0.18), current_y + 5, 25, 25), border_radius=4)
						elif idx == 1:
							pygame.draw.rect(screen, COLOR_OPTIONS[p2_color_idx], (popup_rect.right - int(popup_w * 0.18), current_y + 5, 25, 25), border_radius=4)
						txt_s = btn_font.render(text, True, YELLOW if popup_sub_index == idx else WHITE)
						screen.blit(txt_s, (popup_rect.left + int(popup_w * 0.1), current_y))
				elif popup_type == "GAME_CUSTOM":
					draw_neon_text(screen, "GAME CUSTOM", font, YELLOW, (popup_rect.centerx - font.size("GAME CUSTOM")[0] // 2, popup_rect.top + int(HEIGHT * 0.03)), 1)
					options_text = ["1. SETTING TIMES", "[ BACK ]"]
					for idx, text in enumerate(options_text):
						current_y = popup_rect.top + int(HEIGHT * 0.12) + (idx * int(HEIGHT * 0.08))
						color_preset = YELLOW if popup_sub_index == idx else WHITE
						txt_s = btn_font.render(text, True, color_preset)
						screen.blit(txt_s, (popup_rect.centerx - txt_s.get_width() // 2, current_y))
				elif popup_type == "SETTING_TIMES":
					draw_neon_text(screen, "SETTING TIMES", font, YELLOW, (popup_rect.centerx - font.size("SETTING TIMES")[0] // 2, popup_rect.top + int(HEIGHT * 0.02)), 1)
					options_text = ["10 SECONDS", "30 SECONDS", "1 MINUTE", "2 MINUTES", "[ SAVE & EXIT ]"]
					for idx, text in enumerate(options_text):
						current_y = popup_rect.top + int(HEIGHT * 0.08) + (idx * int(HEIGHT * 0.05))
						marker = "* " if idx == time_limit_idx else "  "
						color_val = NEON_GREEN if idx == time_limit_idx else (YELLOW if popup_sub_index == idx else WHITE)
						txt_s = btn_font.render(marker + text, True, color_val)
						screen.blit(txt_s, (popup_rect.left + int(popup_w * 0.1), current_y))
				elif popup_type == "CREATOR":
					draw_neon_text(screen, "TEAM CREDITS", font, YELLOW, (popup_rect.centerx - font.size("TEAM CREDITS")[0] // 2, popup_rect.top + int(HEIGHT * 0.03)), 1)
					for idx, line in enumerate(["DEVELOPER: Team Hockey Pong", "HARDWARE: ESP32 Wi-Fi UDP", "GRAPHICS: Pygame Neon Framework"]):
						txt_s = btn_font.render(line, True, WHITE)
						screen.blit(txt_s, (popup_rect.centerx - txt_s.get_width() // 2, popup_rect.top + int(HEIGHT * 0.12) + (idx * 30)))

		pygame.display.flip()

	p1_tx_socket.close()
	p2_tx_socket.close()
	pygame.quit()
	sys.exit()


if __name__ == "__main__":
	p1_thread = threading.Thread(target=p1_udp_thread, daemon=True)
	p2_thread = threading.Thread(target=p2_udp_thread, daemon=True)
	p1_thread.start()
	p2_thread.start()
	run_game()
