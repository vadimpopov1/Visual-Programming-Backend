#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define PORT 8080

int main(int argc, char const* argv[])
{
    int status, valread, client_fd;
    struct sockaddr_in serv_addr;
    char* hello = "Hello World";
    char buffer[1024] = {0};
    int count = 0;

    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\nОшибка создания сокета\n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nНеверный адрес / Адрес не поддерживается\n");
        return -1;
    }

    if ((status = connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0) {
        printf("\nПодключение прервано\n");
        return -1;
    }

    printf("Подключен к серверу\n");

    while (1) {
        if (send(client_fd, hello, strlen(hello), 0) < 0) {
            printf("Сервер отключен или ошибка отправки\n");
            break;
        }

        count++;
        valread = read(client_fd, buffer, 1024 - 1);
        if (valread <= 0) {
            printf("Сервер отключен\n");
            break;
        }
        buffer[valread] = '\0';
        printf("Получено от сервера: %s\n", buffer);

        sleep(1);
    }

    close(client_fd);
    printf("Отключение клиента\n");
    return 0;
}
