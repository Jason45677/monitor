import socket

HOST = '0.0.0.0'
PORT = 8888

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind((HOST, PORT))
    s.listen()
    print(f"📡 等待 ESP 發來訊息 on port {PORT}...")

    conn, addr = s.accept()
    with conn:
        print(f"✅ 已連接：{addr}")
        while True:
            data = conn.recv(1024)
            if not data:
                break
            print(f"[ESP] 收到訊息：{data.decode().strip()}")
