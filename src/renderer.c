#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* renderer_load_shader_source(const char* filename) {
    const char* base_path = SDL_GetBasePath();
    if (base_path == NULL) {
        fprintf(stderr, "Failed to locate executable: %s\n", SDL_GetError());
        return NULL;
    }

    size_t path_length = strlen(base_path) + strlen("shaders/")
        + strlen(filename) + 1;
    char* path = malloc(path_length);
    if (path == NULL) {
        fprintf(stderr, "Failed to allocate shader path\n");
        return NULL;
    }

    snprintf(path, path_length, "%sshaders/%s", base_path, filename);
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed to open shader '%s'\n", path);
        free(path);
        return NULL;
    }
    free(path);

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Failed to seek shader '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    long file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to determine shader size for '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    char* source = malloc((size_t)file_size + 1);
    if (source == NULL) {
        fprintf(stderr, "Failed to allocate shader source for '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(source, 1, (size_t)file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "Failed to read shader '%s'\n", filename);
        free(source);
        return NULL;
    }

    source[bytes_read] = '\0';
    return source;
}

static void renderer_print_shader_log(GLuint shader, const char* name) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return;
    }

    char* log = malloc((size_t)length);
    if (log != NULL) {
        glGetShaderInfoLog(shader, length, NULL, log);
        fprintf(stderr, "%s shader log:\n%s\n", name, log);
        free(log);
    }
}

static GLuint renderer_compile_shader(GLenum type,
                                      const char* source,
                                      const char* name) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        fprintf(stderr, "Failed to create %s shader\n", name);
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    renderer_print_shader_log(shader, name);
    if (compiled != GL_TRUE) {
        fprintf(stderr, "Failed to compile %s shader\n", name);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static void renderer_print_program_log(GLuint program) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return;
    }

    char* log = malloc((size_t)length);
    if (log != NULL) {
        glGetProgramInfoLog(program, length, NULL, log);
        fprintf(stderr, "OpenGL program log:\n%s\n", log);
        free(log);
    }
}

