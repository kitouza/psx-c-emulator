#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "types.h"

#define DEBUGGER_MAX_BREAKPOINTS 64
#define DEBUGGER_MAX_WATCHPOINTS 64

typedef enum {
    DEBUG_STOP_NONE,
    DEBUG_STOP_PAUSE,
    DEBUG_STOP_STEP,
    DEBUG_STOP_BREAKPOINT,
    DEBUG_STOP_READ_WATCHPOINT,
    DEBUG_STOP_WRITE_WATCHPOINT
} DebugStopReason;

typedef struct Debugger {
    u32 breakpoints[DEBUGGER_MAX_BREAKPOINTS];
    u32 read_watchpoints[DEBUGGER_MAX_WATCHPOINTS];
    u32 write_watchpoints[DEBUGGER_MAX_WATCHPOINTS];
    u32 breakpoint_count;
    u32 read_watchpoint_count;
    u32 write_watchpoint_count;
    bool stopped;
    bool single_step;
    bool skip_breakpoint_once;
    u32 skipped_breakpoint;
    DebugStopReason stop_reason;
    u32 stop_address;
} Debugger;

void debugger_init(Debugger* debugger);
bool debugger_add_breakpoint(Debugger* debugger, u32 addr);
bool debugger_remove_breakpoint(Debugger* debugger, u32 addr);
bool debugger_add_read_watchpoint(Debugger* debugger, u32 addr);
bool debugger_remove_read_watchpoint(Debugger* debugger, u32 addr);
bool debugger_add_write_watchpoint(Debugger* debugger, u32 addr);
bool debugger_remove_write_watchpoint(Debugger* debugger, u32 addr);
bool debugger_before_instruction(Debugger* debugger, u32 pc);
void debugger_after_instruction(Debugger* debugger, u32 pc);
void debugger_memory_read(Debugger* debugger, u32 addr);
void debugger_memory_write(Debugger* debugger, u32 addr);
void debugger_pause(Debugger* debugger);
void debugger_continue(Debugger* debugger);
void debugger_step(Debugger* debugger);

#endif
