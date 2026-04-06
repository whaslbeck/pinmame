#ifdef REMOTE_DEBUG

#include "remote_debug.h"
#include "http_server.h"
#include "mame.h"
#include "cpuexec.h"
#include "cpuintrf.h"
#include "cpu/m6809/m6809.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

static int is_paused = 0;
static int step_requested = 0;
static int should_quit = 0;
static pthread_mutex_t mame_mutex;
volatile int remote_debug_ready = 0;

/* --- Logs & Messages --- */
typedef struct
{
    UINT32 adr;
    int bank;
    int enabled;
    int temp;
} breakpoint_t;
static breakpoint_t breakpoints[128];
static int breakpoint_count = 0;

typedef struct
{
    UINT32 adr;
    int mode;
    int enabled;
} watchpoint_t;
static watchpoint_t watchpoints[128];
static int watchpoint_count = 0;

/* --- Logs & Messages --- */
typedef struct
{
    UINT32 adr;
    int length;
    int write;
    UINT32 pc;
    int cpunum;
} instr_log_t;
#define LOG_SIZE 1024
static instr_log_t instr_log[LOG_SIZE];
static int log_head = 0;
static int log_count = 0;

typedef struct
{
    UINT32 caller;
    UINT32 receiver;
    int bank;
    UINT16 pc, u, s, x, y;
    UINT8 a, b, dp, cc;
} remote_debug_callstack_entry_t;

#define CALLSTACK_SIZE 128
static remote_debug_callstack_entry_t remote_debug_callstack[CALLSTACK_SIZE];
static int remote_debug_callstack_ptr = 0;

#define MSG_QUEUE_SIZE 50
static char msg_queue[MSG_QUEUE_SIZE][128];
static int msg_head = 0;
static int msg_count = 0;

static UINT32 instr_addrs[128];
static int instr_addr_count = 0;

extern void api_handler(const http_request_t *req, char **resp_body, int *resp_len, char *content_type);
extern int wpc_get_bank(void);

void remote_debug_lock(void) { pthread_mutex_lock(&mame_mutex); }
void remote_debug_unlock(void) { pthread_mutex_unlock(&mame_mutex); }

void remote_debug_add_message(const char *msg)
{
    strncpy(msg_queue[msg_head], msg, 127);
    msg_queue[msg_head][127] = 0;
    msg_head = (msg_head + 1) % MSG_QUEUE_SIZE;
    if (msg_count < MSG_QUEUE_SIZE)
        msg_count++;
}

void remote_debug_init(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mame_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    if (pmoptions.start_paused)
    {
        is_paused = 1;
        printf("Remote Debugger: Paused on start\n");
    }
    should_quit = 0;
    breakpoint_count = 0;
    watchpoint_count = 0;
    msg_count = 0;
    log_count = 0;
    remote_debug_callstack_ptr = 0;
    remote_debug_ready = 1;
    http_server_start(pmoptions.http_port, api_handler);
}

void remote_debug_exit(void)
{
    remote_debug_ready = 0;
    http_server_stop();
    pthread_mutex_destroy(&mame_mutex);
}

int remote_debug_is_paused(void)
{
    if (should_quit)
        return 0;
    if (step_requested)
    {
        step_requested = 0;
        return 0;
    }
    if (is_paused)
        usleep(10000);
    return is_paused;
}

void remote_debug_set_paused(int paused) { is_paused = paused; }
void remote_debug_step(void)
{
    is_paused = 1;
    step_requested = 1;
}
void remote_debug_quit(void)
{
    printf("Remote Debugger: Quit\n");
    should_quit = 1;
}
int remote_debug_should_quit(void) { return should_quit; }

