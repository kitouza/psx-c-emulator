#include "gpu.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

bool gpu_init(Gpu* gpu) {
    memset(gpu, 0, sizeof(Gpu));
    gpu->texture_depth = GPU_TEXTURE_DEPTH_4_BIT;
    gpu->field = GPU_FIELD_TOP;
    gpu->vertical_resolution = GPU_VERTICAL_RESOLUTION_240;
    gpu->video_mode = GPU_VIDEO_MODE_NTSC;
    gpu->display_depth = GPU_DISPLAY_DEPTH_15_BIT;
    gpu->display_disabled = true;
    gpu->dma_direction = GPU_DMA_OFF;
    return renderer_init(&gpu->renderer);
}

void gpu_destroy(Gpu* gpu) {
    renderer_destroy(&gpu->renderer);
}

u32 gpu_read(const Gpu* gpu) {
    (void)gpu;
    // No command capable of producing GPUREAD data is implemented yet.
    return 0;
}

u32 gpu_status(const Gpu* gpu) {
    u32 status = 0;

    status |= (u32)(gpu->page_base_x & 0x0f);
    status |= (u32)(gpu->page_base_y & 1) << 4;
    status |= (u32)(gpu->semi_transparency & 3) << 5;
    status |= (u32)gpu->texture_depth << 7;
    status |= (u32)gpu->dithering << 9;
    status |= (u32)gpu->draw_to_display << 10;
    status |= (u32)gpu->force_set_mask_bit << 11;
    status |= (u32)gpu->preserve_masked_pixels << 12;
    status |= (u32)gpu->field << 13;
    // Bit 14 is not supported by the guide's initial implementation.
    status |= (u32)gpu->texture_disable << 15;
    status |= (u32)(gpu->horizontal_resolution & 7) << 16;
    // Temporary guide workaround: accurate bit 31 timing is not available, so
    // advertising 480-line mode here can deadlock the interlaced BIOS path.
    // status |= (u32)gpu->vertical_resolution << 19;
    status |= (u32)gpu->video_mode << 20;
    status |= (u32)gpu->display_depth << 21;
    status |= (u32)gpu->interlaced << 22;
    status |= (u32)gpu->display_disabled << 23;
    status |= (u32)gpu->interrupt << 24;

    // FIFO timing is not emulated yet, so all ready flags stay asserted.
    status |= 1u << 26;
    status |= 1u << 27;
    status |= 1u << 28;
    status |= (u32)gpu->dma_direction << 29;

    u32 dma_request = 0;
    switch (gpu->dma_direction) {
        case GPU_DMA_OFF:
            dma_request = 0;
            break;
        case GPU_DMA_FIFO:
            dma_request = 1;
            break;
        case GPU_DMA_CPU_TO_GP0:
            dma_request = (status >> 28) & 1;
            break;
        case GPU_DMA_VRAM_TO_CPU:
            dma_request = (status >> 27) & 1;
            break;
    }

    status |= dma_request << 25;
    return status;
}

Position position_from_gp0(u32 value) {
    return (Position) {
        .x = (i16)(value & 0xffff),
        .y = (i16)(value >> 16)
    };
}

Color color_from_gp0(u32 value) {
    return (Color) {
        .r = (u8)(value),
        .g = (u8)(value >> 8),
        .b = (u8)(value >> 16)
    };
}

static void gpu_reset_command_buffer(Gpu* gpu) {
    gpu->gp0_command_length = 0;
    gpu->gp0_words_remaining = 0;
    gpu->gp0_command = GPU_COMMAND_NONE;
    gpu->gp0_mode = GPU_GP0_MODE_COMMAND;
}

static bool gpu_gp0_draw_mode(Gpu* gpu) {
    u32 val = gpu->gp0_command_buffer[0];
    u32 texture_depth = (val >> 7) & 3;
    if (texture_depth > GPU_TEXTURE_DEPTH_15_BIT) {
        fprintf(stderr, "Unhandled GPU texture depth: %u\n", texture_depth);
        return false;
    }

    gpu->page_base_x = val & 0x0f;
    gpu->page_base_y = (val >> 4) & 1;
    gpu->semi_transparency = (val >> 5) & 3;
    gpu->texture_depth = (GpuTextureDepth)texture_depth;
    gpu->dithering = ((val >> 9) & 1) != 0;
    gpu->draw_to_display = ((val >> 10) & 1) != 0;
    gpu->texture_disable = ((val >> 11) & 1) != 0;
    gpu->rectangle_texture_x_flip = ((val >> 12) & 1) != 0;
    gpu->rectangle_texture_y_flip = ((val >> 13) & 1) != 0;
    return true;
}

