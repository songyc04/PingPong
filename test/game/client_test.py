import socket
import sys

def run_client():
   # 파이썬 메인 프로그램(서버)의 IP 및 수신 포트 설정
   # (같은 PC에서 테스트할 경우 '127.0.0.1' 유지, 외부 PC면 실제 IP 입력)
   # SERVER_IP = "192.168.0.102"
   SERVER_IP = "192.168.0.4"  # Test server IP
   SERVER_PORT = 10001

   print("==== ESP32 가상 클라이언트 터미널 (UDP 버전) ====")
   print(f"목적지 설정 완료 -> {SERVER_IP}:{SERVER_PORT}")
   print("보낼 명령어를 입력하세요. (종료하려면 'exit' 입력)")
   print("조이스틱 예시: 2000,2000,2000,2000 | 명령 예시: SRT, STP, UP, DN")
   print("--------------------------------------------------")

   try:
      # TCP(SOCK_STREAM) 대신 UDP(SOCK_DGRAM) 소켓 생성
      # UDP는 connect() 과정이 필요 없습니다.
      client_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
   except Exception as e:
      print(f"소켓 생성에 실패했습니다: {e}")
      sys.exit()

   try:
      while True:
         # 터미널로부터 전송할 문자열 입력받기
         message = input("보낼 메시지 입력 >> ").strip()
         
         if message == "exit":
            print("클라이언트를 종료합니다.")
            break
            
         if not message:
            continue

         # UDP는 sendall 대신 sendto를 사용하여 목적지 주소를 매번 지정해 줍니다.
         client_socket.sendto(message.encode("utf-8"), (SERVER_IP, SERVER_PORT))
         print(f"[전송 완료] -> {message}")
         
   except KeyboardInterrupt:
      print("\n강제 종료되었습니다.")
   except Exception as e:
      print(f"\n통신 중 오류가 발생했습니다: {e}")
   finally:
      # 소켓 자원 반환
      client_socket.close()
      print("소켓이 닫혔습니다.")

if __name__ == "__main__":
   run_client()