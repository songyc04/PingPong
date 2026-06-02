import socket
import sys

def run_client():
   # 서버 IP 및 포트 설정 (로컬 PC에서 테스트할 경우 '127.0.0.1' 사용)
   SERVER_IP = "127.0.0.1"
   SERVER_PORT = 10002

   print("==== ESP32 가상 클라이언트 터미널 ====")
   print(f"서버 연결 시도 중... ({SERVER_IP}:{SERVER_PORT})")

   try:
      # TCP 소켓 생성 및 서버 연결
      client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
      client_socket.connect((SERVER_IP, SERVER_PORT))
      print("서버 연결 성공! 보낼 명령어를 입력하세요. (종료하려면 'exit' 입력)")
      print("--------------------------------------------------")
   except Exception as e:
      print(f"서버 연결에 실패했습니다: {e}")
      sys.exit()

   try:
      while True:
         # 터미널로부터 전송할 문자열 입력받기
         message = input("보낼 메시지 입력 >> ")
         
         if message.strip() == "exit":
            print("클라이언트를 종료합니다.")
            break
            
         if not message:
            continue

         # 입력받은 문자열을 utf-8 코딩으로 전송
         client_socket.sendall(message.encode("utf-8"))
         
   except KeyboardInterrupt:
      print("\n강제 종료되었습니다.")
   except Exception as e:
      print(f"\n통신 중 오류가 발생했습니다: {e}")
   finally:
      # 소켓 자원 반환
      client_socket.close()
      print("소켓 연결이 닫혔습니다.")

if __name__ == "__main__":
   run_client()