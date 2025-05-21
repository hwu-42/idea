#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <stdio.h>


// Client structure to store ID, socket FD, message buffer, and linked list pointer
typedef struct s_client {
    int id, fd;           // Client ID and socket file descriptor
    char *buf;            // Buffer for incoming messages
    struct s_client *next;// Pointer to next client
} t_client;



// Global variables for client list, max FD, and FD sets for select()
t_client *g_clients = NULL; // Linked list of connected clients
int max_fd = 0;             // Highest FD for select()
int sockfd;


fd_set read_set, write_set, active_set; // FD sets for reading, writing, and active FDs

void remove_client(t_client **clients, int fd);

void cleanup() {
    while (g_clients) {
        remove_client(&g_clients, g_clients->fd);
    }
    int i = 0;
    while (i < max_fd) {
        if (FD_ISSET(i, &active_set))
            close (i);
        i++;
    }
}

//exit gracefully
void fatal() {
    cleanup();
    write(2, "Fatal error\n", 12);
    close(sockfd);
    exit(1);
}

//when ctrl+c exit gracefully
void signal_handler(int sig) {
    cleanup();
    write(2, "server is turning down\n", 23);
    close(sockfd);
    exit(0);
}

// Add a new client with given FD and ID to the client list
void add_client(int fd, int id) {
    t_client *new = malloc(sizeof(t_client)); // Allocate new client struct
    if (!new) fatal();
    new->id = id;
    new->fd = fd;
    new->buf = NULL;          // Initialize empty message buffer
    new->next = g_clients;    // Link to current client list
    g_clients = new;          // Update head of list
    if (fd > max_fd) max_fd = fd; // Update max FD if needed
    FD_SET(fd, &active_set);  // Add FD to active set for select()
}

// Remove client with given FD from list, free memory, and close FD
void remove_client(t_client **clients, int fd) {
    t_client *curr = *clients, *prev = NULL;
    while (curr) {
        if (curr->fd == fd) {
            if (prev) prev->next = curr->next; // Unlink from list
            else *clients = curr->next;        // Update head if first client
            free(curr->buf);                   // Free message buffer
            free(curr);                        // Free client struct
            FD_CLR(fd, &active_set);           // Remove FD from active set
            close(fd);                         // Close socket
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

// Broadcast message to all clients except sender
void broadcast(char *msg, int sender_fd) {
    for (t_client *c = g_clients; c; c = c->next)
        if (c->fd != sender_fd) // Skip sender
            send(c->fd, msg, strlen(msg), 0); // Send message
}

// Extract one complete message (ending with \n) from buffer
int extract_message(char **buf, char **msg) {
    if (!*buf) return 0;
    int i = 0;
    while ((*buf)[i] && (*buf)[i] != '\n') i++;
    if (!(*buf)[i]) return 0;

    *msg = malloc(i + 2);
    if (!*msg) fatal();
    strncpy(*msg, *buf, i + 1);
    (*msg)[i + 1] = 0;

    char *newbuf = calloc(1, strlen(*buf + i + 1) + 1);
    if (!newbuf) fatal();
    strcpy(newbuf, *buf + i + 1);
    free(*buf);
    *buf = newbuf;
    return 1;
}

// Join two strings, free old buffer, return new buffer
char *str_join(char *buf, char *add) {
    char *newbuf = malloc((buf ? strlen(buf) : 0) + strlen(add) + 1);
    if (!newbuf) fatal();
    newbuf[0] = 0;           // Initialize empty string
    if (buf) strcat(newbuf, buf); // Append old buffer
    strcat(newbuf, add);      // Append new data
    free(buf);                // Free old buffer
    return newbuf;            // Return new buffer
}

int main(int ac, char **av) {
    // Check for exactly one argument (port)
    if (ac != 2) {
        if (ac == 1)
            write(2, "please provide port number\n", 27);
        else
            write(2, "Wrong number of arguments\n", 26);
        exit(1);
    }

    //exit gracefully when killed
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Convert port argument to integer
    int port = atoi(av[1]);
    if (port <= 0) fatal();

    // Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) fatal();

    // Set up server address (127.0.0.1, specified port)
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(2130706433), .sin_port = htons(port) };
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(sockfd, 10) < 0)
        fatal();

    // Initialize select() variables
    max_fd = sockfd;
    FD_ZERO(&active_set);
    FD_SET(sockfd, &active_set);

    int id = 0;               // Next client ID
    char msg[4200];           // Buffer for broadcast messages
    while (1) {
        // Copy active FD sets for select()
        read_set = write_set = active_set;
        if (select(max_fd + 1, &read_set, &write_set, NULL, NULL) < 0)
            continue;

        // Handle new client connection
        if (FD_ISSET(sockfd, &read_set)) {
            struct sockaddr_in cli;
            int len = sizeof(cli);
            int fd = accept(sockfd, (struct sockaddr *)&cli, &len);
            if (fd >= 0) {
                add_client(fd, id); // Add client to list
                sprintf(msg, "server: client %d just arrived\n", id++);
                broadcast(msg, fd); // Announce new client
            }
        }

        // Process data from clients
        for (t_client *c = g_clients, *next; c; c = next) {
            next = c->next; // Store next pointer (client may be removed)
            if (FD_ISSET(c->fd, &read_set)) {
                char buf[4097]; // Buffer for incoming data
                int n = recv(c->fd, buf, 4096, 0);
                if (n <= 0) { // Client disconnected
                    sprintf(msg, "server: client %d just left\n", c->id);
                    broadcast(msg, c->fd);
                    remove_client(&g_clients, c->fd);
                    continue;
                }
                buf[n] = 0; // Null-terminate received data
                c->buf = str_join(c->buf, buf); // Append to client buffer
                char *line;
                while (extract_message(&c->buf, &line)) { // Process complete messages
                    sprintf(msg, "client %d: %s", c->id, line);
                    broadcast(msg, c->fd); // Broadcast client message
                    free(line);            // Free extracted message
                }
            }
        }
    }

    // Cleanup (unreachable due to infinite loop, but included for completeness)
    while (g_clients) remove_client(&g_clients, g_clients->fd);
    close(sockfd);
    return 0;
}