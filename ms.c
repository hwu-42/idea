#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct s_client {
    int id;
    int fd;
    char *buffer;
    struct s_client *next;
} t_client;

t_client *g_clients = NULL;
int g_max_fd = 0;
fd_set g_read_set, g_write_set, g_active_set;

void fatal_error(void) {
    write(2, "Fatal error\n", 12);
    exit(1);
}

void add_client(t_client **clients, int fd, int id) {
    t_client *new = malloc(sizeof(t_client));
    if (!new)
        fatal_error();
    new->id = id;
    new->fd = fd;
    new->buffer = NULL;
    new->next = *clients;
    *clients = new;
    if (fd > g_max_fd)
        g_max_fd = fd;
    FD_SET(fd, &g_active_set);
}

t_client *find_client(t_client *clients, int fd) {
    while (clients) {
        if (clients->fd == fd)
            return clients;
        clients = clients->next;
    }
    return NULL;
}

void remove_client(t_client **clients, int fd) {
    t_client *curr = *clients;
    t_client *prev = NULL;
    while (curr) {
        if (curr->fd == fd) {
            if (prev)
                prev->next = curr->next;
            else
                *clients = curr->next;
            free(curr->buffer);
            free(curr);
            FD_CLR(fd, &g_active_set);
            close(fd);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

void broadcast(t_client *clients, char *msg, int sender_fd) {
    while (clients) {
        if (clients->fd != sender_fd && FD_ISSET(clients->fd, &g_write_set)) {
            if (send(clients->fd, msg, strlen(msg), 0) < 0)
                fatal_error();
        }
        clients = clients->next;
    }
}

int extract_message(char **buf, char **msg) {
    char *newbuf;
    int i;

    *msg = NULL;
    if (*buf == NULL)
        return 0;
    i = 0;
    while ((*buf)[i]) {
        if ((*buf)[i] == '\n') {
            newbuf = calloc(1, strlen(*buf + i + 1) + 1);
            if (!newbuf)
                fatal_error();
            strcpy(newbuf, *buf + i + 1);
            *msg = *buf;
            (*msg)[i + 1] = 0;
            *buf = newbuf;
            return 1;
        }
        i++;
    }
    return 0;
}

char *str_join(char *buf, char *add) {
    char *newbuf;
    int len;

    len = (buf ? strlen(buf) : 0);
    newbuf = malloc(len + strlen(add) + 1);
    if (!newbuf)
        fatal_error();
    newbuf[0] = 0;
    if (buf)
        strcat(newbuf, buf);
    strcat(newbuf, add);
    free(buf);
    return newbuf;
}

void handle_client_message(t_client *client) {
    char recv_buf[4097];
    char *msg;
    int ret;

    ret = recv(client->fd, recv_buf, 4096, 0);
    if (ret <= 0) {
        char leave_msg[64];
        sprintf(leave_msg, "server: client %d just left\n", client->id);
        broadcast(g_clients, leave_msg, client->fd);
        remove_client(&g_clients, client->fd);
        return;
    }
    recv_buf[ret] = 0;
    client->buffer = str_join(client->buffer, recv_buf);
    while (extract_message(&client->buffer, &msg)) {
        char *full_msg = malloc(strlen(msg) + 32);
        if (!full_msg)
            fatal_error();
        sprintf(full_msg, "client %d: %s", client->id, msg);
        broadcast(g_clients, full_msg, client->fd);
        free(full_msg);
        free(msg);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        write(2, "Wrong number of arguments\n", 26);
        exit(1);
    }

    int port = atoi(argv[1]);
    if (port <= 0)
        fatal_error();

    int sockfd;
    struct sockaddr_in servaddr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        fatal_error();
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(2130706433); // 127.0.0.1
    servaddr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        fatal_error();
    if (listen(sockfd, 10) < 0)
        fatal_error();

    g_max_fd = sockfd;
    FD_ZERO(&g_active_set);
    FD_SET(sockfd, &g_active_set);

    int next_id = 0;
    printf("server started on port %d\n", port);
    while (1) {
        g_read_set = g_write_set = g_active_set;
        if (select(g_max_fd + 1, &g_read_set, &g_write_set, NULL, NULL) < 0)
            continue;

        if (FD_ISSET(sockfd, &g_read_set)) {
            struct sockaddr_in cli;
            int len = sizeof(cli);
            int connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
            if (connfd >= 0) {
                add_client(&g_clients, connfd, next_id);
                char join_msg[64];
                sprintf(join_msg, "server: client %d just arrived\n", next_id);
                broadcast(g_clients, join_msg, connfd);
                next_id++;
            }
        }

        t_client *curr = g_clients;
        while (curr) {
            int fd = curr->fd;
            curr = curr->next;
            if (FD_ISSET(fd, &g_read_set)) {
                t_client *client = find_client(g_clients, fd);
                if (client)
                    handle_client_message(client);
            }
        }
    }

    while (g_clients) {
        close(g_clients->fd);
        free(g_clients->buffer);
        t_client *tmp = g_clients;
        g_clients = g_clients->next;
        free(tmp);
    }
    close(sockfd);
    return 0;
}