import socket
import sys

def run_server_test():
   # 서버 IP 및 포트 설정 (0.0.0.0은 모든 IP로부터의 접속을 허용합니다)
   SERVER_IP = "0.0.0.0"
   SERVER_PORT = 10002

   # TCP 소켓 생성
   server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
   # 포트 재사용 설정 (서버 강제 종료 후 재실행 시 포트 충돌 방지)
   server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

   try:
      server_socket.bind((SERVER_IP, SERVER_PORT))
      server_socket.listen(1)
      print("==== TCP/IP 서버 테스트 모드 ====")
      print(f"클라이언트의 연결을 대기 중입니다... (포트: {SERVER_PORT})")
      print("--------------------------------------------------")
   except Exception as e:
      print(f"서버 바인딩 실패: {e}")
      sys.exit()

   try:
      while True:
         # 클라이언트 접속 대기 (Bloking 모드)
         client_socket, addr = server_socket.accept()
         print(f"[연결 성공] 클라이언트가 접속했습니다! 주소: {addr}")
         print("메시지 수신 대기 중...\n")

         while True:
            try:
               # 클라이언트로부터 최대 1024바이트 데이터 수신
               data = client_socket.recv(1024)
               
               # 데이터가 없으면 클라이언트가 연결을 끊은 것임
               if not data:
                  print(f"\n[연결 끊김] 클라이언트({addr})와의 연결이 종료되었습니다.")
                  break

               # 수신된 바이너리 데이터를 utf-8 문자열로 디코딩
               received_message = data.decode("utf-8").strip()
               print(f"[수신 데이터] {received_message}")

            except Exception as e:
               print(f"\n[통신 오류] 데이터 수신 중 문제가 발생했습니다: {e}")
               break
         
         # 세션 종료 후 소켓 닫고 다음 클라이언트 대기
         client_socket.close()
         print("--------------------------------------------------")
         print("다음 클라이언트의 연결을 다시 대기합니다...")

   except KeyboardInterrupt:
      print("\n[서버 종료] 사용자에 의해 서버가 강제 종료되었습니다.")
   finally:
      server_socket.close()
      print("서버 소켓 자원이 안전하게 반환되었습니다.")

if __name__ == "__main__":
   run_server_test()