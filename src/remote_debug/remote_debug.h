#ifndef REMOTE_DEBUG_H
#define REMOTE_DEBUG_H

#include "driver.h"

/* Initialize the remote debugger extension */
void remote_debug_init(void);

/* Deinitialize the remote debugger extension */
void remote_debug_exit(void);

/* Lock/Unlock for thread safety */
void remote_debug_lock(void);
void remote_debug_unlock(void);

/* Ready flag */
extern volatile int remote_debug_ready;

/* Check if the emulator should quit */
int remote_debug_should_quit(void);

/* Check if the emulator should be paused by the remote debugger */
int remote_debug_is_paused(void);

/* Frame update hook */
void remote_debug_frame_update(struct mame_display *display);

/* Memory access instrumentation hook */
void remote_debug_memref(UINT32 adr, int length, int write);

/* Memory operations */
void remote_debug_memory_fill(int cpu, UINT32 addr, int size, UINT8 val);

/* Register access */
void remote_debug_set_register(int cpu, int reg, UINT32 val);

/* Callstack tracking */
void remote_debug_push_call(UINT32 caller, UINT32 receiver);
void remote_debug_pop_call(void);
void remote_debug_reset_callstack(void);

#endif /* REMOTE_DEBUG_H */
