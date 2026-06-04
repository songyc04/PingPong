import socket
import sys

def main():
   # --- ESP32 고정 IP 및 포트 설정 ---
   ESP32_IP = "192.168.0.207" 
   ESP32_PORT = 10002
   # --------------------------------

   # UDP 소켓 생성 (SOCK_DGRAM)
   udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
   
   print("==================================================")
   print("            UDP 터미널 송신 프로그램 (초고속)        ")
   print(f" ▷ 목적지 고정 IP : {ESP32_IP}")
   print(f" ▷ 목적지 포트    : {ESP32_PORT}")
   print(" ▷ 종료하려면 'exit'를 입력하세요.")
   print("==================================================")

   try:
      while True:
         # 터미널 입력 대기
         message = input("\n[보낼 문자열 입력] -> ")
         
         # 종료 조건
         if message.strip().lower() == 'exit':
               print("[네트워크] 프로그램을 종료합니다.")
               break
               
         if not message:
               continue

         try:
               # 문자열을 바이트 스트림으로 변환하여 UDP 전송
               # UDP는 패킷 자체로 경계가 구분되므로 끝에 '\n'을 붙이지 않아도 됩니다.
               udp_socket.sendto(message.encode('utf-8'), (ESP32_IP, ESP32_PORT))
               print(f" -> 전송 성공! ({len(message)} bytes)")
               
         except Exception as send_error:
               print(f" [!] 전송 중 요류 발생: {send_error}")
               
   except KeyboardInterrupt:
      print("\n[네트워크] 사용자에 의해 강제 종료되었습니다.")
   finally:
      # 소켓 닫기
      udp_socket.close()

if __name__ == "__main__":
    main()