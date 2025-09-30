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
#include "common.h"

#define BACKLOG 10

volatile sig_atomic_t timed_out = 0;

void alarm_handler(int signo) {
    timed_out = 1;
}

void usage() {
    fprintf(stderr, "Usage: tcpServer <IPv4/IPv6/DNS>:<Port>\n");
    exit(1);
}

int parse_host_port(const char *arg, char **host, char **port) {
    char *colon = strchr(arg, ':');
    if (!colon) return -1;
    *host = strndup(arg, colon - arg);
    *port = strdup(colon + 1);
    return 0;
}

int setup_listening_socket(const char *host, const char *port) {
    struct addrinfo hints, *res, *rp;
    int sockfd = -1, yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) continue;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sockfd);
    }
    freeaddrinfo(res);
    if (rp == NULL) return -1;

    if (listen(sockfd, BACKLOG) < 0) {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

void close_client_and_exit(int client_fd) {
    close(client_fd);
    exit(1);
}

void send_error_to(int client_fd) {
    const char *msg = "ERROR TO\n";
    send(client_fd, msg, strlen(msg), 0);
}

int handle_client_protocol(int client_fd, const char *init_msg, ssize_t msg_len);

void child_process(int client_fd) {
    struct sigaction sa;
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    char buf[MAX_BUFFER_SIZE];
    ssize_t nbytes;

    timed_out = 0;
    alarm(5);
    nbytes = recv(client_fd, buf, sizeof(buf) - 1, 0);
    alarm(0);
    if (nbytes <= 0 || timed_out) {
        send_error_to(client_fd);
        close_client_and_exit(client_fd);
    }
    buf[nbytes] = '\0';

    if (handle_client_protocol(client_fd, buf, nbytes) < 0) {
        send_error_to(client_fd);
    }
    close(client_fd);
    exit(0);
}

int handle_client_protocol(int client_fd, const char *init_msg, ssize_t msg_len) {
    if (strncmp(init_msg, "TEXT TCP 1.1", 12) == 0) {
        char buf[MAX_BUFFER_SIZE];
        ssize_t n;
        while (1) {
            timed_out = 0;
            alarm(5);
            n = recv(client_fd, buf, sizeof(buf)-1, 0);
            alarm(0);
            if (n <= 0 || timed_out) break;
            buf[n] = '\0';
            if (send(client_fd, buf, n, 0) < 0) break;
        }
        if (timed_out) return -1;
        return 0;
    } else if (strncmp(init_msg, "BINARY TCP 1.1", 14) == 0) {
        char buf[MAX_BUFFER_SIZE];
        ssize_t n;
        while (1) {
            timed_out = 0;
            alarm(5);
            n = recv(client_fd, buf, sizeof(buf), 0);
            alarm(0);
            if (n <= 0 || timed_out) break;
        }
        if (timed_out) return -1;
        return 0;
    }
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) usage();
    char *host = NULL, *port = NULL;
    if (parse_host_port(argv[1], &host, &port) < 0) {
        fprintf(stderr, "Invalid argument, expected <host>:<port>\n");
        exit(1);
    }
    int listen_fd = setup_listening_socket(host, port);
    if (listen_fd < 0) {
        perror("Failed to set up TCP server socket");
        exit(1);
    }
    printf("TCP server listening on %s:%s\n", host, port);
    free(host);
    free(port);

    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_size);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        pid_t pid = fork();
        if (pid == 0) {
            close(listen_fd);
            child_process(client_fd);
            exit(0);
        } else if (pid > 0) {
            close(client_fd);
        } else {
            perror("fork");
            close(client_fd);
        }
    }
    close(listen_fd);
    return 0;
}