static void gpu_gp0_texture_window(Gpu* gpu) {
    u32 val = gpu->gp0_command_buffer[0];
    gpu->texture_window_x_mask = val & 0x1f;
    gpu->texture_window_y_mask = (val >> 5) & 0x1f;
    gpu->texture_window_x_offset = (val >> 10) & 0x1f;
    gpu->texture_window_y_offset = (val >> 15) & 0x1f;
}

static void gpu_gp0_drawing_area_top_left(Gpu* gpu) {
    u32 val = gpu->gp0_command_buffer[0];
    gpu->drawing_area_left = val & 0x03ff;
    gpu->drawing_area_top = (val >> 10) & 0x03ff;
}

static void gpu_gp0_drawing_area_bottom_right(Gpu* gpu) {
    u32 val = gpu->gp0_command_buffer[0];
    gpu->drawing_area_right = val & 0x03ff;
    gpu->drawing_area_bottom = (val >> 10) & 0x03ff;
}

static bool gpu_gp0_drawing_offset(Gpu* gpu) {
    u16 x = gpu->gp0_command_buffer[0] & 0x07ff;
    u16 y = (gpu->gp0_command_buffer[0] >> 11) & 0x07ff;
    gpu->drawing_x_offset = (i16)(x << 5) >> 5;
    gpu->drawing_y_offset = (i16)(y << 5) >> 5;

    // Temporary frame boundary until GPU timing and VSYNC are emulated.
    return renderer_display(&gpu->renderer);
}

static void gpu_gp0_mask_bit_setting(Gpu* gpu) {
    u32 val = gpu->gp0_command_buffer[0];
    gpu->force_set_mask_bit = (val & 1) != 0;
    gpu->preserve_masked_pixels = (val & 2) != 0;
}

static bool gpu_gp0_image_load(Gpu* gpu) {
    u32 resolution = gpu->gp0_command_buffer[2];
    u64 width = resolution & 0xffff;
    u64 height = resolution >> 16;
    u64 pixels = width * height;
    u64 words = (pixels + 1) / 2;

    if (words > UINT32_MAX) {
        fprintf(stderr, "GPU image load is too large: %llux%llu\n",
                (unsigned long long)width,
                (unsigned long long)height);
        return false;
    }

    gpu->gp0_words_remaining = (u32)words;
    gpu->gp0_mode = words == 0
        ? GPU_GP0_MODE_COMMAND
        : GPU_GP0_MODE_IMAGE_LOAD;
    return true;
}

static void gpu_gp0_image_store(const Gpu* gpu) {
    u32 resolution = gpu->gp0_command_buffer[2];
    fprintf(stderr,
            "Ignoring GPU image store: %ux%u\n",
            resolution & 0xffff,
            resolution >> 16);
}

static bool gp0_triangle_shaded_opaque(Gpu* gpu) {
    Position positions[3] = {
        position_from_gp0(gpu->gp0_command_buffer[1]),
        position_from_gp0(gpu->gp0_command_buffer[3]),
        position_from_gp0(gpu->gp0_command_buffer[5])
    };
    Color colors[3] = {
        color_from_gp0(gpu->gp0_command_buffer[0]),
        color_from_gp0(gpu->gp0_command_buffer[2]),
        color_from_gp0(gpu->gp0_command_buffer[4])
    };

    return renderer_push_triangle(&gpu->renderer, positions, colors);
}

static bool gpu_gp0_quad_mono_opaque(Gpu* gpu) {
    Position positions[4] = {
        position_from_gp0(gpu->gp0_command_buffer[1]),
        position_from_gp0(gpu->gp0_command_buffer[2]),
        position_from_gp0(gpu->gp0_command_buffer[3]),
        position_from_gp0(gpu->gp0_command_buffer[4])
    };
    Color color = color_from_gp0(gpu->gp0_command_buffer[0]);
    Color colors[4] = { color, color, color, color };
    return renderer_push_quad(&gpu->renderer, positions, colors);
}

static bool gpu_gp0_quad_shaded_opaque(Gpu* gpu) {
    Position positions[4] = {
        position_from_gp0(gpu->gp0_command_buffer[1]),
        position_from_gp0(gpu->gp0_command_buffer[3]),
        position_from_gp0(gpu->gp0_command_buffer[5]),
        position_from_gp0(gpu->gp0_command_buffer[7])
    };
    Color colors[4] = {
        color_from_gp0(gpu->gp0_command_buffer[0]),
        color_from_gp0(gpu->gp0_command_buffer[2]),
        color_from_gp0(gpu->gp0_command_buffer[4]),
        color_from_gp0(gpu->gp0_command_buffer[6])
    };
    return renderer_push_quad(&gpu->renderer, positions, colors);
}

