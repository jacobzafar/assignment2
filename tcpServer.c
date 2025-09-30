#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <time.h>

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

int client_socket = -1;

void alarm_handler(int signo) {
    (void)signo;
    if (client_socket != -1) {
        (void)write(client_socket, "ERROR TO\n", 9);
        close(client_socket);
        client_socket = -1;
    }
    _exit(EXIT_FAILURE);
}

int parse_host_port(const char *input, char **host, char **port) {
    const char *colon = strchr(input, ':');
    if (!colon) return -1;
    *host = strndup(input, colon - input);
    if (!*host) return -1;
    *port = strdup(colon + 1);
    if (!*port) {
        free(*host);
        return -1;
    }
    return 0;
}

int setup_tcp_server(const char *host, const char *port) {
    struct addrinfo hints, *res, *p;
    int listenfd = -1, yes = 1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int rv = getaddrinfo(host, port, &hints, &res);
    if (rv != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listenfd < 0) continue;
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (bind(listenfd, p->ai_addr, p->ai_addrlen) < 0) {
            close(listenfd);
            listenfd = -1;
            continue;
        }
        if (listen(listenfd, 10) < 0) {
            close(listenfd);
            listenfd = -1;
            continue;
        }
        break;
    }
    freeaddrinfo(res);
    if (listenfd == -1) {
        fprintf(stderr, "Failed to bind or listen.\n");
    }
    return listenfd;
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

void generate_int_task(int *op, int *v1, int *v2) {
    *op = (rand() % 4) + 1;
    *v1 = (rand() % 100) + 1;
    if (*op == 4) {
        do {
            *v2 = (rand() % 99) + 1;
        } while (*v2 == 0);
    } else {
        *v2 = (rand() % 100) + 1;
    }
}

void handle_client_protocol(int clientfd) {
    char buffer[BUFFER_SIZE];
    ssize_t n;
    srand(time(NULL) ^ getpid());

    alarm(5);
    n = recv(clientfd, buffer, BUFFER_SIZE - 1, 0);
    if (n <= 0) {
        (void)write(clientfd, "ERROR TO\n", 9);
        return;
    }
    alarm(0);
    buffer[n] = '\0';

    if (strncmp(buffer, "TEXT TCP 1.1", 13) == 0) {
        int op, v1, v2, err = 0;
        char opchar;
        generate_int_task(&op, &v1, &v2);
        switch (op) {
            case 1: opchar = '+'; break;
            case 2: opchar = '-'; break;
            case 3: opchar = '*'; break;
            case 4: opchar = '/'; break;
            default: opchar = '?'; break;
        }
        char task[64];
        snprintf(task, sizeof(task), "%d %c %d\n", v1, opchar, v2);
        alarm(5);
        send(clientfd, task, strlen(task), 0);
        alarm(0);
        alarm(5);
        n = recv(clientfd, buffer, BUFFER_SIZE - 1, 0);
        alarm(0);
        if (n <= 0) {
            (void)write(clientfd, "ERROR TO\n", 9);
            return;
        }
        buffer[n] = '\0';
        int answer = atoi(buffer);
        int correct = do_int_op(op, v1, v2, &err);
        if (!err && answer == correct) {
            send(clientfd, "OK\n", 3, 0);
        } else {
            send(clientfd, "NOT OK\n", 7, 0);
        }
        return;
    }

    if (n == sizeof(struct calcProtocol)) {
        struct calcProtocol req, resp;
        memcpy(&req, buffer, sizeof(req));
        req.type = ntohs(req.type);
        req.major_version = ntohs(req.major_version);
        req.minor_version = ntohs(req.minor_version);
        req.id = ntohl(req.id);
        req.arith = ntohl(req.arith);
        req.inValue1 = ntohl(req.inValue1);
        req.inValue2 = ntohl(req.inValue2);
        req.inResult = ntohl(req.inResult);
        memset(&resp, 0, sizeof(resp));
        resp.type = htons(1);
        resp.major_version = htons(1);
        resp.minor_version = htons(1);
        resp.id = htonl(req.id);
        resp.arith = htonl(req.arith);
        resp.inValue1 = htonl(req.inValue1);
        resp.inValue2 = htonl(req.inValue2);
        int err = 0;
        if (req.arith >= 1 && req.arith <= 4) {
            int32_t result = do_int_op(req.arith, req.inValue1, req.inValue2, &err);
            resp.inResult = htonl(result);
            resp.flResult = 0.0;
        } else if (req.arith >= 5 && req.arith <= 8) {
            double result = do_float_op(req.arith, req.flValue1, req.flValue2, &err);
            resp.inResult = 0;
            resp.flResult = result;
        } else {
            err = 1;
        }
        if (err) {
            (void)write(clientfd, "ERROR TO\n", 9);
        } else {
            send(clientfd, &resp, sizeof(resp), 0);
        }
        return;
    }

    (void)write(clientfd, "ERROR TO\n", 9);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <host:port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    char *host = NULL, *port = NULL;
    if (parse_host_port(argv[1], &host, &port) != 0) {
        fprintf(stderr, "Invalid argument format. Use host:port.\n");
        return EXIT_FAILURE;
    }
    int listenfd = setup_tcp_server(host, port);
    if (listenfd < 0) {
        free(host); free(port);
        return EXIT_FAILURE;
    }
    printf("TCP server listening on %s:%s\n", host, port);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = alarm_handler;
    sigaction(SIGALRM, &sa, NULL);
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int clientfd = accept(listenfd, (struct sockaddr *)&client_addr, &addr_size);
        if (clientfd < 0) {
            perror("accept");
            continue;
        }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(clientfd);
            continue;
        } else if (pid == 0) {
            close(listenfd);
            client_socket = clientfd;
            handle_client_protocol(clientfd);
            close(clientfd);
            exit(EXIT_SUCCESS);
        } else {
            close(clientfd);
        }
    }
    free(host);
    free(port);
    close(listenfd);
    return 0;
}