static GLuint renderer_link_program(GLuint vertex_shader,
                                    GLuint fragment_shader) {
    GLuint program = glCreateProgram();
    if (program == 0) {
        fprintf(stderr, "Failed to create OpenGL program\n");
        return 0;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    renderer_print_program_log(program);
    if (linked != GL_TRUE) {
        fprintf(stderr, "Failed to link OpenGL program\n");
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

static bool renderer_bind_integer_attribute(GLuint program,
                                            const char* name,
                                            const RendererBuffer* buffer,
                                            GLint components,
                                            GLenum type) {
    GLint location = glGetAttribLocation(program, name);
    if (location < 0) {
        fprintf(stderr,
                "OpenGL attribute '%s' was not found in the shader program\n",
                name);
        return false;
    }

    glBindBuffer(GL_ARRAY_BUFFER, buffer->object);
    glEnableVertexAttribArray((GLuint)location);
    glVertexAttribIPointer((GLuint)location,
                           components,
                           type,
                           (GLsizei)buffer->element_size,
                           NULL);

    if (glGetError() != GL_NO_ERROR) {
        fprintf(stderr, "Failed to configure OpenGL attribute '%s'\n", name);
        return false;
    }

    return true;
}

static bool renderer_set_gl_attribute(SDL_GLAttr attribute, int value) {
    if (SDL_GL_SetAttribute(attribute, value)) {
        return true;
    }

    fprintf(stderr, "Failed to set OpenGL attribute: %s\n", SDL_GetError());
    return false;
}

static bool renderer_buffer_init(RendererBuffer* buffer,
                                 size_t element_size) {
    size_t size = element_size * RENDERER_VERTEX_BUFFER_LENGTH;
    buffer->data = calloc(RENDERER_VERTEX_BUFFER_LENGTH, element_size);
    if (buffer->data == NULL) {
        fprintf(stderr, "Failed to allocate renderer staging buffer\n");
        return false;
    }

    buffer->element_size = element_size;
    glGenBuffers(1, &buffer->object);
    glBindBuffer(GL_ARRAY_BUFFER, buffer->object);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)size,
                 buffer->data,
                 GL_DYNAMIC_DRAW);

    if (buffer->object == 0 || glGetError() != GL_NO_ERROR) {
        fprintf(stderr, "Failed to create OpenGL attribute buffer\n");
        if (buffer->object != 0) {
            glDeleteBuffers(1, &buffer->object);
        }
        free(buffer->data);
        memset(buffer, 0, sizeof(RendererBuffer));
        return false;
    }

    return true;
}

static bool renderer_buffer_set(RendererBuffer* buffer,
                                u32 index,
                                const void* value) {
    if (index >= RENDERER_VERTEX_BUFFER_LENGTH) {
        fprintf(stderr, "Renderer vertex buffer overflow\n");
        return false;
    }

    size_t offset = (size_t)index * buffer->element_size;
    memcpy((u8*)buffer->data + offset, value, buffer->element_size);
    return true;
}

static bool renderer_buffer_upload(const RendererBuffer* buffer,
                                   u32 element_count) {
    size_t size = (size_t)element_count * buffer->element_size;
    glBindBuffer(GL_ARRAY_BUFFER, buffer->object);
    glBufferSubData(GL_ARRAY_BUFFER,
                    0,
                    (GLsizeiptr)size,
                    buffer->data);
    return glGetError() == GL_NO_ERROR;
}

static void renderer_buffer_destroy(RendererBuffer* buffer) {
    if (buffer->object != 0) {
        glDeleteBuffers(1, &buffer->object);
    }

    free(buffer->data);
    memset(buffer, 0, sizeof(RendererBuffer));
}

bool renderer_init(Renderer* renderer) {
    memset(renderer, 0, sizeof(Renderer));

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Failed to initialize SDL video: %s\n", SDL_GetError());
        return false;
    }

    if (!renderer_set_gl_attribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3)
        || !renderer_set_gl_attribute(SDL_GL_CONTEXT_MINOR_VERSION, 3)
        || !renderer_set_gl_attribute(SDL_GL_CONTEXT_PROFILE_MASK,
                                      SDL_GL_CONTEXT_PROFILE_CORE)
        || !renderer_set_gl_attribute(SDL_GL_DOUBLEBUFFER, 1)) {
        SDL_Quit();
        return false;
    }

    renderer->window = SDL_CreateWindow("PSX",
                                        RENDERER_WIDTH,
                                        RENDERER_HEIGHT,
                                        SDL_WINDOW_OPENGL);
    if (renderer->window == NULL) {
        fprintf(stderr, "Failed to create SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    renderer->gl_context = SDL_GL_CreateContext(renderer->window);
    if (renderer->gl_context == NULL) {
        fprintf(stderr,
                "Failed to create OpenGL context: %s\n",
                SDL_GetError());
        SDL_DestroyWindow(renderer->window);
        renderer->window = NULL;
        SDL_Quit();
        return false;
    }

    if (!SDL_GL_MakeCurrent(renderer->window, renderer->gl_context)) {
        fprintf(stderr,
                "Failed to activate OpenGL context: %s\n",
                SDL_GetError());
        renderer_destroy(renderer);
        return false;
    }

    char* vertex_source = renderer_load_shader_source("vertex.glsl");
    char* fragment_source = renderer_load_shader_source("fragment.glsl");
    if (vertex_source == NULL || fragment_source == NULL) {
        free(vertex_source);
        free(fragment_source);
        renderer_destroy(renderer);
        return false;
    }

    renderer->vertex_shader = renderer_compile_shader(GL_VERTEX_SHADER,
                                                       vertex_source,
                                                       "vertex");
    renderer->fragment_shader = renderer_compile_shader(GL_FRAGMENT_SHADER,
                                                         fragment_source,
                                                         "fragment");
    free(vertex_source);
    free(fragment_source);

    if (renderer->vertex_shader == 0 || renderer->fragment_shader == 0) {
        renderer_destroy(renderer);
        return false;
    }

    renderer->program = renderer_link_program(renderer->vertex_shader,
                                              renderer->fragment_shader);
    if (renderer->program == 0) {
        renderer_destroy(renderer);
        return false;
    }
    glUseProgram(renderer->program);

    glGenVertexArrays(1, &renderer->vertex_array_obj);
    glBindVertexArray(renderer->vertex_array_obj);
    if (renderer->vertex_array_obj == 0 || glGetError() != GL_NO_ERROR) {
        fprintf(stderr, "Failed to create OpenGL vertex array object\n");
        renderer_destroy(renderer);
        return false;
    }

    if (!renderer_buffer_init(&renderer->positions, sizeof(Position))
        || !renderer_bind_integer_attribute(renderer->program,
                                            "vertex_position",
                                            &renderer->positions,
                                            2,
                                            GL_SHORT)
        || !renderer_buffer_init(&renderer->colors, sizeof(Color))
        || !renderer_bind_integer_attribute(renderer->program,
                                            "vertex_color",
                                            &renderer->colors,
                                            3,
                                            GL_UNSIGNED_BYTE)) {
        renderer_destroy(renderer);
        return false;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    SDL_GL_SwapWindow(renderer->window);

    renderer->initialized = true;
    return true;
}

bool renderer_push_triangle(Renderer* renderer,
                            const Position positions[3],
                            const Color colors[3]) {
    if (renderer->vertex_count > RENDERER_VERTEX_BUFFER_LENGTH - 3) {
        fprintf(stderr, "Renderer buffers full; forcing an early draw\n");
        if (!renderer_draw(renderer)) {
            return false;
        }
    }

    for (u32 i = 0; i < 3; ++i) {
        u32 index = renderer->vertex_count + i;
        if (!renderer_buffer_set(&renderer->positions, index, &positions[i])
            || !renderer_buffer_set(&renderer->colors, index, &colors[i])) {
            fprintf(stderr, "Failed to stage triangle vertex %u\n", i);
            return false;
        }
    }

    renderer->vertex_count += 3;
    return true;
}

bool renderer_push_quad(Renderer* renderer,
                        const Position positions[4],
                        const Color colors[4]) {
    if (renderer->vertex_count > RENDERER_VERTEX_BUFFER_LENGTH - 6) {
        fprintf(stderr, "Renderer buffers full; forcing an early draw\n");
        if (!renderer_draw(renderer)) {
            return false;
        }
    }

    // The PSX splits a quad along the diagonal shared by vertices 1 and 2.
    static const u8 vertex_order[6] = { 0, 1, 2, 1, 2, 3 };
    for (u32 i = 0; i < 6; ++i) {
        u8 source = vertex_order[i];
        u32 destination = renderer->vertex_count + i;
        if (!renderer_buffer_set(&renderer->positions,
                                 destination,
                                 &positions[source])
            || !renderer_buffer_set(&renderer->colors,
                                    destination,
                                    &colors[source])) {
            fprintf(stderr, "Failed to stage quadrilateral vertex %u\n", i);
            return false;
        }
    }

    renderer->vertex_count += 6;
    return true;
}

bool renderer_draw(Renderer* renderer) {
    if (renderer->vertex_count == 0) {
        return true;
    }

    // Upload each complete attribute range once. The CPU staging arrays remain
    // independent from GPU memory, so OpenGL handles any buffer-use hazards.
    if (!renderer_buffer_upload(&renderer->positions, renderer->vertex_count)
        || !renderer_buffer_upload(&renderer->colors,
                                   renderer->vertex_count)) {
        fprintf(stderr, "Failed to upload renderer attribute buffers\n");
        return false;
    }

    glUseProgram(renderer->program);
    glBindVertexArray(renderer->vertex_array_obj);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)renderer->vertex_count);
    if (glGetError() != GL_NO_ERROR) {
        fprintf(stderr, "OpenGL failed to draw buffered triangles\n");
        return false;
    }

    renderer->vertex_count = 0;
    return true;
}

