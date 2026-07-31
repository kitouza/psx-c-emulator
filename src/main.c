#include "types.h"
#include "cpu.h"
#include "debugger.h"
#include "debug_ui.h"
#include "gdb_server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool debug_ui;
    bool show_help;
} AppOptions;

static void print_usage(const char* program) {
    printf("Usage: %s [options]\n", program);
    printf("  --debug-ui  Open the PSX hardware debugger window\n");
    printf("  --help      Show this help message\n");
}

static bool parse_options(int argc, char** argv, AppOptions* options) {
    *options = (AppOptions){0};
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug-ui") == 0) {
            options->debug_ui = true;
        } else if (strcmp(argv[i], "--help") == 0
                   || strcmp(argv[i], "-h") == 0) {
            options->show_help = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

static u16 gdb_port_from_environment(void) {
    const char* setting = getenv("PSX_GDB_PORT");
    if (setting == NULL) return 9001;

    errno = 0;
    char* end;
    unsigned long port = strtoul(setting, &end, 10);
    if (errno != 0 || *setting == '\0' || *end != '\0' || port > 65535
        || port == 0) {
        fprintf(stderr, "Invalid PSX_GDB_PORT: %s\n", setting);
        return 0;
    }
    return (u16)port;
}

int main(int argc, char** argv) {
    AppOptions options;
    if (!parse_options(argc, argv, &options)) {
        return 1;
    }
    if (options.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    Bios bios;
    if (!bios_init(&bios, "roms/SCPH1001.bin")) {
        return 1;
    }

    Interconnect inter;
    if (!interconnect_init(&inter, &bios)) {
        return 1;
    }

    Debugger debugger;
    debugger_init(&debugger);

    Cpu cpu;
    cpu_init(&cpu, &inter, &debugger);

    DebugUi debug_ui = {0};
    if (options.debug_ui
        && !debug_ui_init(&debug_ui, &inter.gpu.renderer)) {
        interconnect_destroy(&inter);
        return 1;
    }

    u16 gdb_port = gdb_port_from_environment();
    if (gdb_port == 0) {
        debug_ui_destroy(&debug_ui, &inter.gpu.renderer);
        interconnect_destroy(&inter);
        return 1;
    }

    GdbServer gdb;
    if (!gdb_server_init(&gdb, gdb_port)) {
        debug_ui_destroy(&debug_ui, &inter.gpu.renderer);
        interconnect_destroy(&inter);
        return 1;
    }

    bool running = true;
    while (running) {
        if (!gdb_server_poll(&gdb, &cpu, &debugger)) {
            fprintf(stderr, "GDB server failed\n");
            break;
        }

        debug_ui_input_begin(&debug_ui);
        running = renderer_handle_events(&inter.gpu.renderer, &debug_ui);
        debug_ui_input_end(&debug_ui);
        if (renderer_take_debug_pause(&inter.gpu.renderer)) {
            debugger_pause(&debugger);
        }
        debug_ui_frame(&debug_ui,
                       &inter.gpu.renderer,
                       &cpu,
                       &debugger,
                       &inter.dma,
                       &inter.gpu);

        // Process events regularly so macOS can display and update the window.
        for (u32 i = 0; i < 10000; ++i) {
            if (!cpu_run_next_instruction(&cpu)) {
                break;
            }
        }

        if (debugger.stopped) SDL_Delay(1);
    }

    gdb_server_destroy(&gdb);
    debug_ui_destroy(&debug_ui, &inter.gpu.renderer);
    interconnect_destroy(&inter);
    return 0;
}
