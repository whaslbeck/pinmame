#ifdef REMOTE_DEBUG

#include "api_handler.h"
#include "remote_debug.h"
#include "http_server.h"
#include "driver.h"
#include "mame.h"
#include "wpc/core.h"
#include "wpc/wpc.h"
#include "cpuintrf.h"
#include "cpu/m6809/m6809.h"
#include "cpu/adsp2100/adsp2100.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern void remote_debug_get_screenshot(char **buffer, int *len, char *content_type);
extern void remote_debug_get_dmd_screenshot(char **buffer, int *len, char *content_type);
extern void remote_debug_set_paused(int paused);
extern void remote_debug_step(void);
extern int remote_debug_is_paused(void);
extern void remote_debug_lock(void);
extern void remote_debug_unlock(void);
extern int wpc_get_bank(void);
extern void remote_debug_quit(void);
extern void remote_debug_breakpoint_add(UINT32 adr);
extern void remote_debug_breakpoint_add_banked(UINT32 adr, int bank);
extern void remote_debug_breakpoint_clear(void);
extern void remote_debug_breakpoint_toggle(int index);
extern void remote_debug_breakpoint_delete(int index);
extern void remote_debug_watchpoint_add(UINT32 adr, int mode);
extern void remote_debug_watchpoint_clear(void);
extern void remote_debug_watchpoint_toggle(int index);
extern void remote_debug_watchpoint_delete(int index);
extern void remote_debug_get_points(char **buffer, int *len);
extern void remote_debug_get_messages(char **buffer, int *len);
extern void remote_debug_get_callstack(char **buffer, int *len);
extern void remote_debug_add_message(const char *msg);
extern void remote_debug_memory_fill(int cpu, UINT32 addr, int size, UINT8 val);
extern void remote_debug_set_register(int cpu, int reg, UINT32 val);
extern int remote_debug_memory_find(int cpu, UINT32 addr, int size, const UINT8 *pat, int len, UINT32 *found);
extern void remote_debug_step_over(void);
extern void remote_debug_run_to(UINT32 addr);
extern void core_get_dmd_data(int layout_idx, float **pixels, int *width, int *height);

extern unsigned activecpu_dasm(char *buffer, unsigned pc);

