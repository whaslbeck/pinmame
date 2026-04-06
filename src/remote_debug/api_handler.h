#ifndef API_HANDLER_H
#define API_HANDLER_H

#include "http_server.h"

typedef void (*api_route_fn)(const http_request_t *, char **, int *, char *);

typedef struct
{
    const char *path;
    api_route_fn handler;
} api_route_t;

static void handle_api_info(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_dmd_info(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_dmd_raw(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_dmd_pnm(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_screenshot_info(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_screenshot_raw(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_screenshot_pnm(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_screenshot_legacy(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_command(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_control_runto(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_control(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_messages(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_callstack(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_breakpoints(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_watchpoints(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_points(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_memory_find(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_memory_fill(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_dasm(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_nvram_dump(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_nvram(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_memory_write(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_state(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_debugger_memory(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_input(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_ui(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
static void handle_api_doc(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);

#endif