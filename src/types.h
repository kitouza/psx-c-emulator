#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef enum {
    ACCESS_BYTE = 1,
    ACCESS_HALFWORD = 2,
    ACCESS_WORD = 4
} AccessWidth;

static inline u32 access_width_mask(AccessWidth width) {
    switch (width) {
        case ACCESS_BYTE: return 0x000000ff;
        case ACCESS_HALFWORD: return 0x0000ffff;
        case ACCESS_WORD: return 0xffffffff;
    }
    return 0;
}

static inline const char* access_width_name(AccessWidth width) {
    switch (width) {
        case ACCESS_BYTE: return "byte";
        case ACCESS_HALFWORD: return "halfword";
        case ACCESS_WORD: return "word";
    }
    return "invalid";
}

#endif
