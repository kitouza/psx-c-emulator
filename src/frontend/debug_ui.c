#include "debug_ui.h"

#include "cpu.h"
#include "debugger.h"
#include "dma.h"
#include "gpu.h"
#include "renderer.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include <nuklear.h>

enum {
    DEBUG_UI_WIDTH = 960,
    DEBUG_UI_HEIGHT = 720,
    DEBUG_UI_VERTEX_MEMORY = 512 * 1024,
    DEBUG_UI_ELEMENT_MEMORY = 128 * 1024
};

typedef struct {
    float position[2];
    float uv[2];
    u8 color[4];
} DebugUiVertex;

typedef struct {
    SDL_Window* window;
    SDL_GLContext gl_context;
    SDL_WindowID window_id;
    struct nk_context context;
    struct nk_font_atlas atlas;
    struct nk_buffer commands;
    struct nk_draw_null_texture null_texture;
    GLuint program;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint vertex_array;
    GLuint vertex_buffer;
    GLuint element_buffer;
    GLuint font_texture;
    GLint projection_uniform;
    GLint texture_uniform;
    void* vertices;
    void* elements;
    u32 previous_registers[32];
    bool have_previous_registers;
    bool visible;
} DebugUiImplementation;

static char* load_text_file(const char* directory, const char* filename) {
    const char* base = SDL_GetBasePath();
    if (base == NULL) return NULL;

    size_t length = strlen(base) + strlen(directory) + strlen(filename) + 1;
    char* path = malloc(length);
    if (path == NULL) return NULL;
    snprintf(path, length, "%s%s%s", base, directory, filename);

    FILE* file = fopen(path, "rb");
    free(path);
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char* text = malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        free(text);
        return NULL;
    }
    text[read] = '\0';
    return text;
}

static GLuint compile_shader(GLenum type, const char* source,
                             const char* description) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        char* log = log_length > 0 ? malloc((size_t)log_length) : NULL;
        if (log != NULL) {
            glGetShaderInfoLog(shader, log_length, NULL, log);
            fprintf(stderr, "%s shader failed:\n%s\n", description, log);
            free(log);
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool create_program(DebugUiImplementation* ui) {
    char* vertex_source = load_text_file("shaders/", "debug_ui.vert");
    char* fragment_source = load_text_file("shaders/", "debug_ui.frag");
    if (vertex_source == NULL || fragment_source == NULL) {
        fprintf(stderr, "Failed to load debug UI shaders\n");
        free(vertex_source);
        free(fragment_source);
        return false;
    }

    ui->vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source,
                                       "Debug UI vertex");
    ui->fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source,
                                         "Debug UI fragment");
    free(vertex_source);
    free(fragment_source);
    if (ui->vertex_shader == 0 || ui->fragment_shader == 0) return false;

    ui->program = glCreateProgram();
    glAttachShader(ui->program, ui->vertex_shader);
    glAttachShader(ui->program, ui->fragment_shader);
    glLinkProgram(ui->program);
    GLint linked = GL_FALSE;
    glGetProgramiv(ui->program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        fprintf(stderr, "Failed to link debug UI shader program\n");
        return false;
    }
    ui->projection_uniform = glGetUniformLocation(ui->program, "projection");
    ui->texture_uniform = glGetUniformLocation(ui->program, "font_texture");
    return true;
}

static bool create_device(DebugUiImplementation* ui) {
    if (!create_program(ui)) return false;

    ui->vertices = malloc(DEBUG_UI_VERTEX_MEMORY);
    ui->elements = malloc(DEBUG_UI_ELEMENT_MEMORY);
    if (ui->vertices == NULL || ui->elements == NULL) return false;

    glGenVertexArrays(1, &ui->vertex_array);
    glGenBuffers(1, &ui->vertex_buffer);
    glGenBuffers(1, &ui->element_buffer);
    glBindVertexArray(ui->vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, ui->vertex_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui->element_buffer);
    glBufferData(GL_ARRAY_BUFFER, DEBUG_UI_VERTEX_MEMORY, NULL, GL_STREAM_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, DEBUG_UI_ELEMENT_MEMORY, NULL,
                 GL_STREAM_DRAW);

    GLsizei stride = sizeof(DebugUiVertex);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(DebugUiVertex, position));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(DebugUiVertex, uv));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          (void*)offsetof(DebugUiVertex, color));

    nk_font_atlas_init_default(&ui->atlas);
    nk_font_atlas_begin(&ui->atlas);
    int width;
    int height;
    const void* pixels = nk_font_atlas_bake(&ui->atlas, &width, &height,
                                            NK_FONT_ATLAS_RGBA32);
    glGenTextures(1, &ui->font_texture);
    glBindTexture(GL_TEXTURE_2D, ui->font_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);
    nk_font_atlas_end(&ui->atlas, nk_handle_id((int)ui->font_texture),
                      &ui->null_texture);
    if (ui->atlas.default_font != NULL) {
        nk_style_set_font(&ui->context,
                          &ui->atlas.default_font->handle);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    return glGetError() == GL_NO_ERROR;
}

