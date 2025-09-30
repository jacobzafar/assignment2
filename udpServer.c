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

#ifndef HAVE_STRNLEN
size_t strnlen(const char *s, size_t maxlen) {
    size_t i;
    for (i = 0; i < maxlen && s[i]; ++i);
    return i;
}
#endif
#ifndef HAVE_STRDUP
char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *d = malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}
#endif
#ifndef HAVE_STRNDUP
char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *d = malloc(len + 1);
    if (d) {
        memcpy(d, s, len);
        d[len] = '\0';
    }
    return d;
}
#endif

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

struct __attribute__((__packed__)) calcProtocol {
    uint16_t type;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t id;
    uint32_t arith;
    int32_t inValue1;
    int32_t inValue2;
    int32_t inResult;
    double flValue1;
    double flValue2;
    double flResult;
};

typedef struct {
    struct sockaddr_storage addr;
    socklen_t addr_len;
    struct calcProtocol task;
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

int32_t do_int_op(uint32_t arith, int32_t v1, int32_t v2, int *err) {
    *err = 0;
    switch (arith) {
        case 1: return v1 + v2;
        case 2: return v1 - v2;
        case 3: return v1 * v2;
        case 4:
            if (v2 == 0) { *err = 1; return 0; }
            return v1 / v2;
        default: *err = 1; return 0;
    }
}

double do_float_op(uint32_t arith, double v1, double v2, int *err) {
    *err = 0;
    switch (arith) {
        case 5: return v1 + v2;
        case 6: return v1 - v2;
        case 7: return v1 * v2;
        case 8:
            if (v2 == 0.0) { *err = 1; return 0.0; }
            return v1 / v2;
        default: *err = 1; return 0.0;
    }
}

void generate_int_task(struct calcProtocol *task) {
    int op = (rand() % 4) + 1;
    int v1 = (rand() % 100) + 1;
    int v2;
    if (op == 4) {
        do {
            v2 = (rand() % 99) + 1;
        } while (v2 == 0);
    } else {
        v2 = (rand() % 100) + 1;
    }
    memset(task, 0, sizeof(*task));
    task->type = htons(1);
    task->major_version = htons(1);
    task->minor_version = htons(1);
    task->id = htonl(rand());
    task->arith = htonl(op);
    task->inValue1 = htonl(v1);
    task->inValue2 = htonl(v2);
    task->inResult = 0;
    task->flValue1 = 0.0;
    task->flValue2 = 0.0;
    task->flResult = 0.0;
}

void generate_float_task(struct calcProtocol *task) {
    int op = (rand() % 4) + 5;
    double v1 = ((double)(rand() % 10000)) / 100.0 + 1.0;
    double v2;
    if (op == 8) {
        do {
            v2 = ((double)(rand() % 9900)) / 100.0 + 1.0;
        } while (v2 == 0.0);
    } else {
        v2 = ((double)(rand() % 10000)) / 100.0 + 1.0;
    }
    memset(task, 0, sizeof(*task));
    task->type = htons(1);
    task->major_version = htons(1);
    task->minor_version = htons(1);
    task->id = htonl(rand());
    task->arith = htonl(op);
    task->inValue1 = 0;
    task->inValue2 = 0;
    task->inResult = 0;
    task->flValue1 = v1;
    task->flValue2 = v2;
    task->flResult = 0.0;
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
            char buf[BUFFER_SIZE];
            struct sockaddr_storage client_addr;
            socklen_t addr_len = sizeof(client_addr);
            ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0,
                                 (struct sockaddr *)&client_addr, &addr_len);
            if (n < 0) {
                perror("recvfrom");
                continue;
            }
            int idx = find_client(&client_addr, addr_len);
            time_t now = time(NULL);
            if (n == sizeof(struct calcProtocol)) {
                struct calcProtocol *msg = (struct calcProtocol *)buf;
                if (idx == -1) {
                    idx = add_client(&client_addr, addr_len);
                    if (idx == -1) {
                        fprintf(stderr, "Too many clients\n");
                        continue;
                    }
                    if ((rand() % 2) == 0)
                        generate_int_task(&clients[idx].task);
                    else
                        generate_float_task(&clients[idx].task);
                    clients[idx].last_active = now;
                    clients[idx].expecting_response = 1;
                    sendto(sockfd, &clients[idx].task, sizeof(struct calcProtocol), 0,
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
                int err = 0;
                int32_t correct = 0;
                double fcorrect = 0.0;
                uint32_t arith = ntohl(msg->arith);
                if (arith >= 1 && arith <= 4) {
                    int32_t v1 = ntohl(msg->inValue1);
                    int32_t v2 = ntohl(msg->inValue2);
                    correct = do_int_op(arith, v1, v2, &err);
                    int32_t answer = ntohl(msg->inResult);
                    const char *res_msg = (!err && answer == correct) ? "RESULT: correct\n" : "RESULT: incorrect\n";
                    sendto(sockfd, res_msg, strlen(res_msg), 0,
                           (struct sockaddr *)&client_addr, addr_len);
                } else if (arith >= 5 && arith <= 8) {
                    double v1 = msg->flValue1;
                    double v2 = msg->flValue2;
                    fcorrect = do_float_op(arith, v1, v2, &err);
                    double answer = msg->flResult;
                    const char *res_msg = (!err && answer == fcorrect) ? "RESULT: correct\n" : "RESULT: incorrect\n";
                    sendto(sockfd, res_msg, strlen(res_msg), 0,
                           (struct sockaddr *)&client_addr, addr_len);
                } else {
                    const char *msg = "ERROR: invalid operation\n";
                    sendto(sockfd, msg, strlen(msg), 0,
                           (struct sockaddr *)&client_addr, addr_len);
                }
                clients[idx].expecting_response = 0;
                remove_client(idx);
                continue;
            }
            buf[n] = '\0';
            if (idx == -1) {
                idx = add_client(&client_addr, addr_len);
                if (idx == -1) {
                    fprintf(stderr, "Too many clients\n");
                    continue;
                }
                int op, v1, v2;
                op = (rand() % 4) + 1;
                v1 = (rand() % 100) + 1;
                if (op == 4) {
                    do {
                        v2 = (rand() % 99) + 1;
                    } while (v2 == 0);
                } else {
                    v2 = (rand() % 100) + 1;
                }
                clients[idx].task.arith = op;
                clients[idx].task.inValue1 = v1;
                clients[idx].task.inValue2 = v2;
                clients[idx].last_active = now;
                clients[idx].expecting_response = 1;
                char opchar;
                switch (op) {
                    case 1: opchar = '+'; break;
                    case 2: opchar = '-'; break;
                    case 3: opchar = '*'; break;
                    case 4: opchar = '/'; break;
                    default: opchar = '?'; break;
                }
                char task_msg[100];
                snprintf(task_msg, sizeof(task_msg), "%d %c %d\n", v1, opchar, v2);
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
            int correct = do_int_op(clients[idx].task.arith, clients[idx].task.inValue1, clients[idx].task.inValue2, &(int){0});
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