/* Point Management */
void remote_debug_breakpoint_add_internal(UINT32 adr, int bank, int temp)
{
    remote_debug_lock();
    if (breakpoint_count < 128)
    {
        breakpoints[breakpoint_count].adr = adr;
        breakpoints[breakpoint_count].bank = bank;
        breakpoints[breakpoint_count].enabled = 1;
        breakpoints[breakpoint_count].temp = temp;
        breakpoint_count++;
        if (!temp)
        {
            char b[128];
            if (bank != -1)
                sprintf(b, "BP added at %02X:%04X", bank, adr);
            else
                sprintf(b, "BP added at %04X", adr);
            remote_debug_add_message(b);
        }
    }
    remote_debug_unlock();
}

void remote_debug_breakpoint_add(UINT32 adr) { remote_debug_breakpoint_add_internal(adr, -1, 0); }
void remote_debug_breakpoint_add_banked(UINT32 adr, int bank) { remote_debug_breakpoint_add_internal(adr, bank, 0); }

void remote_debug_breakpoint_clear(void)
{
    remote_debug_lock();
    breakpoint_count = 0;
    remote_debug_add_message("Breakpoints cleared");
    remote_debug_unlock();
}
void remote_debug_breakpoint_toggle(int index)
{
    remote_debug_lock();
    if (index >= 0 && index < breakpoint_count)
        breakpoints[index].enabled = !breakpoints[index].enabled;
    remote_debug_unlock();
}
void remote_debug_breakpoint_delete(int index)
{
    remote_debug_lock();
    if (index >= 0 && index < breakpoint_count)
    {
        for (int i = index; i < breakpoint_count - 1; i++)
            breakpoints[i] = breakpoints[i + 1];
        breakpoint_count--;
    }
    remote_debug_unlock();
}

void remote_debug_watchpoint_add(UINT32 adr, int mode)
{
    remote_debug_lock();
    if (watchpoint_count < 128)
    {
        watchpoints[watchpoint_count].adr = adr;
        watchpoints[watchpoint_count].mode = mode;
        watchpoints[watchpoint_count].enabled = 1;
        watchpoint_count++;
        char b[128];
        sprintf(b, "WP added at %04X", adr);
        remote_debug_add_message(b);
    }
    remote_debug_unlock();
}
void remote_debug_watchpoint_clear(void)
{
    remote_debug_lock();
    watchpoint_count = 0;
    remote_debug_add_message("Watchpoints cleared");
    remote_debug_unlock();
}
void remote_debug_watchpoint_toggle(int index)
{
    remote_debug_lock();
    if (index >= 0 && index < watchpoint_count)
        watchpoints[index].enabled = !watchpoints[index].enabled;
    remote_debug_unlock();
}
void remote_debug_watchpoint_delete(int index)
{
    remote_debug_lock();
    if (index >= 0 && index < watchpoint_count)
    {
        for (int i = index; i < watchpoint_count - 1; i++)
            watchpoints[i] = watchpoints[i + 1];
        watchpoint_count--;
    }
    remote_debug_unlock();
}

void remote_debug_get_points(char **buffer, int *len)
{
    remote_debug_lock();
    *buffer = malloc(8192);
    char *p = *buffer;
    p += sprintf(p, "{\"breakpoints\": [");
    for (int i = 0; i < breakpoint_count; i++)
    {
        if (i > 0)
            p += sprintf(p, ",");
        p += sprintf(p, "{\"idx\": %d, \"addr\": %u, \"bank\": %d, \"enabled\": %d}", i, breakpoints[i].adr, breakpoints[i].bank, breakpoints[i].enabled);
    }
    p += sprintf(p, "], \"watchpoints\": [");
    for (int i = 0; i < watchpoint_count; i++)
    {
        if (i > 0)
            p += sprintf(p, ",");
        p += sprintf(p, "{\"idx\": %d, \"addr\": %u, \"mode\": %d, \"enabled\": %d}", i, watchpoints[i].adr, watchpoints[i].mode, watchpoints[i].enabled);
    }
    p += sprintf(p, "]}");
    *len = p - *buffer;
    remote_debug_unlock();
}