static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src)
    {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b)))
        {
            if (a >= 'a')
                a -= 'a' - 'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a' - 'A';
            if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        }
        else if (*src == '+')
        {
            *dst++ = ' ';
            src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void get_query_param(const char *path, const char *key, char *dest, int max_len)
{
    char search_key[128];
    sprintf(search_key, "%s=", key);
    const char *p = strstr(path, search_key);
    if (p)
    {
        p += strlen(search_key);
        const char *end = strchr(p, '&');
        int len = end ? (end - p) : strlen(p);
        if (len >= max_len)
            len = max_len - 1;
        memcpy(dest, p, len);
        dest[len] = 0;
    }
    else
    {
        dest[0] = 0;
    }
}

static int resolve_register_id(const char *name)
{
    if (name[0] == '\0')
        return -1;

    /* If it's a number, return it directly */
    if (isdigit(name[0]))
        return atoi(name);

    /* Map common register names (M6809 focus) */
    if (strcasecmp(name, "PC") == 0) return 1;
    if (strcasecmp(name, "S") == 0 || strcasecmp(name, "SP") == 0) return 2;
    if (strcasecmp(name, "CC") == 0 || strcasecmp(name, "FLAGS") == 0) return 3;
    if (strcasecmp(name, "A") == 0) return 4;
    if (strcasecmp(name, "B") == 0) return 5;
    if (strcasecmp(name, "U") == 0) return 6;
    if (strcasecmp(name, "X") == 0) return 7;
    if (strcasecmp(name, "Y") == 0) return 8;
    if (strcasecmp(name, "DP") == 0) return 9;

    return -1;
}

static UINT32 parse_addr(const char *str)
{
    if (!str || !*str)
        return 0;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
        return strtoul(str + 2, NULL, 16);
    return strtoul(str, NULL, 16);
}

static void get_cpu_registers(int i, char **p)
{
    int type = Machine->drv->cpu[i].cpu_type;
    *p += sprintf(*p, "\"type\": %d, \"pc\": %u, \"sp\": %u", type, cpunum_get_reg(i, REG_PC), cpunum_get_reg(i, REG_SP));
    if (type == CPU_M6809)
    {
        *p += sprintf(*p, ", \"a\": %u, \"b\": %u, \"x\": %u, \"y\": %u, \"u\": %u, \"dp\": %u, \"cc\": %u",
                      cpunum_get_reg(i, M6809_A), cpunum_get_reg(i, M6809_B),
                      cpunum_get_reg(i, M6809_X), cpunum_get_reg(i, M6809_Y),
                      cpunum_get_reg(i, M6809_U), cpunum_get_reg(i, M6809_DP),
                      cpunum_get_reg(i, M6809_CC));
    }
    else if (type == CPU_ADSP2105)
    {
        *p += sprintf(*p, ", \"ax0\": %u, \"ax1\": %u, \"ay0\": %u, \"ay1\": %u, \"ar\": %u, \"cntr\": %u, \"astat\": %u",
                      cpunum_get_reg(i, ADSP2100_AX0), cpunum_get_reg(i, ADSP2100_AX1),
                      cpunum_get_reg(i, ADSP2100_AY0), cpunum_get_reg(i, ADSP2100_AY1),
                      cpunum_get_reg(i, ADSP2100_AR), cpunum_get_reg(i, ADSP2100_CNTR),
                      cpunum_get_reg(i, ADSP2100_ASTAT));
    }
}

static void handle_classic_command(const char *cmd_line_encoded)
{
    char decoded[256];
    url_decode(decoded, cmd_line_encoded);
    char clean[256];
    strncpy(clean, decoded, 255);
    clean[255] = 0;
    char *p = clean;
    while (*p)
    {
        *p = toupper(*p);
        p++;
    }
    char *cmd = strtok(clean, " ,");
    if (!cmd)
        return;
    if (strcmp(cmd, "BP") == 0)
    {
        char *as = strtok(NULL, " ,");
        if (as)
        {
            char *colon = strchr(as, ':');
            if (colon)
            {
                *colon = 0;
                remote_debug_breakpoint_add_banked(parse_addr(colon + 1), (int)parse_addr(as));
            }
            else
            {
                remote_debug_breakpoint_add(parse_addr(as));
            }
        }
    }
    else if (strcmp(cmd, "BC") == 0)
    {
        remote_debug_breakpoint_clear();
    }
    else if (strcmp(cmd, "WP") == 0)
    {
        char *as = strtok(NULL, " ,");
        char *sz = strtok(NULL, " ,");
        char *md = strtok(NULL, " ,");
        if (as)
        {
            int mode = 3;
            if (md)
            {
                if (strstr(md, "RW"))
                    mode = 3;
                else if (strchr(md, 'R'))
                    mode = 1;
                else if (strchr(md, 'W'))
                    mode = 2;
            }
            remote_debug_watchpoint_add(parse_addr(as), mode);
        }
    }
    else if (strcmp(cmd, "WC") == 0)
    {
        remote_debug_watchpoint_clear();
    }
    else if (strcmp(cmd, "G") == 0)
    {
        remote_debug_set_paused(0);
    }
    else if (strcmp(cmd, "S") == 0)
    {
        remote_debug_step();
    }
    else if (strcmp(cmd, "F") == 0)
    {
        char *as = strtok(NULL, " ,");
        char *sz = strtok(NULL, " ,");
        char *vl = strtok(NULL, " ,");
        if (as && sz && vl)
            remote_debug_memory_fill(0, parse_addr(as), (int)parse_addr(sz), (UINT8)parse_addr(vl));
    }
    else if (strcmp(cmd, "QUIT") == 0)
    {
        remote_debug_quit();
    }
    else if (strcmp(cmd, "HELP") == 0)
    {
        remote_debug_add_message("Commands: BP [bank]:[addr], BC, WP [addr],[len],[type], WC, G, S, F [addr],[len],[val], QUIT");
    }
    else
    {
        char err[128];
        sprintf(err, "Unknown command: %s", cmd);
        remote_debug_add_message(err);
    }
}

static void set_json_response(const char *json, char **resp_body, int *resp_len, char *content_type)
{
    *resp_body = strdup(json);
    *resp_len = strlen(json);
    strcpy(content_type, "application/json");
}


static const api_route_t api_routes[] = {
    {"/api/debugger/control/runto", handle_api_debugger_control_runto},
    {"/api/debugger/memory/write", handle_api_debugger_memory_write},
    {"/api/debugger/memory/find", handle_api_debugger_memory_find},
    {"/api/debugger/memory/fill", handle_api_debugger_memory_fill},
    {"/api/debugger/nvram/dump", handle_api_debugger_nvram_dump},
    {"/api/debugger/breakpoints", handle_api_debugger_breakpoints},
    {"/api/debugger/watchpoints", handle_api_debugger_watchpoints},
    {"/api/debugger/callstack", handle_api_debugger_callstack},
    {"/api/debugger/messages", handle_api_debugger_messages},
    {"/api/debugger/command", handle_api_debugger_command},
    {"/api/debugger/control", handle_api_debugger_control},
    {"/api/debugger/nvram", handle_api_debugger_nvram},
    {"/api/debugger/points", handle_api_debugger_points},
    {"/api/debugger/dasm", handle_api_debugger_dasm},
    {"/api/debugger/state/write", handle_api_debugger_state_write},
    {"/api/debugger/state", handle_api_debugger_state},
    {"/api/debugger/memory", handle_api_debugger_memory},
    {"/api/screenshot/info", handle_api_screenshot_info},
    {"/api/screenshot/raw", handle_api_screenshot_raw},
    {"/api/screenshot/pnm", handle_api_screenshot_pnm},
    {"/api/screenshot", handle_api_screenshot_legacy},
    {"/api/dmd/info", handle_api_dmd_info},
    {"/api/dmd/raw", handle_api_dmd_raw},
    {"/api/dmd/pnm", handle_api_dmd_pnm},
    {"/api/input", handle_api_input},
    {"/ui", handle_ui},
    {"/api/doc", handle_api_doc},
    {"/api/info", handle_api_info},
};

static int dispatch_api_route(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    for (size_t i = 0; i < sizeof(api_routes) / sizeof(api_routes[0]); i++)
    {
        size_t len = strlen(api_routes[i].path);
        if (strncmp(req->path, api_routes[i].path, len) == 0)
        {
            api_routes[i].handler(req, resp_body, resp_len, content_type);
            return 1;
        }
    }
    return 0;
}

void api_handler(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    if (!remote_debug_ready)
    {
        set_json_response("{\"status\": \"not_ready\"}", resp_body, resp_len, content_type);
        return;
    }

    if (!dispatch_api_route(req, resp_body, resp_len, content_type))
    {
        set_json_response("{\"error\": \"not_found\"}", resp_body, resp_len, content_type);
    }
}

static void handle_api_info(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    strcpy(content_type, "application/json");
    char *buffer = malloc(8192);
    remote_debug_lock();
    if (Machine && Machine->gamedrv)
    {
        int bank = wpc_get_bank();
        char lamp_hex[CORE_MAXLAMPCOL * 2 + 1];
        for (int i = 0; i < CORE_MAXLAMPCOL; i++)
            sprintf(lamp_hex + i * 2, "%02X", coreGlobals.lampMatrix[i]);
        char sw_hex[CORE_MAXSWCOL * 2 + 1];
        for (int i = 0; i < CORE_MAXSWCOL; i++)
            sprintf(sw_hex + i * 2, "%02X", coreGlobals.swMatrix[i]);
        char seg_hex[CORE_SEGCOUNT * 4 + 1];
        for (int i = 0; i < CORE_SEGCOUNT; i++)
            sprintf(seg_hex + i * 4, "%04X", coreGlobals.segments[i].w);
        int ded = (wpc_data) ? wpc_data[0] : 0;
        int len = sprintf(buffer, "{\"game\": \"%s\", \"description\": \"%s\", \"manufacturer\": \"%s\", \"year\": \"%s\", \"paused\": %d, \"wpc_bank\": %d, \"lamps\": \"%s\", \"switches\": \"%s\", \"segments\": \"%s\", \"dedicated\": %d, \"solenoids\": %u, \"solenoids2\": %u}",
                          Machine->gamedrv->name, Machine->gamedrv->description, Machine->gamedrv->manufacturer, Machine->gamedrv->year,
                          remote_debug_is_paused(), bank, lamp_hex, sw_hex, seg_hex, ded, coreGlobals.solenoids, coreGlobals.solenoids2);
        *resp_body = buffer;
        *resp_len = len;
    }
    else
    {
        *resp_body = strdup("{\"status\": \"initializing\"}");
        *resp_len = 27;
    }
    remote_debug_unlock();
}

static void handle_api_dmd_info(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    strcpy(content_type, "application/json");
    float *px;
    int w, h;
    remote_debug_lock();
    core_get_dmd_data(0, &px, &w, &h);
    if (px && w > 0 && h > 0)
    {
        char res[128];
        int len = sprintf(res, "{\"width\": %d, \"height\": %d}", w, h);
        *resp_body = strdup(res);
        *resp_len = len;
    }
    else
    {
        *resp_body = strdup("{\"error\": \"DMD not initialized\"}");
        *resp_len = 32;
    }
    remote_debug_unlock();
}

static void handle_api_dmd_raw(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    remote_debug_get_dmd_screenshot(resp_body, resp_len, content_type);
}

static void handle_api_dmd_pnm(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    extern void remote_debug_get_dmd_pnm(char **buffer, int *len, char *content_type);
    remote_debug_get_dmd_pnm(resp_body, resp_len, content_type);
}

static void handle_api_screenshot_info(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    extern void remote_debug_get_screenshot_info(int *w, int *h);
    int w, h;
    remote_debug_get_screenshot_info(&w, &h);
    char res[128];
    int len = sprintf(res, "{\"width\": %d, \"height\": %d}", w, h);
    *resp_body = strdup(res);
    *resp_len = len;
    strcpy(content_type, "application/json");
}

static void handle_api_screenshot_raw(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    extern void remote_debug_get_raw_screenshot(char **buffer, int *len);
    remote_debug_get_raw_screenshot(resp_body, resp_len);
    if (*resp_body)
    {
        strcpy(content_type, "application/octet-stream");
    }
    else
    {
        *resp_body = strdup("{\"error\":\"No display\"}");
        *resp_len = 22;
        strcpy(content_type, "application/json");
    }
}

static void handle_api_screenshot_pnm(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    remote_debug_get_screenshot(resp_body, resp_len, content_type);
}

static void handle_api_screenshot_legacy(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    remote_debug_get_screenshot(resp_body, resp_len, content_type);
}

static void handle_api_debugger_command(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char cmd_buf[256];
    get_query_param(req->path, "cmd", cmd_buf, 256);
    if (cmd_buf[0])
        handle_classic_command(cmd_buf);
    set_json_response("{\"status\": \"ok\"}", resp_body, resp_len, content_type);
}

static void handle_api_debugger_control_runto(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char addr_buf[32];
    get_query_param(req->path, "addr", addr_buf, 32);
    remote_debug_run_to(parse_addr(addr_buf));
    set_json_response("{\"status\": \"ok\"}", resp_body, resp_len, content_type);
}

static void handle_api_debugger_control(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char cmd_buf[32];
    get_query_param(req->path, "cmd", cmd_buf, 32);
    if (strcmp(cmd_buf, "pause") == 0)
        remote_debug_set_paused(1);
    else if (strcmp(cmd_buf, "resume") == 0)
        remote_debug_set_paused(0);
    else if (strcmp(cmd_buf, "step") == 0)
        remote_debug_step();
    else if (strcmp(cmd_buf, "exit") == 0)
        remote_debug_quit();
    else if (strcmp(cmd_buf, "stepover") == 0)
        remote_debug_step_over();
    set_json_response("{\"status\": \"ok\"}", resp_body, resp_len, content_type);
}

static void handle_api_debugger_messages(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    strcpy(content_type, "application/json");
    remote_debug_get_messages(resp_body, resp_len);
}

static void handle_api_debugger_callstack(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    strcpy(content_type, "application/json");
    remote_debug_get_callstack(resp_body, resp_len);
}

static void handle_api_debugger_breakpoints(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char cmd_buf[32], addr_buf[32], bank_buf[32];
    get_query_param(req->path, "cmd", cmd_buf, 32);
    get_query_param(req->path, "addr", addr_buf, 32);
    get_query_param(req->path, "bank", bank_buf, 32);
    if (strcmp(cmd_buf, "add") == 0)
    {
        if (bank_buf[0])
            remote_debug_breakpoint_add_banked(parse_addr(addr_buf), atoi(bank_buf));
        else
            remote_debug_breakpoint_add(parse_addr(addr_buf));
    }
    else if (strcmp(cmd_buf, "clear") == 0)
        remote_debug_breakpoint_clear();
    set_json_response("{\"status\": \"ok\"}", resp_body, resp_len, content_type);
}

static void handle_api_debugger_watchpoints(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char cmd_buf[32], addr_buf[32], mode_buf[32];
    get_query_param(req->path, "cmd", cmd_buf, 32);
    get_query_param(req->path, "addr", addr_buf, 32);
    get_query_param(req->path, "mode", mode_buf, 32);
    if (strcmp(cmd_buf, "add") == 0)
        remote_debug_watchpoint_add(parse_addr(addr_buf), atoi(mode_buf));
    else if (strcmp(cmd_buf, "clear") == 0)
        remote_debug_watchpoint_clear();
    set_json_response("{\"status\": \"ok\"}", resp_body, resp_len, content_type);
}

static void handle_api_debugger_points(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char cmd_buf[32], type_buf[32], idx_buf[32];
    get_query_param(req->path, "cmd", cmd_buf, 32);
    get_query_param(req->path, "type", type_buf, 32);
    get_query_param(req->path, "idx", idx_buf, 32);
    if (cmd_buf[0])
    {
        int idx = atoi(idx_buf);
        if (strcmp(type_buf, "bp") == 0)
        {
            if (strcmp(cmd_buf, "toggle") == 0)
                remote_debug_breakpoint_toggle(idx);
            else if (strcmp(cmd_buf, "delete") == 0)
                remote_debug_breakpoint_delete(idx);
        }
        else
        {
            if (strcmp(cmd_buf, "toggle") == 0)
                remote_debug_watchpoint_toggle(idx);
            else if (strcmp(cmd_buf, "delete") == 0)
                remote_debug_watchpoint_delete(idx);
        }
    }
    strcpy(content_type, "application/json");
    remote_debug_get_points(resp_body, resp_len);
}

static void handle_api_debugger_memory_find(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char addr_buf[32], size_buf[32], pat_buf[256], cpu_buf[32];
    get_query_param(req->path, "addr", addr_buf, 32);
    get_query_param(req->path, "size", size_buf, 32);
    get_query_param(req->path, "pattern", pat_buf, 256);
    get_query_param(req->path, "cpu", cpu_buf, 32);
    UINT32 addr = parse_addr(addr_buf);
    int size = size_buf[0] ? (int)parse_addr(size_buf) : 0x2000;
    int cpu = cpu_buf[0] ? atoi(cpu_buf) : 0;
    UINT8 pattern[128];
    int pat_len = 0;
    for (int i = 0; i < (int)strlen(pat_buf) && pat_len < 128; i += 2)
    {
        char hex[3] = {pat_buf[i], pat_buf[i + 1], 0};
        pattern[pat_len++] = (UINT8)strtoul(hex, NULL, 16);
    }
    UINT32 found = 0;
    if (remote_debug_memory_find(cpu, addr, size, pattern, pat_len, &found))
    {
        char res[64];
        int len = sprintf(res, "{\"status\": \"ok\", \"found\": %u}", found);
        *resp_body = strdup(res);
        *resp_len = len;
    }
    else
    {
        *resp_body = strdup("{\"status\": \"not_found\"}");
        *resp_len = 23;
    }
    strcpy(content_type, "application/json");
}

static void handle_api_debugger_memory_fill(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char addr_buf[32], size_buf[32], val_buf[32], cpu_buf[32];
    get_query_param(req->path, "addr", addr_buf, 32);
    get_query_param(req->path, "size", size_buf, 32);
    get_query_param(req->path, "val", val_buf, 32);
    get_query_param(req->path, "cpu", cpu_buf, 32);
    remote_debug_memory_fill(atoi(cpu_buf), parse_addr(addr_buf), (int)parse_addr(size_buf), (UINT8)parse_addr(val_buf));
    set_json_response("{\"status\": \"ok\"}", resp_body, resp_len, content_type);
}

static void handle_api_debugger_dasm(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char addr_buf[32], lines_buf[32], cpu_buf[32], bank_buf[32];
    get_query_param(req->path, "addr", addr_buf, 32);
    get_query_param(req->path, "lines", lines_buf, 32);
    get_query_param(req->path, "cpu", cpu_buf, 32);
    get_query_param(req->path, "bank", bank_buf, 32);
    UINT32 addr = parse_addr(addr_buf);
    int lines = lines_buf[0] ? atoi(lines_buf) : 10;
    int cpu_idx = cpu_buf[0] ? atoi(cpu_buf) : 0;
    int bank = bank_buf[0] ? atoi(bank_buf) : -1;
    if (lines > 100)
        lines = 100;
    strcpy(content_type, "application/json");
    char *buf = malloc(lines * 128 + 256);
    char *ptr = buf;
    ptr += sprintf(ptr, "{\"cpu\": %d, \"bank\": %d, \"lines\": [", cpu_idx, bank);
    remote_debug_lock();
    if (Machine && cpu_idx < cpu_gettotalcpu())
    {
        cpuintrf_push_context(cpu_idx);
        int old_bank = -1;
        if (bank != -1 && Machine->drv->cpu[cpu_idx].cpu_type == CPU_M6809)
        {
            old_bank = wpc_get_bank();
            cpu_setbank(1, memory_region(WPC_ROMREGION) + bank * 0x4000);
        }
        for (int i = 0; i < lines; i++)
        {
            char dasm_buf[64];
            activecpu_set_op_base(addr);
            unsigned size = activecpu_dasm(dasm_buf, addr);
            if (size == 0)
                size = 1;
            if (i > 0)
                ptr += sprintf(ptr, ",");
            ptr += sprintf(ptr, "{\"addr\": %u, \"size\": %u, \"text\": \"%s\"}", addr, size, dasm_buf);
            addr += size;
        }
        if (old_bank != -1)
        {
            cpu_setbank(1, memory_region(WPC_ROMREGION) + old_bank * 0x4000);
        }
        cpuintrf_pop_context();
    }
    remote_debug_unlock();
    ptr += sprintf(ptr, "]}");
    *resp_body = buf;
    *resp_len = ptr - buf;
}

static void handle_api_debugger_nvram_dump(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    extern UINT8 *wpc_ram;
    strcpy(content_type, "application/octet-stream");
    remote_debug_lock();
    if (Machine && Machine->drv && Machine->drv->nvram_handler)
    {
        if (wpc_ram)
        {
            *resp_body = malloc(0x2000);
            memcpy(*resp_body, wpc_ram, 0x2000);
            *resp_len = 0x2000;
        }
        else
        {
            *resp_body = strdup("No WPC RAM");
            *resp_len = 10;
        }
    }
    remote_debug_unlock();
}

static void handle_api_debugger_nvram(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char cmd_buf[32];
    get_query_param(req->path, "cmd", cmd_buf, 32);
    if (cmd_buf[0] && strcmp(cmd_buf, "clear") == 0)
    {
        remote_debug_lock();
        if (Machine && Machine->drv && Machine->drv->nvram_handler)
        {
            Machine->drv->nvram_handler(NULL, 0);
            set_json_response("{\"status\": \"ok\"}", resp_body, resp_len, content_type);
        }
        else
        {
            set_json_response("{\"status\": \"error\"}", resp_body, resp_len, content_type);
        }
        remote_debug_unlock();
    }
    else
    {
        set_json_response("{\"status\": \"error\"}", resp_body, resp_len, content_type);
    }
}

static void handle_api_debugger_memory_write(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char addr_buf[32], val_buf[32], cpu_buf[32];
    get_query_param(req->path, "addr", addr_buf, 32);
    get_query_param(req->path, "val", val_buf, 32);
    get_query_param(req->path, "cpu", cpu_buf, 32);
    if (addr_buf[0] && val_buf[0])
    {
        UINT32 addr = parse_addr(addr_buf);
        int val = (int)parse_addr(val_buf);
        int cpu_idx = cpu_buf[0] ? atoi(cpu_buf) : 0;
        remote_debug_lock();
        if (Machine && cpu_idx < cpu_gettotalcpu())
        {
            cpunum_write_byte(cpu_idx, addr, val);
            set_json_response("{\"status\": \"ok\"}", resp_body, resp_len, content_type);
        }
        else
        {
            set_json_response("{\"status\": \"error\"}", resp_body, resp_len, content_type);
        }
        remote_debug_unlock();
    }
    else
    {
        set_json_response("{\"status\": \"error\", \"message\": \"missing params\"}", resp_body, resp_len, content_type);
    }
}

static void handle_api_debugger_state_write(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char cpu_buf[32], reg_buf[32], val_buf[32];
    get_query_param(req->path, "reg", reg_buf, 32);
    get_query_param(req->path, "val", val_buf, 32);

    if (reg_buf[0] != '\0' && val_buf[0] != '\0')
    {
        int cpu_idx = 0;
        get_query_param(req->path, "cpu", cpu_buf, 32);
        if (cpu_buf[0] != '\0')
            cpu_idx = atoi(cpu_buf);

        int reg_id = resolve_register_id(reg_buf);
        if (reg_id != -1)
        {
            UINT32 val = (UINT32)parse_addr(val_buf);
            remote_debug_set_register(cpu_idx, reg_id, val);
            set_json_response("{\"status\": \"ok\"}", resp_body, resp_len, content_type);
        }
        else
        {
            set_json_response("{\"status\": \"error\", \"message\": \"invalid register name/id\"}", resp_body, resp_len, content_type);
        }
    }
    else
    {
        set_json_response("{\"status\": \"error\", \"message\": \"missing params (reg, val)\"}", resp_body, resp_len, content_type);
    }
}

static void handle_api_debugger_state(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    strcpy(content_type, "application/json");
    char *buf = malloc(8192);
    char *ptr = buf;
    ptr += sprintf(ptr, "{\"cpus\": [");
    remote_debug_lock();
    if (Machine)
    {
        for (int i = 0; i < cpu_gettotalcpu(); i++)
        {
            if (i > 0)
                ptr += sprintf(ptr, ",");
            ptr += sprintf(ptr, "{\"id\": %d, ", i);
            cpuintrf_push_context(i);
            get_cpu_registers(i, &ptr);
            cpuintrf_pop_context();
            ptr += sprintf(ptr, "}");
        }
    }
    remote_debug_unlock();
    ptr += sprintf(ptr, "]}");
    *resp_body = buf;
    *resp_len = ptr - buf;
}

static void handle_api_debugger_memory(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char addr_buf[32], size_buf[32], cpu_buf[32];
    get_query_param(req->path, "addr", addr_buf, 32);
    get_query_param(req->path, "size", size_buf, 32);
    get_query_param(req->path, "cpu", cpu_buf, 32);
    UINT32 addr = parse_addr(addr_buf);
    int size = size_buf[0] ? atoi(size_buf) : 16;
    int cpu_idx = cpu_buf[0] ? atoi(cpu_buf) : 0;
    if (size > 2048)
        size = 2048;
    strcpy(content_type, "application/json");
    char *buf = malloc(size * 4 + 256);
    char *ptr = buf;
    ptr += sprintf(ptr, "{\"addr\": %u, \"cpu\": %d, \"data\": [", addr, cpu_idx);
    remote_debug_lock();
    if (Machine && cpu_idx < cpu_gettotalcpu())
    {
        for (int i = 0; i < size; i++)
        {
            if (i > 0)
                ptr += sprintf(ptr, ",");
            ptr += sprintf(ptr, "%u", (unsigned int)cpunum_read_byte(cpu_idx, addr + i));
        }
    }
    remote_debug_unlock();
    ptr += sprintf(ptr, "]}");
    *resp_body = buf;
    *resp_len = ptr - buf;
}

static void handle_api_input(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    char sw_buf[32], val_buf[32];
    get_query_param(req->path, "sw", sw_buf, 32);
    get_query_param(req->path, "val", val_buf, 32);
    if (sw_buf[0] && val_buf[0])
    {
        int sw = atoi(sw_buf);
        int val = atoi(val_buf);
        remote_debug_lock();
        if (coreData)
        {
            core_setSw(sw, val);
            set_json_response("{\"status\": \"ok\"}", resp_body, resp_len, content_type);
        }
        else
        {
            set_json_response("{\"status\": \"error\"}", resp_body, resp_len, content_type);
        }
        remote_debug_unlock();
    }
    else
    {
        set_json_response("{\"status\": \"error\", \"message\": \"missing params\"}", resp_body, resp_len, content_type);
    }
}

static void handle_ui(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
// ui_html.h is generated from ui.html by the makefile, it contains the HTML content as a C string and its length in bytes
#include "remote_debug/ui_html.h"
    strcpy(content_type, "text/html");
    *resp_body = strdup((const char *)src_remote_debug_ui_html);
    *resp_len = src_remote_debug_ui_html_len;
}

static void handle_api_doc(const http_request_t *req, char **resp_body, int *resp_len, char *content_type)
{
    strcpy(content_type, "text/plain");
    const char *doc = "PinMAME Remote Debugger API\n===========================\n\n"
                      "GET /api/debugger/command?cmd=C (MAME Syntax)\n"
                      "GET /api/debugger/memory/find?addr=A&size=S&pattern=HEX\n"
                      "GET /api/debugger/memory/fill?addr=A&size=S&val=V\n"
                      "GET /api/debugger/control/stepover\n"
                      "GET /api/debugger/control/runto?addr=A\n";
    *resp_body = strdup(doc);
    *resp_len = strlen(doc);
}

#endif /* REMOTE_DEBUG */
