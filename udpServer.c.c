#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "common.h"

#define MAX_CLIENTS 100

typedef struct {
    struct sockaddr_storage addr;
    socklen_t addr_len;
    Task task;
    time_t last_active;
    int expecting_response;
    int occupied;
} client_info_t;

client_info_t clients[MAX_CLIENTS];

int sockaddr_cmp(struct sockaddr_storage *a, socklen_t a_len,
                 struct sockaddr_storage *b, socklen_t b_len) {
    if (a->ss_family != b->ss_family) return 1;
    if (a_len != b_len) return 1;
    if (a->ss_family == AF_INET) {
        struct sockaddr_in *sa = (struct sockaddr_in*)a;
        struct sockaddr_in *sb = (struct sockaddr_in*)b;
        if (memcmp(&sa->sin_addr, &sb->sin_addr, sizeof(sa->sin_addr)) != 0) return 1;
        if (sa->sin_port != sb->sin_port) return 1;
        return 0;
    } else if (a->ss_family == AF_INET6) {
        struct sockaddr_in6 *sa6 = (struct sockaddr_in6*)a;
        struct sockaddr_in6 *sb6 = (struct sockaddr_in6*)b;
        if (memcmp(&sa6->sin6_addr, &sb6->sin6_addr, sizeof(sa6->sin6_addr)) != 0) return 1;
        if (sa6->sin6_port != sb6->sin6_port) return 1;
        return 0;
    }
    return 1;
}

int find_client(struct sockaddr_storage *addr, socklen_t addr_len) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].occupied &&
            sockaddr_cmp(&clients[i].addr, clients[i].addr_len, addr, addr_len) == 0) {
            return i;
        }
    }
    return -1;
}

int add_client(struct sockaddr_storage *addr, socklen_t addr_len) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].occupied) {
            clients[i].occupied = 1;
            memcpy(&clients[i].addr, addr, addr_len);
            clients[i].addr_len = addr_len;
            clients[i].expecting_response = 0;
            clients[i].last_active = time(NULL);
            return i;
        }
    }
    return -1;
}

void remove_client(int idx) {
    clients[idx].occupied = 0;
}

int setup_udp_socket(const char *host, const char *port) {
    struct addrinfo hints, *res, *rp;
    int sockfd = -1, yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) continue;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sockfd);
    }
    freeaddrinfo(res);
    if (rp == NULL) return -1;
    return sockfd;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: udpServer <IPv4/IPv6/DNS>:<Port>\n");
        exit(1);
    }
    char *host = NULL, *port = NULL;
    char *colon = strchr(argv[1], ':');
    if (!colon) {
        fprintf(stderr, "Invalid argument format, expected <host>:<port>\n");
        exit(1);
    }
    host = strndup(argv[1], colon - argv[1]);
    port = strdup(colon + 1);

    srand(time(NULL));
    memset(clients, 0, sizeof(clients));

    int sockfd = setup_udp_socket(host, port);
    if (sockfd < 0) {
        perror("Failed to set up UDP socket");
        exit(1);
    }
    printf("UDP server listening on %s:%s\n", host, port);

    fd_set read_fds;
    struct timeval tv;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int rv = select(sockfd + 1, &read_fds, NULL, NULL, &tv);
        if (rv < 0) {
            perror("select");
            continue;
        } else if (rv == 0) {
            // Timeout: clean up clients not responding for 10s
            time_t now = time(NULL);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].occupied && clients[i].expecting_response &&
                    now - clients[i].last_active > 10) {
                    printf("Client timed out, removing\n");
                    remove_client(i);
                }
            }
            continue;
        }
        if (FD_ISSET(sockfd, &read_fds)) {
            char buf[MAX_BUFFER_SIZE];
            struct sockaddr_storage client_addr;
            socklen_t addr_len = sizeof(client_addr);
            ssize_t n = recvfrom(sockfd, buf, sizeof(buf) - 1, 0,
                                 (struct sockaddr *)&client_addr, &addr_len);
            if (n < 0) {
                perror("recvfrom");
                continue;
            }
            buf[n] = '\0';

            int idx = find_client(&client_addr, addr_len);
            time_t now = time(NULL);

            if (idx == -1) {
                idx = add_client(&client_addr, addr_len);
                if (idx == -1) {
                    fprintf(stderr, "Too many clients\n");
                    continue;
                }
                if (generate_task(&clients[idx].task) < 0) {
                    remove_client(idx);
                    continue;
                }
                clients[idx].last_active = now;
                clients[idx].expecting_response = 1;

                char task_msg[100];
                snprintf(task_msg, sizeof(task_msg), "%d %c %d\n",
                         clients[idx].task.operand1, clients[idx].task.operator, clients[idx].task.operand2);
                sendto(sockfd, task_msg, strlen(task_msg), 0,
                       (struct sockaddr *)&client_addr, addr_len);
                continue;
            }

            clients[idx].last_active = now;
            if (!clients[idx].expecting_response) {
                const char *msg = "ERROR: response rejected (late or unexpected)\n";
                sendto(sockfd, msg, strlen(msg), 0,
                       (struct sockaddr *)&client_addr, addr_len);
                continue;
            }
            int answer = atoi(buf);
            int correct = calculate_task(&clients[idx].task);
            const char *res_msg = (answer == correct) ? "RESULT: correct\n" : "RESULT: incorrect\n";
            sendto(sockfd, res_msg, strlen(res_msg), 0,
                   (struct sockaddr *)&client_addr, addr_len);
            clients[idx].expecting_response = 0;
            remove_client(idx);
        }
    }

    free(host);
    free(port);
    close(sockfd);
    return 0;
}