void remote_debug_get_messages(char **buffer, int *len)
{
    remote_debug_lock();
    *buffer = malloc(msg_count * 140 + 128);
    char *p = *buffer;
    p += sprintf(p, "{\"messages\": [");
    for (int i = 0; i < msg_count; i++)
    {
        int idx = (msg_head - msg_count + i + MSG_QUEUE_SIZE) % MSG_QUEUE_SIZE;
        if (i > 0)
            p += sprintf(p, ",");
        p += sprintf(p, "\"%s\"", msg_queue[idx]);
    }
    p += sprintf(p, "]}");
    *len = p - *buffer;
    remote_debug_unlock();
}

void remote_debug_breakpoint_hook(void)
{
    UINT32 pc = activecpu_get_reg(REG_PC);
    int current_bank = wpc_get_bank();
    for (int i = 0; i < breakpoint_count; i++)
    {
        if (breakpoints[i].enabled && pc == breakpoints[i].adr)
        {
            /* Check bank if specified */
            if (breakpoints[i].bank != -1 && current_bank != breakpoints[i].bank)
                continue;

            is_paused = 1;
            activecpu_abort_timeslice();
            char b[128];
            if (current_bank != -1)
                sprintf(b, "Halt: BP at %02X:%04X", current_bank, pc);
            else
                sprintf(b, "Halt: BP at %04X", pc);

            remote_debug_lock();
            remote_debug_add_message(b);
            if (breakpoints[i].temp)
            {
                for (int j = i; j < breakpoint_count - 1; j++)
                    breakpoints[j] = breakpoints[j + 1];
                breakpoint_count--;
                i--;
            }
            remote_debug_unlock();
            printf("Remote Debugger: %s\n", b);
            break;
        }
    }
}

void remote_debug_memref(UINT32 adr, int length, int write)
{
    int active_cpu = cpu_getactivecpu();
    if (active_cpu < 0)
        return;
    for (int i = 0; i < watchpoint_count; i++)
    {
        if (watchpoints[i].enabled && adr >= watchpoints[i].adr && adr < watchpoints[i].adr + length)
        {
            int hit = 0;
            if (watchpoints[i].mode == 3)
                hit = 1;
            else if (watchpoints[i].mode == 1 && !write)
                hit = 1;
            else if (watchpoints[i].mode == 2 && write)
                hit = 1;
            if (hit)
            {
                is_paused = 1;
                activecpu_abort_timeslice();
                UINT32 pc = activecpu_get_reg(REG_PC);
                int bank = wpc_get_bank();
                char b[128];
                sprintf(b, "Halt: WP %s at %04X (PC=%04X, Bank=%02X)", write ? "Write" : "Read", adr, pc, bank);
                remote_debug_lock();
                remote_debug_add_message(b);
                remote_debug_unlock();
                printf("Remote Debugger: %s\n", b);
            }
        }
    }
    int logged = 0;
    for (int i = 0; i < instr_addr_count; i++)
    {
        if (adr >= instr_addrs[i] && adr < instr_addrs[i] + length)
        {
            logged = 1;
            break;
        }
    }
    if (logged)
    {
        remote_debug_lock();
        instr_log_t *e = &instr_log[log_head];
        e->adr = adr;
        e->length = length;
        e->write = write;
        e->cpunum = active_cpu;
        e->pc = activecpu_get_reg(REG_PC);
        log_head = (log_head + 1) % LOG_SIZE;
        if (log_count < LOG_SIZE)
            log_count++;
        remote_debug_unlock();
    }
}

void remote_debug_memory_fill(int cpu_idx, UINT32 addr, int size, UINT8 val)
{
    remote_debug_lock();
    if (Machine && cpu_idx < cpu_gettotalcpu())
    {
        for (int i = 0; i < size; i++)
            cpunum_write_byte(cpu_idx, addr + i, val);
        char b[128];
        sprintf(b, "Memory Fill: %04X-%04X with %02X", addr, addr + size - 1, val);
        remote_debug_add_message(b);
    }
    remote_debug_unlock();
}

