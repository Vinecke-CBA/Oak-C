#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

/* Every block Oak hands out is preceded by this header, so the block knows how
   big it is. That is what lets mem_size() answer without trusting the caller,
   and what lets mem_read()/mem_write() refuse to run off the end instead of
   trusting an offset. The header is padded to 16 bytes so the payload keeps
   the alignment malloc and C libraries expect. */
typedef struct {
    uint64_t size;
    uint64_t pad;
} OakBlock;

#define OAK_MEM_HEADER ((uint64_t)sizeof(OakBlock))
#define OAK_MEM_ALIGN 16u

static OakBlock *block_of(void *p) {
    return (OakBlock *)(void *)((uint8_t *)p - OAK_MEM_HEADER);
}

static void *block_alloc(uint64_t size, int zeroed) {
    if (size == 0) {
        size = 1;
    }
    OakBlock *b = (OakBlock *)malloc((size_t)(OAK_MEM_HEADER + size));
    if (!b) {
        return NULL;
    }
    b->size = size;
    b->pad = 0;
    if (zeroed) {
        memset((uint8_t *)b + OAK_MEM_HEADER, 0, (size_t)size);
    }
    return (uint8_t *)b + OAK_MEM_HEADER;
}

void *oak_mem_alloc(uint64_t size) {
    return block_alloc(size, 0);
}

void *oak_mem_calloc(uint64_t count, uint64_t size) {
    if (count == 0 || size == 0) {
        return block_alloc(1, 1);
    }
    if (size > UINT64_MAX / count) {
        return NULL; /* the byte count would overflow */
    }
    return block_alloc(count * size, 1);
}

void *oak_mem_realloc(void *p, uint64_t size) {
    if (!p) {
        return block_alloc(size, 0);
    }
    if (size == 0) {
        size = 1;
    }
    OakBlock *b = (OakBlock *)realloc(block_of(p), (size_t)(OAK_MEM_HEADER + size));
    if (!b) {
        return NULL; /* the old block is untouched and still owned by the caller */
    }
    b->size = size;
    return (uint8_t *)b + OAK_MEM_HEADER;
}

void oak_mem_free(void *p) {
    if (p) {
        free(block_of(p));
    }
}

uint64_t oak_mem_size(void *p) {
    return p ? block_of(p)->size : 0;
}

/* Clamp a requested byte count to what the block really has, so a wrong size
   can never walk off the end of either block. */
static uint64_t clamp_to(void *p, uint64_t size) {
    uint64_t have = oak_mem_size(p);
    return size > have ? have : size;
}

void oak_mem_copy(void *dst, void *src, uint64_t size) {
    if (!dst || !src || size == 0) {
        return;
    }
    memmove(dst, src, (size_t)clamp_to(dst, clamp_to(src, size)));
}

void oak_mem_fill(void *dst, int32_t byte, uint64_t size) {
    if (!dst || size == 0) {
        return;
    }
    memset(dst, (int)(byte & 0xff), (size_t)clamp_to(dst, size));
}

int32_t oak_mem_equal(void *a, void *b, uint64_t size) {
    if (!a || !b) {
        return a == b;
    }
    return memcmp(a, b, (size_t)clamp_to(a, clamp_to(b, size))) == 0;
}

/* Round a size up to the next multiple of 16, the alignment C libraries
   like to be handed. */
uint64_t oak_mem_align(uint64_t size) {
    return (size + (OAK_MEM_ALIGN - 1u)) & ~(uint64_t)(OAK_MEM_ALIGN - 1u);
}


/* Is [offset, offset + width) inside the block? Written as a subtraction so a
   huge offset cannot wrap the check. */
static int cell_in_range(void *p, uint64_t offset, uint64_t width) {
    if (!p) {
        return 0;
    }
    uint64_t size = oak_mem_size(p);
    return offset <= size && width <= size - offset;
}

int64_t oak_mem_read(void *p, uint64_t offset, uint32_t bits) {
    uint64_t width = bits / 8u;
    if (bits != 8 && bits != 16 && bits != 32 && bits != 64) {
        return -1;
    }
    if (!cell_in_range(p, offset, width)) {
        return -1;
    }
    const uint8_t *base = (const uint8_t *)p;
    switch (bits) {
    case 8:
        return (int64_t)base[offset];
    case 16: {
        uint16_t v;
        memcpy(&v, base + offset, sizeof(v));
        return (int64_t)v;
    }
    case 32: {
        uint32_t v;
        memcpy(&v, base + offset, sizeof(v));
        return (int64_t)v;
    }
    default: {
        uint64_t v;
        memcpy(&v, base + offset, sizeof(v));
        return (int64_t)v;
    }
    }
}

void oak_mem_write(void *p, uint64_t offset, uint32_t bits, uint64_t value) {
    uint64_t width = bits / 8u;
    if (bits != 8 && bits != 16 && bits != 32 && bits != 64) {
        return;
    }
    if (!cell_in_range(p, offset, width)) {
        return; /* a write past the end is dropped, not a crash */
    }
    uint8_t *base = (uint8_t *)p;
    switch (bits) {
    case 8:
        base[offset] = (uint8_t)value;
        return;
    case 16: {
        uint16_t v = (uint16_t)value;
        memcpy(base + offset, &v, sizeof(v));
        return;
    }
    case 32: {
        uint32_t v = (uint32_t)value;
        memcpy(base + offset, &v, sizeof(v));
        return;
    }
    default:
        memcpy(base + offset, &value, sizeof(value));
        return;
    }
}


/* The growable buffer: a header followed by the cells, so one pointer is all
   the caller has to keep and the layout stays private to this file. */
typedef struct {
    uint64_t len;
    uint64_t cap;
    uint64_t cells[];
} OakBuffer;

void *oak_buffer_new(uint64_t capacity) {
    if (capacity == 0) {
        capacity = 8;
    }
    if (capacity > (UINT64_MAX - sizeof(OakBuffer)) / sizeof(uint64_t)) {
        return NULL;
    }
    OakBuffer *b = (OakBuffer *)calloc(1, sizeof(OakBuffer) + capacity * sizeof(uint64_t));
    if (b) {
        b->cap = capacity;
    }
    return b;
}

uint64_t oak_buffer_len(void *b) {
    return b ? ((OakBuffer *)b)->len : 0;
}

uint64_t oak_buffer_push(void *b, uint64_t value) {
    OakBuffer *buf = (OakBuffer *)b;
    if (!buf) {
        return 0;
    }
    if (buf->len == buf->cap) {
        uint64_t cap = buf->cap ? buf->cap * 2 : 8;
        OakBuffer *grown =
            (OakBuffer *)realloc(buf, sizeof(OakBuffer) + cap * sizeof(uint64_t));
        if (!grown) {
            return buf->len; /* unchanged: the old block is still valid */
        }
        grown->cap = cap;
        buf = grown;
    }
    buf->cells[buf->len++] = value;
    return buf->len;
}

int64_t oak_buffer_at(void *b, uint64_t index) {
    OakBuffer *buf = (OakBuffer *)b;
    if (!buf || index >= buf->len) {
        return -1;
    }
    return (int64_t)buf->cells[index];
}

void oak_buffer_free(void *b) {
    free(b);
}
