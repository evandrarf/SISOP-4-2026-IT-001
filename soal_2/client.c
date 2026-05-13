#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 9000
#define BUFFER_SIZE 4096

static int connect_to_server(const char *host, int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t) port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", host);
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

static int send_all(int sockfd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t written = send(sockfd, buf + sent, len - sent, 0);
        if (written == -1) {
            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        sent += (size_t) written;
    }

    return 0;
}

static int read_response(int sockfd)
{
    char buffer[BUFFER_SIZE];
    bool printed = false;

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = printed ? 0 : 5;
        timeout.tv_usec = printed ? 200000 : 0;

        int ready = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
        if (ready == -1) {
            perror("select");
            return -1;
        }

        if (ready == 0) {
            return printed ? 0 : -1;
        }

        ssize_t received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (received == -1) {
            perror("recv");
            return -1;
        }

        if (received == 0) {
            fprintf(stderr, "Server closed the connection\n");
            return -1;
        }

        buffer[received] = '\0';
        fputs(buffer, stdout);
        printed = true;
    }
}

int main(int argc, char *argv[])
{
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    if (argc >= 2) {
        host = argv[1];
    }

    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
    }

    int sockfd = connect_to_server(host, port);
    if (sockfd == -1) {
        return EXIT_FAILURE;
    }

    printf("Connected to DB Server on %s:%d\n", host, port);
    puts("Type HELP for available commands");
    puts("Type EXIT to quit");

    char input[BUFFER_SIZE];
    while (1) {
        printf("db > ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        if (strcmp(input, "EXIT\n") == 0 || strcmp(input, "EXIT") == 0) {
            break;
        }

        if (send_all(sockfd, input, strlen(input)) == -1) {
            perror("send");
            close(sockfd);
            return EXIT_FAILURE;
        }

        if (read_response(sockfd) == -1) {
            close(sockfd);
            return EXIT_FAILURE;
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
