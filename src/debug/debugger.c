#include "debugger.h"

#include <string.h>

static bool address_list_contains(const u32* addresses, u32 count, u32 addr) {
    for (u32 i = 0; i < count; ++i) {
        if (addresses[i] == addr) {
            return true;
        }
    }
    return false;
}

static bool address_list_add(u32* addresses, u32* count, u32 capacity,
                             u32 addr) {
    if (address_list_contains(addresses, *count, addr)) {
        return true;
    }
    if (*count == capacity) {
        return false;
    }
    addresses[(*count)++] = addr;
    return true;
}

static bool address_list_remove(u32* addresses, u32* count, u32 addr) {
    for (u32 i = 0; i < *count; ++i) {
        if (addresses[i] == addr) {
            addresses[i] = addresses[--(*count)];
            return true;
        }
    }
    return false;
}

static void debugger_stop(Debugger* debugger, DebugStopReason reason,
                          u32 addr) {
    debugger->stopped = true;
    debugger->single_step = false;
    debugger->stop_reason = reason;
    debugger->stop_address = addr;
}

void debugger_init(Debugger* debugger) {
    memset(debugger, 0, sizeof(*debugger));
}

bool debugger_add_breakpoint(Debugger* debugger, u32 addr) {
    return address_list_add(debugger->breakpoints,
                            &debugger->breakpoint_count,
                            DEBUGGER_MAX_BREAKPOINTS, addr);
}

bool debugger_remove_breakpoint(Debugger* debugger, u32 addr) {
    return address_list_remove(debugger->breakpoints,
                               &debugger->breakpoint_count, addr);
}

bool debugger_add_read_watchpoint(Debugger* debugger, u32 addr) {
    return address_list_add(debugger->read_watchpoints,
                            &debugger->read_watchpoint_count,
                            DEBUGGER_MAX_WATCHPOINTS, addr);
}

bool debugger_remove_read_watchpoint(Debugger* debugger, u32 addr) {
    return address_list_remove(debugger->read_watchpoints,
                               &debugger->read_watchpoint_count, addr);
}

bool debugger_add_write_watchpoint(Debugger* debugger, u32 addr) {
    return address_list_add(debugger->write_watchpoints,
                            &debugger->write_watchpoint_count,
                            DEBUGGER_MAX_WATCHPOINTS, addr);
}

bool debugger_remove_write_watchpoint(Debugger* debugger, u32 addr) {
    return address_list_remove(debugger->write_watchpoints,
                               &debugger->write_watchpoint_count, addr);
}

bool debugger_before_instruction(Debugger* debugger, u32 pc) {
    if (debugger->stopped) {
        return false;
    }
    if (debugger->skip_breakpoint_once && debugger->skipped_breakpoint == pc) {
        debugger->skip_breakpoint_once = false;
        return true;
    }
    debugger->skip_breakpoint_once = false;
    if (address_list_contains(debugger->breakpoints,
                              debugger->breakpoint_count, pc)) {
        debugger_stop(debugger, DEBUG_STOP_BREAKPOINT, pc);
        return false;
    }
    return true;
}

void debugger_after_instruction(Debugger* debugger, u32 pc) {
    if (debugger->single_step && !debugger->stopped) {
        debugger_stop(debugger, DEBUG_STOP_STEP, pc);
    }
}

void debugger_memory_read(Debugger* debugger, u32 addr) {
    if (address_list_contains(debugger->read_watchpoints,
                              debugger->read_watchpoint_count, addr)) {
        debugger_stop(debugger, DEBUG_STOP_READ_WATCHPOINT, addr);
    }
}

void debugger_memory_write(Debugger* debugger, u32 addr) {
    if (address_list_contains(debugger->write_watchpoints,
                              debugger->write_watchpoint_count, addr)) {
        debugger_stop(debugger, DEBUG_STOP_WRITE_WATCHPOINT, addr);
    }
}

void debugger_pause(Debugger* debugger) {
    debugger_stop(debugger, DEBUG_STOP_PAUSE, 0);
}

void debugger_continue(Debugger* debugger) {
    debugger->skip_breakpoint_once =
        debugger->stop_reason == DEBUG_STOP_BREAKPOINT;
    debugger->skipped_breakpoint = debugger->stop_address;
    debugger->stopped = false;
    debugger->single_step = false;
    debugger->stop_reason = DEBUG_STOP_NONE;
}

void debugger_step(Debugger* debugger) {
    debugger->skip_breakpoint_once =
        debugger->stop_reason == DEBUG_STOP_BREAKPOINT;
    debugger->skipped_breakpoint = debugger->stop_address;
    debugger->stopped = false;
    debugger->single_step = true;
    debugger->stop_reason = DEBUG_STOP_NONE;
}
