#version 330 core

in ivec2 vertex_position;
in uvec3 vertex_color;

out vec3 color;

void main() {
    // Convert PSX VRAM coordinates to OpenGL clip coordinates.
    float x = (float(vertex_position.x) / 512.0) - 1.0;

    // VRAM's origin is at the top-left; OpenGL's is bottom-left.
    float y = 1.0 - (float(vertex_position.y) / 256.0);
    gl_Position = vec4(x, y, 0.0, 1.0);

    color = vec3(vertex_color) / 255.0;
}
