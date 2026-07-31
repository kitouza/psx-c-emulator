#version 330 core

uniform sampler2D font_texture;

in vec2 fragment_uv;
in vec4 fragment_color;
out vec4 output_color;

void main() {
    output_color = fragment_color * texture(font_texture, fragment_uv);
}