static bool gpu_gp0_quad_texture_blend_opaque(Gpu* gpu) {
    Position positions[4] = {
        position_from_gp0(gpu->gp0_command_buffer[1]),
        position_from_gp0(gpu->gp0_command_buffer[3]),
        position_from_gp0(gpu->gp0_command_buffer[5]),
        position_from_gp0(gpu->gp0_command_buffer[7])
    };

    // Textures are not implemented yet; use the guide's solid red fallback.
    Color red = { .r = 0x80, .g = 0, .b = 0 };
    Color colors[4] = { red, red, red, red };
    return renderer_push_quad(&gpu->renderer, positions, colors);
}

static bool gpu_execute_gp0_command(Gpu* gpu) {
    switch (gpu->gp0_command) {
        case GPU_COMMAND_NOP:
        case GPU_COMMAND_CLEAR_CACHE:
            return true;
        case GPU_COMMAND_MONO_QUAD:
            return gpu_gp0_quad_mono_opaque(gpu);
        case GPU_COMMAND_TEXTURED_QUAD:
            return gpu_gp0_quad_texture_blend_opaque(gpu);
        case GPU_COMMAND_SHADED_TRIANGLE:
            return gp0_triangle_shaded_opaque(gpu);
        case GPU_COMMAND_SHADED_QUAD:
            return gpu_gp0_quad_shaded_opaque(gpu);
        case GPU_COMMAND_IMAGE_LOAD:
            return gpu_gp0_image_load(gpu);
        case GPU_COMMAND_IMAGE_STORE:
            gpu_gp0_image_store(gpu);
            return true;
        case GPU_COMMAND_DRAW_MODE:
            return gpu_gp0_draw_mode(gpu);
        case GPU_COMMAND_TEXTURE_WINDOW:
            gpu_gp0_texture_window(gpu);
            return true;
        case GPU_COMMAND_DRAWING_AREA_TOP_LEFT:
            gpu_gp0_drawing_area_top_left(gpu);
            return true;
        case GPU_COMMAND_DRAWING_AREA_BOTTOM_RIGHT:
            gpu_gp0_drawing_area_bottom_right(gpu);
            return true;
        case GPU_COMMAND_DRAWING_OFFSET:
            return gpu_gp0_drawing_offset(gpu);
        case GPU_COMMAND_MASK_BIT_SETTING:
            gpu_gp0_mask_bit_setting(gpu);
            return true;
        case GPU_COMMAND_NONE:
            break;
    }

    fprintf(stderr, "No GP0 command selected\n");
    return false;
}

static bool gpu_start_gp0_command(Gpu* gpu, u32 val) {
    u8 opcode = val >> 24;
    u8 length;
    GpuCommand command;

    switch (opcode) {
        case 0x00: length = 1; command = GPU_COMMAND_NOP; break;
        case 0x01: length = 1; command = GPU_COMMAND_CLEAR_CACHE; break;
        case 0x28: length = 5; command = GPU_COMMAND_MONO_QUAD; break;
        case 0x2c: length = 9; command = GPU_COMMAND_TEXTURED_QUAD; break;
        case 0x30: length = 6; command = GPU_COMMAND_SHADED_TRIANGLE; break;
        case 0x38: length = 8; command = GPU_COMMAND_SHADED_QUAD; break;
        case 0xa0: length = 3; command = GPU_COMMAND_IMAGE_LOAD; break;
        case 0xc0: length = 3; command = GPU_COMMAND_IMAGE_STORE; break;
        case 0xe1: length = 1; command = GPU_COMMAND_DRAW_MODE; break;
        case 0xe2: length = 1; command = GPU_COMMAND_TEXTURE_WINDOW; break;
        case 0xe3: length = 1; command = GPU_COMMAND_DRAWING_AREA_TOP_LEFT; break;
        case 0xe4: length = 1; command = GPU_COMMAND_DRAWING_AREA_BOTTOM_RIGHT; break;
        case 0xe5: length = 1; command = GPU_COMMAND_DRAWING_OFFSET; break;
        case 0xe6: length = 1; command = GPU_COMMAND_MASK_BIT_SETTING; break;
        default:
            fprintf(stderr, "Unhandled GP0 command: 0x%08x\n", val);
            return false;
    }

    gpu->gp0_command_length = 0;
    gpu->gp0_words_remaining = length;
    gpu->gp0_command = command;
    return true;
}

