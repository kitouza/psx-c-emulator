#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3/SDL.h>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL_opengl.h>
#endif

#include "types.h"

typedef struct DebugUi DebugUi;

enum {
    RENDERER_WIDTH = 1024,
    RENDERER_HEIGHT = 512,
    RENDERER_VERTEX_BUFFER_LENGTH = 64 * 1024
};

typedef struct {
    i16 x;
    i16 y;
} Position;

typedef struct {
    u8 r;
    u8 g;
    u8 b;
} Color;

typedef struct {
    GLuint object;
    void* data;
    size_t element_size;
} RendererBuffer;

typedef struct Renderer {
    SDL_Window* window;
    SDL_GLContext gl_context;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint vertex_array_obj;
    RendererBuffer positions;
    RendererBuffer colors;
    u32 vertex_count;
    bool initialized;
    bool debug_pause_requested;
} Renderer;

bool renderer_init(Renderer* renderer);
bool renderer_push_triangle(Renderer* renderer,
                            const Position positions[3],
                            const Color colors[3]);
bool renderer_push_quad(Renderer* renderer,
                        const Position positions[4],
                        const Color colors[4]);
bool renderer_draw(Renderer* renderer);
bool renderer_display(Renderer* renderer);
bool renderer_handle_events(Renderer* renderer, DebugUi* debug_ui);
bool renderer_take_debug_pause(Renderer* renderer);
void renderer_destroy(Renderer* renderer);

#endif
