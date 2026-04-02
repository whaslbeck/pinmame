#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

typedef struct http_request {
    char method[16];
    char path[256];
    char *body;
    int body_len;
} http_request_t;

typedef void (*http_handler_t)(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);

int http_server_start(int port, http_handler_t handler);
void http_server_stop(void);

#endif /* HTTP_SERVER_H */
