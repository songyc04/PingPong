import socket

HOST = '0.0.0.0'  
PORT = 10001 # 포트 번호 변경

def start_server():
   server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
   server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
   server_socket.bind((HOST, PORT))
   server_socket.listen(1)
   
   print(f"[*] 테스트 서버 시작 (포트: {PORT})")
   print("[*] ESP32 보드의 연결을 기다리는 중...")
   try:
      client_socket, addr = server_socket.accept()
      print(f"[+] ESP32 연결 성공! (IP: {addr[0]})")
      while True:
         data = client_socket.recv(1024)
         if not data:
            print("[-] ESP32와 연결이 끊어졌습니다.")
            break
         message = data.decode('utf-8').strip()
         print(f"[수신] ESP32 -> PC: {message}")
         if message == "TCP/IP":
            response = "OK"
            client_socket.sendall((response + "\n").encode('utf-8'))
            print(f"[송신] PC -> ESP32: {response}")
         else:
            response = "ANOTHER"
            client_socket.sendall((response + "\n").encode('utf-8'))
            print(f"[송신] PC -> ESP32: {response}")
   except Exception as e:
      print(f"[에러] 발생: {e}")
   finally:
      client_socket.close()
      server_socket.close()
      print("[*] 서버가 종료되었습니다.")

if __name__ == "__main__":
   start_server()