bool renderer_display(Renderer* renderer) {
    if (!renderer_draw(renderer)) {
        return false;
    }

    if (!SDL_GL_SwapWindow(renderer->window)) {
        fprintf(stderr, "Failed to swap OpenGL window: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

bool renderer_handle_events(Renderer* renderer) {
    (void)renderer;
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT
            || (event.type == SDL_EVENT_KEY_DOWN
                && event.key.key == SDLK_ESCAPE)) {
            return false;
        }
    }

    return true;
}

void renderer_destroy(Renderer* renderer) {
    renderer_buffer_destroy(&renderer->positions);
    renderer_buffer_destroy(&renderer->colors);

    if (renderer->vertex_array_obj != 0) {
        glDeleteVertexArrays(1, &renderer->vertex_array_obj);
        renderer->vertex_array_obj = 0;
    }
    if (renderer->program != 0) {
        glDeleteProgram(renderer->program);
        renderer->program = 0;
    }
    if (renderer->vertex_shader != 0) {
        glDeleteShader(renderer->vertex_shader);
        renderer->vertex_shader = 0;
    }
    if (renderer->fragment_shader != 0) {
        glDeleteShader(renderer->fragment_shader);
        renderer->fragment_shader = 0;
    }

    if (renderer->gl_context != NULL) {
        SDL_GL_DestroyContext(renderer->gl_context);
        renderer->gl_context = NULL;
    }

    if (renderer->window != NULL) {
        SDL_DestroyWindow(renderer->window);
        renderer->window = NULL;
    }

    SDL_Quit();
    renderer->initialized = false;
}
