#ifndef GPU_H
#define GPU_H

#include "types.h"

typedef enum {
    GPU_TEXTURE_DEPTH_4_BIT = 0,
    GPU_TEXTURE_DEPTH_8_BIT = 1,
    GPU_TEXTURE_DEPTH_15_BIT = 2
} GpuTextureDepth;

typedef enum {
    GPU_FIELD_BOTTOM = 0,
    GPU_FIELD_TOP = 1
} GpuField;

typedef enum {
    GPU_VERTICAL_RESOLUTION_240 = 0,
    GPU_VERTICAL_RESOLUTION_480 = 1
} GpuVerticalResolution;

typedef enum {
    GPU_VIDEO_MODE_NTSC = 0,
    GPU_VIDEO_MODE_PAL = 1
} GpuVideoMode;

typedef enum {
    GPU_DISPLAY_DEPTH_15_BIT = 0,
    GPU_DISPLAY_DEPTH_24_BIT = 1
} GpuDisplayDepth;

typedef enum {
    GPU_DMA_OFF = 0,
    GPU_DMA_FIFO = 1,
    GPU_DMA_CPU_TO_GP0 = 2,
    GPU_DMA_VRAM_TO_CPU = 3
} GpuDmaDirection;

typedef enum {
    GPU_GP0_MODE_COMMAND,
    GPU_GP0_MODE_IMAGE_LOAD
} GpuGp0Mode;

typedef enum {
    GPU_COMMAND_NONE,
    GPU_COMMAND_NOP,
    GPU_COMMAND_CLEAR_CACHE,
    GPU_COMMAND_MONO_QUAD,
    GPU_COMMAND_TEXTURED_QUAD,
    GPU_COMMAND_SHADED_TRIANGLE,
    GPU_COMMAND_SHADED_QUAD,
    GPU_COMMAND_IMAGE_LOAD,
    GPU_COMMAND_IMAGE_STORE,
    GPU_COMMAND_DRAW_MODE,
    GPU_COMMAND_TEXTURE_WINDOW,
    GPU_COMMAND_DRAWING_AREA_TOP_LEFT,
    GPU_COMMAND_DRAWING_AREA_BOTTOM_RIGHT,
    GPU_COMMAND_DRAWING_OFFSET,
    GPU_COMMAND_MASK_BIT_SETTING
} GpuCommand;

typedef struct {
    u8 page_base_x;
    u8 page_base_y;
    u8 semi_transparency;
    GpuTextureDepth texture_depth;
    bool dithering;
    bool draw_to_display;
    bool force_set_mask_bit;
    bool preserve_masked_pixels;
    GpuField field;
    bool texture_disable;
    u8 horizontal_resolution;
    GpuVerticalResolution vertical_resolution;
    GpuVideoMode video_mode;
    GpuDisplayDepth display_depth;
    bool interlaced;
    bool display_disabled;
    bool interrupt;
    GpuDmaDirection dma_direction;
    bool rectangle_texture_x_flip;
    bool rectangle_texture_y_flip;
    u8 texture_window_x_mask;
    u8 texture_window_y_mask;
    u8 texture_window_x_offset;
    u8 texture_window_y_offset;
    u16 drawing_area_left;
    u16 drawing_area_top;
    u16 drawing_area_right;
    u16 drawing_area_bottom;
    i16 drawing_x_offset;
    i16 drawing_y_offset;
    u16 display_vram_x_start;
    u16 display_vram_y_start;
    u16 display_horizontal_start;
    u16 display_horizontal_end;
    u16 display_line_start;
    u16 display_line_end;
    u32 gp0_command_buffer[12];
    u8 gp0_command_length;
    u32 gp0_words_remaining;
    GpuCommand gp0_command;
    GpuGp0Mode gp0_mode;
} Gpu;

void gpu_init(Gpu* gpu);
u32 gpu_read(const Gpu* gpu);
u32 gpu_status(const Gpu* gpu);
bool gpu_gp0(Gpu* gpu, u32 val);
bool gpu_gp1(Gpu* gpu, u32 val);

#endif