int remote_debug_memory_find(int cpu_idx, UINT32 addr, int size, const UINT8 *pattern, int pat_len, UINT32 *found_addr)
{
    int result = 0;
    remote_debug_lock();
    if (Machine && cpu_idx < cpu_gettotalcpu() && pat_len > 0 && size > 0 && pat_len <= size)
    {
        for (UINT32 a = addr; a <= addr + size - pat_len; a++)
        {
            int match = 1;
            for (int i = 0; i < pat_len; i++)
            {
                if (cpunum_read_byte(cpu_idx, a + i) != pattern[i])
                {
                    match = 0;
                    break;
                }
            }
            if (match)
            {
                *found_addr = a;
                result = 1;
                break;
            }
        }
    }
    remote_debug_unlock();
    return result;
}

/* Advanced Execution */
void remote_debug_step_over(void)
{
    remote_debug_lock();
    int cpu = cpu_getactivecpu();
    if (cpu >= 0)
    {
        UINT32 pc = activecpu_get_reg(REG_PC);
        char dasm[64];
        activecpu_set_op_base(pc);
        int size = activecpu_dasm(dasm, pc);
        if (size > 0)
        {
            remote_debug_breakpoint_add_internal(pc + size, -1, 1);
            is_paused = 0;
            remote_debug_add_message("Stepping over...");
        }
        else
        {
            remote_debug_step();
        }
    }
    remote_debug_unlock();
}

void remote_debug_run_to(UINT32 addr)
{
    remote_debug_lock();
    remote_debug_breakpoint_add_internal(addr, -1, 1);
    is_paused = 0;
    char b[128];
    sprintf(b, "Running to %04X...", addr);
    remote_debug_add_message(b);
    remote_debug_unlock();
}

/* Callstack tracking */
void remote_debug_push_call(UINT32 caller, UINT32 receiver)
{
    if (!remote_debug_ready)
        return;
    remote_debug_lock();
    if (remote_debug_callstack_ptr < CALLSTACK_SIZE)
    {
        remote_debug_callstack_entry_t *e = &remote_debug_callstack[remote_debug_callstack_ptr++];
        e->caller = caller;
        e->receiver = receiver;
        e->bank = wpc_get_bank();
        e->pc = activecpu_get_reg(REG_PC);
        e->u = activecpu_get_reg(M6809_U);
        e->s = activecpu_get_reg(REG_SP);
        e->x = activecpu_get_reg(M6809_X);
        e->y = activecpu_get_reg(M6809_Y);
        e->a = activecpu_get_reg(M6809_A);
        e->b = activecpu_get_reg(M6809_B);
        e->dp = activecpu_get_reg(M6809_DP);
        e->cc = activecpu_get_reg(M6809_CC);
    }
    remote_debug_unlock();
}

void remote_debug_pop_call(void)
{
    if (!remote_debug_ready)
        return;
    remote_debug_lock();
    if (remote_debug_callstack_ptr > 0)
    {
        remote_debug_callstack_ptr--;
    }
    remote_debug_unlock();
}

void remote_debug_reset_callstack(void)
{
    if (!remote_debug_ready)
        return;
    remote_debug_lock();
    remote_debug_callstack_ptr = 0;
    remote_debug_unlock();
}

/* Callstack */
void remote_debug_get_callstack(char **buffer, int *len)
{
    remote_debug_lock();
    *buffer = malloc(CALLSTACK_SIZE * 256 + 128);
    char *p = *buffer;
    p += sprintf(p, "{\"stack\": [");
    for (int i = 0; i < remote_debug_callstack_ptr; i++)
    {
        remote_debug_callstack_entry_t *e = &remote_debug_callstack[i];
        if (i > 0)
            p += sprintf(p, ",");
        p += sprintf(p, "{\"caller\": %u, \"receiver\": %u, \"bank\": %d, \"pc\": %d, \"u\": %d, \"s\": %d, \"x\": %d, \"y\": %d, \"a\": %d, \"b\": %d, \"dp\": %d, \"cc\": %d}",
                     e->caller, e->receiver, e->bank, e->pc, e->u, e->s, e->x, e->y, e->a, e->b, e->dp, e->cc);
    }
    p += sprintf(p, "]}");
    *len = p - *buffer;
    remote_debug_unlock();
}

