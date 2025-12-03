import socket
import time

client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client_socket.connect(('localhost', 12345))

try:
    while True:
        message = "Hello World"
        client_socket.sendall(message.encode())
        time.sleep(1)

except KeyboardInterrupt:
    print("Отключение клиента...")

client_socket.close()
