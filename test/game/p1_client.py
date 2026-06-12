import socket
import threading
import sys
import random
import time

SERVER_IP = "127.0.0.1"
SEND_PORT = 10001
RECV_PORT = 10003

tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
joystick_active = False
joystick_lock = threading.Lock()


def recv_thread():
	global joystick_active
	rx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	try:
		rx_socket.bind((SERVER_IP, RECV_PORT))
		rx_socket.settimeout(1.0)
		print(f"[P1 수신] 포트 {RECV_PORT} 개방 완료")
	except Exception as e:
		print(f"[P1 수신] 포트 바인딩 실패: {e}")
		return

	while True:
		try:
			data, addr = rx_socket.recvfrom(1024)
			if data:
				msg = data.decode("utf-8").strip()
				print(f"\n[P1 수신 <- Python] {msg}")
				
				if msg == "SRT":
					with joystick_lock:
						joystick_active = True
					print("[P1] 조이스틱 시뮬레이션 시작")
				elif msg in ["END", "STP"]:
					with joystick_lock:
						joystick_active = False
					print("[P1] 조이스틱 시뮬레이션 중지")
				
				print("명령어 입력> ", end="", flush=True)
		except socket.timeout:
			continue
		except Exception as e:
			print(f"[P1 수신] 오류: {e}")
			break


def send_message(msg):
	try:
		tx_socket.sendto(msg.encode("utf-8"), (SERVER_IP, SEND_PORT))
		print(f"[P1 송신 -> Python:{SEND_PORT}] {msg}")
	except Exception as e:
		print(f"[P1 송신] 오류: {e}")


def joystick_thread():
	global joystick_active
	while True:
		time.sleep(1.0)
		with joystick_lock:
			active = joystick_active
		if active:
			x = random.randint(0, 4095)
			y = random.randint(0, 4095)
			msg = f"{x}:{y}"
			try:
				tx_socket.sendto(msg.encode("utf-8"), (SERVER_IP, SEND_PORT))
				print(f"[P1 조이스틱 자동 전송] {msg}")
			except Exception as e:
				print(f"[P1 조이스틱] 전송 오류: {e}")


def main():
	print("=" * 50)
	print("  P1 ESP32 시뮬레이터 클라이언트")
	print("=" * 50)
	print(f"  Python 서버 IP: {SERVER_IP}")
	print(f"  송신 포트 (-> Python): {SEND_PORT}")
	print(f"  수신 포트 (<- Python): {RECV_PORT}")
	print("=" * 50)
	print()
	print("사용 가능한 명령어:")
	print("  SRT    - 게임 시작 요청 (양쪽 동시 필요)")
	print("  STP    - 게임 일시정지")
	print("  SET    - 설정창 열기")
	print("  UP     - 메뉴 위 이동")
	print("  DN     - 메뉴 아래 이동")
	print("  CLK    - 선택/확인")
	print("  x:y    - 조이스틱 값 (예: 2048:3000)")
	print("  q      - 종료")
	print()

	recv = threading.Thread(target=recv_thread, daemon=True)
	recv.start()

	joy = threading.Thread(target=joystick_thread, daemon=True)
	joy.start()

	while True:
		try:
			user_input = input("명령어 입력> ").strip()

			if user_input.lower() == 'q':
				print("[P1] 클라이언트 종료")
				break
			elif user_input:
				send_message(user_input)

		except KeyboardInterrupt:
			print("\n[P1] 종료")
			break
		except Exception as e:
			print(f"[P1] 입력 오류: {e}")

	tx_socket.close()


if __name__ == "__main__":
	main()