void remote_debug_get_log(char **buffer, int *len)
{
    remote_debug_lock();
    *buffer = malloc(log_count * 128 + 256);
    char *p = *buffer;
    p += sprintf(p, "{\"logs\": [");
    for (int i = 0; i < log_count; i++)
    {
        int idx = (log_head - log_count + i + LOG_SIZE) % LOG_SIZE;
        instr_log_t *e = &instr_log[idx];
        if (i > 0)
            p += sprintf(p, ",");
        p += sprintf(p, "{\"cpu\": %d, \"pc\": %u, \"adr\": %u, \"len\": %d, \"write\": %d}", e->cpunum, e->pc, e->adr, e->length, e->write);
    }
    p += sprintf(p, "]}");
    *len = p - *buffer;
    remote_debug_unlock();
}

void remote_debug_instr_add(UINT32 adr)
{
    remote_debug_lock();
    if (instr_addr_count < 128)
        instr_addrs[instr_addr_count++] = adr;
    remote_debug_unlock();
}
void remote_debug_instr_clear(void)
{
    remote_debug_lock();
    instr_addr_count = 0;
    log_head = 0;
    log_count = 0;
    remote_debug_unlock();
}

/* Visuals */
static struct mame_display *last_display = NULL;
void remote_debug_frame_update(struct mame_display *display)
{
    remote_debug_lock();
    last_display = display;
    remote_debug_unlock();
}
void write_ppm_to_buffer(struct mame_bitmap *bitmap, char **buffer, int *len)
{
    if (!bitmap || !last_display)
        return;
    int width = bitmap->width;
    int height = bitmap->height;
    if (width > 2048 || height > 2048)
        return;
    *len = width * height * 3 + 1024;
    *buffer = malloc(*len);
    char *p = *buffer;
    p += sprintf(p, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            UINT32 c;
            if (bitmap->depth == 32)
            {
                c = ((UINT32 **)bitmap->line)[y][x];
                *p++ = (c >> 16) & 0xFF;
                *p++ = (c >> 8) & 0xFF;
                *p++ = c & 0xFF;
            }
            else if (bitmap->depth == 16)
            {
                c = ((UINT16 **)bitmap->line)[y][x];
                rgb_t r = last_display->game_palette[c];
                *p++ = (r >> 16) & 0xFF;
                *p++ = (r >> 8) & 0xFF;
                *p++ = r & 0xFF;
            }
            else if (bitmap->depth == 8)
            {
                c = ((UINT8 **)bitmap->line)[y][x];
                rgb_t r = last_display->game_palette[c];
                *p++ = (r >> 16) & 0xFF;
                *p++ = (r >> 8) & 0xFF;
                *p++ = r & 0xFF;
            }
        }
    }
    *len = p - *buffer;
}
void remote_debug_get_screenshot_info(int *w, int *h)
{
    remote_debug_lock();
    if (last_display && last_display->game_bitmap)
    {
        *w = last_display->game_bitmap->width;
        *h = last_display->game_bitmap->height;
    }
    else
    {
        *w = 0;
        *h = 0;
    }
    remote_debug_unlock();
}

