import socket

server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server_socket.bind(('localhost', 12345))
server_socket.listen(1)

print("Сервер запущен и ожидает входящих подключений...")

while True:
    print("\nОжидание клиента...")
    conn, addr = server_socket.accept()
    print(f"Клиент подключился: {addr}")
    count = 0

    try:
        while True:
            data = conn.recv(1024)
            if not data:
                print("Клиент отключился.")
                break
            count += 1
            print(f"[{count}] Получено: {data.decode()}")

    except ConnectionResetError:
        print("Клиент отключился (обрыв соединения).")

    conn.close()