static void destroy_device(DebugUiImplementation* ui) {
    nk_font_atlas_clear(&ui->atlas);
    nk_buffer_free(&ui->commands);
    nk_free(&ui->context);
    if (ui->font_texture != 0) glDeleteTextures(1, &ui->font_texture);
    if (ui->vertex_buffer != 0) glDeleteBuffers(1, &ui->vertex_buffer);
    if (ui->element_buffer != 0) glDeleteBuffers(1, &ui->element_buffer);
    if (ui->vertex_array != 0) glDeleteVertexArrays(1, &ui->vertex_array);
    if (ui->program != 0) glDeleteProgram(ui->program);
    if (ui->vertex_shader != 0) glDeleteShader(ui->vertex_shader);
    if (ui->fragment_shader != 0) glDeleteShader(ui->fragment_shader);
    free(ui->vertices);
    free(ui->elements);
}

static void clipboard_copy(nk_handle user, const char* text, int length) {
    (void)user;
    if (length <= 0) return;
    char* copy = malloc((size_t)length + 1);
    if (copy == NULL) return;
    memcpy(copy, text, (size_t)length);
    copy[length] = '\0';
    SDL_SetClipboardText(copy);
    free(copy);
}

static void clipboard_paste(nk_handle user, struct nk_text_edit* edit) {
    (void)user;
    char* text = SDL_GetClipboardText();
    if (text != NULL) {
        nk_textedit_paste(edit, text, (int)strlen(text));
        SDL_free(text);
    }
}

bool debug_ui_init(DebugUi* debug_ui, Renderer* renderer) {
    memset(debug_ui, 0, sizeof(*debug_ui));
    DebugUiImplementation* ui = calloc(1, sizeof(*ui));
    if (ui == NULL) return false;

    SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    ui->window = SDL_CreateWindow("PSX Hardware Debugger",
                                  DEBUG_UI_WIDTH,
                                  DEBUG_UI_HEIGHT,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (ui->window == NULL) goto failure;
    ui->gl_context = SDL_GL_CreateContext(ui->window);
    if (ui->gl_context == NULL
        || !SDL_GL_MakeCurrent(ui->window, ui->gl_context)) {
        goto failure;
    }
    SDL_GL_SetSwapInterval(0);
    ui->window_id = SDL_GetWindowID(ui->window);
    ui->visible = true;

    if (!nk_init_default(&ui->context, NULL)) goto failure;
    ui->context.clip.copy = clipboard_copy;
    ui->context.clip.paste = clipboard_paste;
    nk_buffer_init_default(&ui->commands);
    if (!create_device(ui)) goto failure;

    debug_ui->implementation = ui;
    SDL_GL_MakeCurrent(previous_window != NULL ? previous_window
                                               : renderer->window,
                       previous_context != NULL ? previous_context
                                                : renderer->gl_context);
    return true;

failure:
    if (ui->gl_context != NULL) {
        SDL_GL_MakeCurrent(ui->window, ui->gl_context);
        destroy_device(ui);
        SDL_GL_DestroyContext(ui->gl_context);
    }
    if (ui->window != NULL) SDL_DestroyWindow(ui->window);
    free(ui);
    SDL_GL_MakeCurrent(previous_window, previous_context);
    fprintf(stderr, "Failed to initialize PSX debug UI\n");
    return false;
}

void debug_ui_input_begin(DebugUi* debug_ui) {
    if (debug_ui == NULL) return;
    DebugUiImplementation* ui = debug_ui->implementation;
    if (ui != NULL && ui->visible) nk_input_begin(&ui->context);
}

static bool event_is_for_window(const DebugUiImplementation* ui,
                                const SDL_Event* event) {
    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: return event->key.windowID == ui->window_id;
        case SDL_EVENT_TEXT_INPUT: return event->text.windowID == ui->window_id;
        case SDL_EVENT_MOUSE_MOTION:
            return event->motion.windowID == ui->window_id;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return event->button.windowID == ui->window_id;
        case SDL_EVENT_MOUSE_WHEEL:
            return event->wheel.windowID == ui->window_id;
        default: return false;
    }
}