void remote_debug_get_raw_screenshot(char **buffer, int *len)
{
    remote_debug_lock();
    if (last_display && last_display->game_bitmap)
    {
        struct mame_bitmap *bm = last_display->game_bitmap;
        int width = bm->width;
        int height = bm->height;
        *len = width * height * 3;
        *buffer = malloc(*len);
        char *p = *buffer;
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                UINT32 c;
                if (bm->depth == 32)
                {
                    c = ((UINT32 **)bm->line)[y][x];
                    *p++ = (c >> 16) & 0xFF;
                    *p++ = (c >> 8) & 0xFF;
                    *p++ = c & 0xFF;
                }
                else if (bm->depth == 16)
                {
                    c = ((UINT16 **)bm->line)[y][x];
                    rgb_t r = last_display->game_palette[c];
                    *p++ = (r >> 16) & 0xFF;
                    *p++ = (r >> 8) & 0xFF;
                    *p++ = r & 0xFF;
                }
                else if (bm->depth == 8)
                {
                    c = ((UINT8 **)bm->line)[y][x];
                    rgb_t r = last_display->game_palette[c];
                    *p++ = (r >> 16) & 0xFF;
                    *p++ = (r >> 8) & 0xFF;
                    *p++ = r & 0xFF;
                }
            }
        }
    }
    else
    {
        *buffer = NULL;
        *len = 0;
    }
    remote_debug_unlock();
}

void remote_debug_get_screenshot(char **buffer, int *len, char *content_type)
{
    remote_debug_lock();
    if (last_display && last_display->game_bitmap)
    {
        int w, h;
        w = last_display->game_bitmap->width;
        h = last_display->game_bitmap->height;
        char *raw;
        int raw_len;
        remote_debug_get_raw_screenshot(&raw, &raw_len);
        if (raw)
        {
            char head[64];
            int hlen = sprintf(head, "P6\n%d %d\n255\n", w, h);
            *len = raw_len + hlen;
            *buffer = malloc(*len);
            memcpy(*buffer, head, hlen);
            memcpy(*buffer + hlen, raw, raw_len);
            free(raw);
            strcpy(content_type, "image/x-portable-pixmap");
        }
        else
        {
            *buffer = strdup("{\"error\":\"No display\"}");
            *len = 22;
            strcpy(content_type, "application/json");
        }
    }
    else
    {
        *buffer = strdup("{\"error\":\"No display\"}");
        *len = 22;
        strcpy(content_type, "application/json");
    }
    remote_debug_unlock();
}

extern void core_get_dmd_data(int layout_idx, float **pixels, int *width, int *height);
void remote_debug_get_dmd_screenshot(char **buffer, int *len, char *content_type)
{
    float *px;
    int w, h;
    remote_debug_lock();
    core_get_dmd_data(0, &px, &w, &h);
    if (px && w > 0 && h > 0)
    {
        *len = w * h;
        *buffer = malloc(*len);
        UINT8 *p = (UINT8 *)*buffer;
        for (int i = 0; i < w * h; i++)
        {
            float v = px[i];
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            *p++ = (UINT8)(v * 255.0f);
        }
        strcpy(content_type, "application/octet-stream");
    }
    else
    {
        *buffer = strdup("{\"error\":\"No DMD\"}");
        *len = 18;
        strcpy(content_type, "application/json");
    }
    remote_debug_unlock();
}

void remote_debug_get_dmd_pnm(char **buffer, int *len, char *content_type)
{
    float *px;
    int w, h;
    remote_debug_lock();
    core_get_dmd_data(0, &px, &w, &h);
    if (px && w > 0 && h > 0)
    {
        char head[64];
        int hlen = sprintf(head, "P5\n%d %d\n255\n", w, h);
        *len = w * h + hlen;
        *buffer = malloc(*len);
        memcpy(*buffer, head, hlen);
        UINT8 *p = (UINT8 *)(*buffer + hlen);
        for (int i = 0; i < w * h; i++)
        {
            float v = px[i];
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            *p++ = (UINT8)(v * 255.0f);
        }
        strcpy(content_type, "image/x-portable-pixmap");
    }
    else
    {
        *buffer = strdup("{\"error\":\"No DMD\"}");
        *len = 18;
        strcpy(content_type, "application/json");
    }
    remote_debug_unlock();
}
#endif /* REMOTE_DEBUG */