bool gpu_gp0(Gpu* gpu, u32 val) {
    if (gpu->gp0_words_remaining == 0
        && !gpu_start_gp0_command(gpu, val)) {
        return false;
    }

    --gpu->gp0_words_remaining;

    if (gpu->gp0_mode == GPU_GP0_MODE_IMAGE_LOAD) {
        // VRAM is introduced later; consume and discard image data for now.
        if (gpu->gp0_words_remaining == 0) {
            gpu->gp0_mode = GPU_GP0_MODE_COMMAND;
        }
        return true;
    }

    if (gpu->gp0_command_length >= 12) {
        fprintf(stderr, "GP0 command buffer overflow\n");
        return false;
    }

    gpu->gp0_command_buffer[gpu->gp0_command_length++] = val;
    return gpu->gp0_words_remaining != 0 || gpu_execute_gp0_command(gpu);
}

static void gpu_gp1_reset(Gpu* gpu) {
    gpu_reset_command_buffer(gpu);
    gpu->interrupt = false;
    gpu->page_base_x = 0;
    gpu->page_base_y = 0;
    gpu->semi_transparency = 0;
    gpu->texture_depth = GPU_TEXTURE_DEPTH_4_BIT;
    gpu->texture_window_x_mask = 0;
    gpu->texture_window_y_mask = 0;
    gpu->texture_window_x_offset = 0;
    gpu->texture_window_y_offset = 0;
    gpu->dithering = false;
    gpu->draw_to_display = false;
    gpu->texture_disable = false;
    gpu->rectangle_texture_x_flip = false;
    gpu->rectangle_texture_y_flip = false;
    gpu->drawing_area_left = 0;
    gpu->drawing_area_top = 0;
    gpu->drawing_area_right = 0;
    gpu->drawing_area_bottom = 0;
    gpu->drawing_x_offset = 0;
    gpu->drawing_y_offset = 0;
    gpu->force_set_mask_bit = false;
    gpu->preserve_masked_pixels = false;
    gpu->dma_direction = GPU_DMA_OFF;
    gpu->display_disabled = true;
    gpu->display_vram_x_start = 0;
    gpu->display_vram_y_start = 0;
    gpu->horizontal_resolution = 0;
    gpu->vertical_resolution = GPU_VERTICAL_RESOLUTION_240;
    gpu->video_mode = GPU_VIDEO_MODE_NTSC;
    gpu->interlaced = true;
    gpu->display_horizontal_start = 0x0200;
    gpu->display_horizontal_end = 0x0c00;
    gpu->display_line_start = 0x0010;
    gpu->display_line_end = 0x0100;
    gpu->display_depth = GPU_DISPLAY_DEPTH_15_BIT;
}

static bool gpu_gp1_display_mode(Gpu* gpu, u32 val) {
    if ((val & 0x80) != 0) {
        fprintf(stderr, "Unsupported GPU display mode: 0x%08x\n", val);
        return false;
    }

    u8 hr1 = val & 3;
    u8 hr2 = (val >> 6) & 1;
    gpu->horizontal_resolution = hr2 | (hr1 << 1);
    gpu->vertical_resolution = (val & 0x04) != 0
        ? GPU_VERTICAL_RESOLUTION_480
        : GPU_VERTICAL_RESOLUTION_240;
    gpu->video_mode = (val & 0x08) != 0
        ? GPU_VIDEO_MODE_PAL
        : GPU_VIDEO_MODE_NTSC;
    gpu->display_depth = (val & 0x10) != 0
        ? GPU_DISPLAY_DEPTH_24_BIT
        : GPU_DISPLAY_DEPTH_15_BIT;
    gpu->interlaced = (val & 0x20) != 0;
    return true;
}

bool gpu_gp1(Gpu* gpu, u32 val) {
    u8 opcode = val >> 24;

    switch (opcode) {
        case 0x00:
            gpu_gp1_reset(gpu);
            return true;
        case 0x01:
            gpu_reset_command_buffer(gpu);
            return true;
        case 0x02:
            gpu->interrupt = false;
            return true;
        case 0x03:
            gpu->display_disabled = (val & 1) != 0;
            return true;
        case 0x04:
            gpu->dma_direction = (GpuDmaDirection)(val & 3);
            return true;
        case 0x05:
            gpu->display_vram_x_start = val & 0x03fe;
            gpu->display_vram_y_start = (val >> 10) & 0x01ff;
            return true;
        case 0x06:
            gpu->display_horizontal_start = val & 0x0fff;
            gpu->display_horizontal_end = (val >> 12) & 0x0fff;
            return true;
        case 0x07:
            gpu->display_line_start = val & 0x03ff;
            gpu->display_line_end = (val >> 10) & 0x03ff;
            return true;
        case 0x08:
            return gpu_gp1_display_mode(gpu, val);
        default:
            fprintf(stderr, "Unhandled GP1 command: 0x%08x\n", val);
            return false;
    }
}