void debug_ui_handle_event(DebugUi* debug_ui, const SDL_Event* event) {
    if (debug_ui == NULL) return;
    DebugUiImplementation* ui = debug_ui->implementation;
    if (ui == NULL) return;
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
        && event->window.windowID == ui->window_id) {
        ui->visible = false;
        SDL_HideWindow(ui->window);
        return;
    }
    if (!ui->visible || !event_is_for_window(ui, event)) return;

    struct nk_context* context = &ui->context;
    bool down;
    bool control = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            down = event->type == SDL_EVENT_KEY_DOWN;
            switch (event->key.key) {
                case SDLK_LSHIFT:
                case SDLK_RSHIFT: nk_input_key(context, NK_KEY_SHIFT, down); break;
                case SDLK_DELETE: nk_input_key(context, NK_KEY_DEL, down); break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER: nk_input_key(context, NK_KEY_ENTER, down); break;
                case SDLK_TAB: nk_input_key(context, NK_KEY_TAB, down); break;
                case SDLK_BACKSPACE:
                    nk_input_key(context, NK_KEY_BACKSPACE, down); break;
                case SDLK_HOME:
                    nk_input_key(context, NK_KEY_TEXT_START, down); break;
                case SDLK_END:
                    nk_input_key(context, NK_KEY_TEXT_END, down); break;
                case SDLK_PAGEUP:
                    nk_input_key(context, NK_KEY_SCROLL_UP, down); break;
                case SDLK_PAGEDOWN:
                    nk_input_key(context, NK_KEY_SCROLL_DOWN, down); break;
                case SDLK_UP: nk_input_key(context, NK_KEY_UP, down); break;
                case SDLK_DOWN: nk_input_key(context, NK_KEY_DOWN, down); break;
                case SDLK_LEFT:
                    nk_input_key(context, control ? NK_KEY_TEXT_WORD_LEFT
                                                  : NK_KEY_LEFT, down);
                    break;
                case SDLK_RIGHT:
                    nk_input_key(context, control ? NK_KEY_TEXT_WORD_RIGHT
                                                  : NK_KEY_RIGHT, down);
                    break;
                case SDLK_C: nk_input_key(context, NK_KEY_COPY,
                                          down && control); break;
                case SDLK_V: nk_input_key(context, NK_KEY_PASTE,
                                          down && control); break;
                case SDLK_X: nk_input_key(context, NK_KEY_CUT,
                                          down && control); break;
                case SDLK_Z: nk_input_key(context, NK_KEY_TEXT_UNDO,
                                          down && control); break;
                default: break;
            }
            break;
        case SDL_EVENT_TEXT_INPUT:
            {
                nk_glyph glyph = {0};
                strncpy(glyph, event->text.text, NK_UTF_SIZE);
                nk_input_glyph(context, glyph);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            nk_input_motion(context, (int)event->motion.x,
                            (int)event->motion.y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (event->button.button == SDL_BUTTON_LEFT) {
                nk_input_button(context, NK_BUTTON_LEFT,
                                (int)event->button.x, (int)event->button.y,
                                down);
            } else if (event->button.button == SDL_BUTTON_RIGHT) {
                nk_input_button(context, NK_BUTTON_RIGHT,
                                (int)event->button.x, (int)event->button.y,
                                down);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            nk_input_scroll(context,
                            nk_vec2(event->wheel.x, event->wheel.y));
            break;
        default: break;
    }
}

void debug_ui_input_end(DebugUi* debug_ui) {
    if (debug_ui == NULL) return;
    DebugUiImplementation* ui = debug_ui->implementation;
    if (ui != NULL && ui->visible) nk_input_end(&ui->context);
}

static const char* stop_reason_name(DebugStopReason reason) {
    switch (reason) {
        case DEBUG_STOP_NONE: return "Running";
        case DEBUG_STOP_PAUSE: return "Paused";
        case DEBUG_STOP_STEP: return "Single step";
        case DEBUG_STOP_BREAKPOINT: return "Breakpoint";
        case DEBUG_STOP_READ_WATCHPOINT: return "Read watchpoint";
        case DEBUG_STOP_WRITE_WATCHPOINT: return "Write watchpoint";
    }
    return "Unknown";
}

static void label_value(struct nk_context* context, const char* name,
                        const char* format, u32 value) {
    char text[96];
    snprintf(text, sizeof(text), format, name, value);
    nk_label(context, text, NK_TEXT_LEFT);
}

static void label_signed(struct nk_context* context, const char* name,
                         i32 value) {
    char text[96];
    snprintf(text, sizeof(text), "%s: %d", name, value);
    nk_label(context, text, NK_TEXT_LEFT);
}

static void build_controls(DebugUiImplementation* ui, Cpu* cpu,
                           Debugger* debugger) {
    struct nk_context* context = &ui->context;
    if (nk_begin(context, "Execution", nk_rect(10, 10, 300, 150),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(context, 28, 3);
        if (nk_button_label(context, "Run")) debugger_continue(debugger);
        if (nk_button_label(context, "Pause")) debugger_pause(debugger);
        if (nk_button_label(context, "Step")) debugger_step(debugger);

        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, debugger->stopped
                              ? stop_reason_name(debugger->stop_reason)
                              : "Running",
                 NK_TEXT_LEFT);
        label_value(context, "PC", "%s: 0x%08x", cpu->pc);
        label_value(context, "Stop address", "%s: 0x%08x",
                    debugger->stop_address);
    }
    nk_end(context);
}

static void build_cpu(DebugUiImplementation* ui, const Cpu* cpu) {
    static const char* names[32] = {
        "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra"
    };
    struct nk_context* context = &ui->context;
    if (nk_begin(context, "CPU Registers", nk_rect(10, 170, 300, 530),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE
                 | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(context, 19, 2);
        for (u32 i = 0; i < 32; ++i) {
            bool changed = ui->have_previous_registers
                && ui->previous_registers[i] != cpu->regs[i];
            if (changed) {
                nk_style_push_color(context, &context->style.text.color,
                                    nk_rgb(255, 195, 80));
            }
            label_value(context, names[i], "$%s", i);
            label_value(context, "", "%s0x%08x", cpu->regs[i]);
            if (changed) nk_style_pop_color(context);
        }
        nk_layout_row_dynamic(context, 19, 1);
        label_value(context, "HI", "%s: 0x%08x", cpu->hi);
        label_value(context, "LO", "%s: 0x%08x", cpu->lo);
        label_value(context, "SR", "%s: 0x%08x", cpu->sr);
        label_value(context, "Cause", "%s: 0x%08x", cpu->cause);
        label_value(context, "EPC", "%s: 0x%08x", cpu->epc);
        label_value(context, "BadVAddr", "%s: 0x%08x", cpu->badvaddr);
    }
    nk_end(context);
    memcpy(ui->previous_registers, cpu->regs, sizeof(ui->previous_registers));
    ui->have_previous_registers = true;
}

static const char* dma_port_name(u32 port) {
    static const char* names[] = {
        "MDEC in", "MDEC out", "GPU", "CD-ROM", "SPU", "PIO", "OTC"
    };
    return port < 7 ? names[port] : "Unknown";
}

static void build_dma(DebugUiImplementation* ui, const Dma* dma) {
    struct nk_context* context = &ui->context;
    if (nk_begin(context, "DMA Channels", nk_rect(320, 10, 315, 420),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE
                 | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(context, 20, 1);
        label_value(context, "DPCR", "%s: 0x%08x", dma_control(dma));
        label_value(context, "DICR", "%s: 0x%08x", dma_interrupt(dma));

        for (u32 i = 0; i < 7; ++i) {
            const DmaChannel* channel = dma_channel(dma, (DmaPort)i);
            char heading[64];
            snprintf(heading, sizeof(heading), "%u - %s%s", i,
                     dma_port_name(i),
                     dma_channel_active(channel) ? " (active)" : "");
            nk_layout_row_dynamic(context, 22, 1);
            nk_label(context, heading, NK_TEXT_LEFT);
            nk_layout_row_dynamic(context, 18, 2);
            label_value(context, "Base", "%s 0x%06x", channel->base);
            label_value(context, "Control", "%s 0x%08x",
                        dma_channel_control(channel));
            label_value(context, "Block size", "%s %u",
                        channel->block_size);
            label_value(context, "Block count", "%s %u",
                        channel->block_count);
        }
    }
    nk_end(context);
}

static const char* gpu_command_name(GpuCommand command) {
    static const char* names[] = {
        "None", "NOP", "Clear cache", "Monochrome quad", "Textured quad",
        "Shaded triangle", "Shaded quad", "Image load", "Image store",
        "Draw mode", "Texture window", "Drawing area top-left",
        "Drawing area bottom-right", "Drawing offset", "Mask-bit setting"
    };
    return (u32)command < sizeof(names) / sizeof(names[0])
        ? names[command]
        : "Unknown";
}

static void build_gpu(DebugUiImplementation* ui, const Gpu* gpu) {
    struct nk_context* context = &ui->context;
    if (nk_begin(context, "GPU / GP0", nk_rect(645, 10, 305, 420),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE
                 | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(context, 20, 1);
        label_value(context, "GPUSTAT", "%s: 0x%08x", gpu_status(gpu));
        nk_label(context, gpu_command_name(gpu->gp0_command), NK_TEXT_LEFT);
        label_value(context, "Command words", "%s: %u",
                    gpu->gp0_command_length);
        label_value(context, "Words remaining", "%s: %u",
                    gpu->gp0_words_remaining);
        label_value(context, "DMA direction", "%s: %u",
                    (u32)gpu->dma_direction);
        label_signed(context, "Drawing offset X", gpu->drawing_x_offset);
        label_signed(context, "Drawing offset Y", gpu->drawing_y_offset);
        label_value(context, "Area left", "%s: %u", gpu->drawing_area_left);
        label_value(context, "Area top", "%s: %u", gpu->drawing_area_top);
        label_value(context, "Area right", "%s: %u",
                    gpu->drawing_area_right);
        label_value(context, "Area bottom", "%s: %u",
                    gpu->drawing_area_bottom);
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Buffered GP0 words:", NK_TEXT_LEFT);
        for (u32 i = 0; i < gpu->gp0_command_length && i < 12; ++i) {
            label_value(context, "word", "%s: 0x%08x",
                        gpu->gp0_command_buffer[i]);
        }
    }
    nk_end(context);
}

static void build_vram_placeholder(DebugUiImplementation* ui) {
    struct nk_context* context = &ui->context;
    if (nk_begin(context, "VRAM", nk_rect(320, 440, 630, 260),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE
                 | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(context, 24, 1);
        nk_label(context, "VRAM storage is not implemented yet.",
                 NK_TEXT_CENTERED);
        nk_label(context,
                 "This panel will display the 1024x512 16-bit framebuffer "
                 "once GPU image storage is added.",
                 NK_TEXT_CENTERED);
    }
    nk_end(context);
}

static void build_ui(DebugUiImplementation* ui, Cpu* cpu,
                     Debugger* debugger, const Dma* dma, const Gpu* gpu) {
    build_controls(ui, cpu, debugger);
    build_cpu(ui, cpu);
    build_dma(ui, dma);
    build_gpu(ui, gpu);
    build_vram_placeholder(ui);
}

static void render_ui(DebugUiImplementation* ui) {
    int width;
    int height;
    int pixel_width;
    int pixel_height;
    SDL_GetWindowSize(ui->window, &width, &height);
    SDL_GetWindowSizeInPixels(ui->window, &pixel_width, &pixel_height);
    if (width <= 0 || height <= 0) return;

    const GLfloat projection[4][4] = {
        {2.0f / width, 0.0f, 0.0f, 0.0f},
        {0.0f, -2.0f / height, 0.0f, 0.0f},
        {0.0f, 0.0f, -1.0f, 0.0f},
        {-1.0f, 1.0f, 0.0f, 1.0f}
    };
    struct nk_convert_config config;
    memset(&config, 0, sizeof(config));
    static const struct nk_draw_vertex_layout_element layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT,
         offsetof(DebugUiVertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(DebugUiVertex, uv)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(DebugUiVertex, color)},
        {NK_VERTEX_LAYOUT_END}
    };
    config.vertex_layout = layout;
    config.vertex_size = sizeof(DebugUiVertex);
    config.vertex_alignment = _Alignof(DebugUiVertex);
    config.tex_null = ui->null_texture;
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.global_alpha = 1.0f;
    config.shape_AA = NK_ANTI_ALIASING_ON;
    config.line_AA = NK_ANTI_ALIASING_ON;

    struct nk_buffer vertex_buffer;
    struct nk_buffer element_buffer;
    nk_buffer_init_fixed(&vertex_buffer, ui->vertices,
                         DEBUG_UI_VERTEX_MEMORY);
    nk_buffer_init_fixed(&element_buffer, ui->elements,
                         DEBUG_UI_ELEMENT_MEMORY);
    nk_convert(&ui->context, &ui->commands, &vertex_buffer, &element_buffer,
               &config);

    glViewport(0, 0, pixel_width, pixel_height);
    glClearColor(0.055f, 0.065f, 0.085f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(ui->program);
    glUniform1i(ui->texture_uniform, 0);
    glUniformMatrix4fv(ui->projection_uniform, 1, GL_FALSE, &projection[0][0]);
    glBindVertexArray(ui->vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, ui->vertex_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)nk_buffer_total(&vertex_buffer),
                    ui->vertices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui->element_buffer);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                    (GLsizeiptr)nk_buffer_total(&element_buffer),
                    ui->elements);

    float scale_x = (float)pixel_width / (float)width;
    float scale_y = (float)pixel_height / (float)height;
    const struct nk_draw_command* command;
    const nk_draw_index* offset = NULL;
    nk_draw_foreach(command, &ui->context, &ui->commands) {
        if (command->elem_count == 0) continue;
        glBindTexture(GL_TEXTURE_2D, (GLuint)command->texture.id);
        glScissor((GLint)(command->clip_rect.x * scale_x),
                  (GLint)((height - command->clip_rect.y
                           - command->clip_rect.h) * scale_y),
                  (GLint)(command->clip_rect.w * scale_x),
                  (GLint)(command->clip_rect.h * scale_y));
        glDrawElements(GL_TRIANGLES, (GLsizei)command->elem_count,
                       sizeof(nk_draw_index) == 2
                           ? GL_UNSIGNED_SHORT
                           : GL_UNSIGNED_INT,
                       offset);
        offset += command->elem_count;
    }

    nk_clear(&ui->context);
    nk_buffer_clear(&ui->commands);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glUseProgram(0);
}

void debug_ui_frame(DebugUi* debug_ui, Renderer* renderer, Cpu* cpu,
                    Debugger* debugger, const Dma* dma, const Gpu* gpu) {
    if (debug_ui == NULL) return;
    DebugUiImplementation* ui = debug_ui->implementation;
    if (ui == NULL || !ui->visible) return;

    SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    if (!SDL_GL_MakeCurrent(ui->window, ui->gl_context)) return;
    build_ui(ui, cpu, debugger, dma, gpu);
    render_ui(ui);
    SDL_GL_SwapWindow(ui->window);
    SDL_GL_MakeCurrent(previous_window != NULL ? previous_window
                                               : renderer->window,
                       previous_context != NULL ? previous_context
                                                : renderer->gl_context);
}

void debug_ui_destroy(DebugUi* debug_ui, Renderer* renderer) {
    if (debug_ui == NULL) return;
    DebugUiImplementation* ui = debug_ui->implementation;
    if (ui == NULL) return;
    SDL_GL_MakeCurrent(ui->window, ui->gl_context);
    destroy_device(ui);
    SDL_GL_DestroyContext(ui->gl_context);
    SDL_DestroyWindow(ui->window);
    free(ui);
    debug_ui->implementation = NULL;
    SDL_GL_MakeCurrent(renderer->window, renderer->gl_context);
}
