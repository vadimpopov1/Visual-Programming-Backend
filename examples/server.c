#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define PORT 8080

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    char buffer[1024];
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // if (setsockopt(server_fd, SOL_SOCKET,
    //                SO_REUSEADDR | SO_REUSEPORT,
    //                &opt, sizeof(opt))) {
    //     perror("setsockopt");
    //     exit(EXIT_FAILURE);
    // }

    if (setsockopt(server_fd, SOL_SOCKET, 
                SO_REUSEADDR, &opt, sizeof(opt))) { // Особенность системы MacOS что нельзя использовать SO_REUSEPORT (не поддерживается в таком виде)
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address,
             sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Сервер запущен на порту %d\n", PORT);

    while (1) {
        printf("\nОжидание клиента...\n");

        new_socket = accept(server_fd,
                            (struct sockaddr*)&address,
                            &addrlen);

        if (new_socket < 0) {
            perror("accept");
            continue;
        }

        printf("Клиент подключился.\n");

        int count = 0;

        while (1) {
            ssize_t bytes = read(new_socket, buffer, sizeof(buffer) - 1);

            if (bytes <= 0) {
                printf("Клиент отключился.\n");
                close(new_socket);
                break;
            }

            buffer[bytes] = '\0';
            count++;

            printf("[%d] Получено: %s\n", count, buffer);

            send(new_socket, "Hello from server", 17, 0);
        }
    }

    close(server_fd);
    return 0;
}
