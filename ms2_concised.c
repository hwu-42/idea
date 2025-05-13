#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <stdio.h>

typedef struct s_client {
    int id, fd;
    char *buf;
    struct s_client *next;
} t_client;

t_client *g_clients = NULL;
int max_fd = 0;
fd_set read_set, write_set, active_set;

void fatal() {
    write(2, "Fatal error\n", 12);
    exit(1);
}

void add_client(int fd, int id) {
    t_client *new = malloc(sizeof(t_client));
    if (!new) fatal();
    new->id = id;
    new->fd = fd;
    new->buf = NULL;
    new->next = g_clients;
    g_clients = new;
    if (fd > max_fd) max_fd = fd;
    FD_SET(fd, &active_set);
}

void remove_client(t_client **clients, int fd) {
    t_client *curr = *clients, *prev = NULL;
    while (curr) {
        if (curr->fd == fd) {
            if (prev) prev->next = curr->next;
            else *clients = curr->next;
            free(curr->buf);
            free(curr);
            FD_CLR(fd, &active_set);
            close(fd);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

void broadcast(char *msg, int sender_fd) {
    for (t_client *c = g_clients; c; c = c->next)
        if (c->fd != sender_fd)
            send(c->fd, msg, strlen(msg), 0);
}

int extract_message(char **buf, char **msg) {
    if (!*buf) return 0;
    int i = 0;
    while ((*buf)[i] && (*buf)[i] != '\n') i++;
    if (!(*buf)[i]) return 0;
    *msg = *buf;
    (*msg)[i + 1] = 0;
    char *newbuf = calloc(1, strlen(*buf + i + 1) + 1);
    if (!newbuf) fatal();
    strcpy(newbuf, *buf + i + 1);
    *buf = newbuf;
    return 1;
}

char *str_join(char *buf, char *add) {
    char *newbuf = malloc((buf ? strlen(buf) : 0) + strlen(add) + 1);
    if (!newbuf) fatal();
    newbuf[0] = 0;
    if (buf) strcat(newbuf, buf);
    strcat(newbuf, add);
    free(buf);
    return newbuf;
}

int main(int ac, char **av) {
    if (ac != 2) {
        write(2, "Wrong number of arguments\n", 26);
        exit(1);
    }

    int port = atoi(av[1]);
    if (port <= 0) fatal();

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) fatal();

    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(2130706433), .sin_port = htons(port) };
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(sockfd, 10) < 0) fatal();

    max_fd = sockfd;
    FD_ZERO(&active_set);
    FD_SET(sockfd, &active_set);

    int id = 0;
    char msg[4200];
    while (1) {
        read_set = write_set = active_set;
        if (select(max_fd + 1, &read_set, &write_set, NULL, NULL) < 0) continue;

        if (FD_ISSET(sockfd, &read_set)) {
            struct sockaddr_in cli;
            int len = sizeof(cli);
            int fd = accept(sockfd, (struct sockaddr *)&cli, &len);
            if (fd >= 0) {
                add_client(fd, id);
                sprintf(msg, "server: client %d just arrived\n", id++);
                broadcast(msg, fd);
            }
        }

        for (t_client *c = g_clients, *next; c; c = next) {
            next = c->next;
            if (FD_ISSET(c->fd, &read_set)) {
                char buf[4097];
                int n = recv(c->fd, buf, 4096, 0);
                if (n <= 0) {
                    sprintf(msg, "server: client %d just left\n", c->id);
                    broadcast(msg, c->fd);
                    remove_client(&g_clients, c->fd);
                    continue;
                }
                buf[n] = 0;
                c->buf = str_join(c->buf, buf);
                char *line;
                while (extract_message(&c->buf, &line)) {
                    sprintf(msg, "client %d: %s", c->id, line);
                    broadcast(msg, c->fd);
                    free(line);
                }
            }
        }
    }

    while (g_clients) remove_client(&g_clients, g_clients->fd);
    close(sockfd);
    return 0;
}