#ifndef DEBUG_UI_H
#define DEBUG_UI_H

#include <SDL3/SDL.h>

#include "types.h"

typedef struct Cpu Cpu;
typedef struct Debugger Debugger;
typedef struct Dma Dma;
typedef struct Gpu Gpu;
typedef struct Renderer Renderer;

typedef struct DebugUi {
    void* implementation;
} DebugUi;

bool debug_ui_init(DebugUi* ui, Renderer* renderer);
void debug_ui_input_begin(DebugUi* ui);
void debug_ui_handle_event(DebugUi* ui, const SDL_Event* event);
void debug_ui_input_end(DebugUi* ui);
void debug_ui_frame(DebugUi* ui,
                    Renderer* renderer,
                    Cpu* cpu,
                    Debugger* debugger,
                    const Dma* dma,
                    const Gpu* gpu);
void debug_ui_destroy(DebugUi* ui, Renderer* renderer);

#endif
