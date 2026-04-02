#ifdef REMOTE_DEBUG

#include "http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>

static int server_fd = -1;
static int stop_requested = 0;
static pthread_t server_thread;
static http_handler_t global_handler = NULL;

static void *http_server_thread(void *arg) {
    int port = *(int *)arg;
    free(arg);

    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return NULL;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        return NULL;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return NULL;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return NULL;
    }

    printf("HTTP Server: listening on port %d\n", port);

    while (!stop_requested) {
        struct timeval tv = {1, 0};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server_fd, &fds);

        int ret = select(server_fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (ret == 0) continue;

        int new_socket;
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        char buffer[4096] = {0};
        int valread = read(new_socket, buffer, 4095);
        if (valread > 0) {
            buffer[valread] = 0;
            http_request_t req;
            memset(&req, 0, sizeof(req));
            
            /* Safer parsing */
            char *line = buffer;
            char *method_end = strchr(line, ' ');
            if (method_end) {
                int method_len = method_end - line;
                if (method_len > 15) method_len = 15;
                memcpy(req.method, line, method_len);
                req.method[method_len] = 0;
                
                char *path_start = method_end + 1;
                char *path_end = strchr(path_start, ' ');
                if (path_end) {
                    int path_len = path_end - path_start;
                    if (path_len > 255) path_len = 255;
                    memcpy(req.path, path_start, path_len);
                    req.path[path_len] = 0;
                }
            }

            char *resp_body = NULL;
            int resp_len = 0;
            char content_type[64] = "text/plain";
            
            if (global_handler && req.path[0] != 0) {
                global_handler(&req, &resp_body, &resp_len, content_type);
            } else {
                resp_body = strdup("Invalid Request");
                resp_len = 15;
            }

            char header[512];
            int header_len = sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n", 
                                     content_type, resp_len);
            
            write(new_socket, header, header_len);
            if (resp_body && resp_len > 0) {
                write(new_socket, resp_body, resp_len);
                free(resp_body);
            }
        }
        close(new_socket);
    }

    close(server_fd);
    server_fd = -1;
    return NULL;
}

int http_server_start(int port, http_handler_t handler) {
    global_handler = handler;
    stop_requested = 0;
    int *port_ptr = malloc(sizeof(int));
    *port_ptr = port;
    if (pthread_create(&server_thread, NULL, http_server_thread, port_ptr) != 0) {
        perror("pthread_create");
        free(port_ptr);
        return -1;
    }
    return 0;
}

void http_server_stop(void) {
    stop_requested = 1;
    pthread_join(server_thread, NULL);
}

#endif /* REMOTE_DEBUG */
