#version 330 core

uniform mat4 projection;

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 tex_coord;
layout(location = 2) in vec4 color;

out vec2 fragment_uv;
out vec4 fragment_color;

void main() {
    fragment_uv = tex_coord;
    fragment_color = color;
    gl_Position = projection * vec4(position, 0.0, 1.0);
}
