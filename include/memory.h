#ifndef OAK_MEMORY_H
#define OAK_MEMORY_H

#include <stdint.h>

/* Shared by memory.c and the generated program. Oak spells the types below as
   `u64` (uint64_t), `u32` (uint32_t), `i32` (int32_t), `i64` (int64_t) and
   `ptr` (void *), and every declaration here is written exactly as memory.oak
   declares it: pointers are a plain `void *`, because that is the only
   pointer Oak can name. Keep the two spellings in sync (see C-INTEROP.md). */

void *oak_mem_alloc(uint64_t size);
void *oak_mem_calloc(uint64_t count, uint64_t size);
void *oak_mem_realloc(void *p, uint64_t size);
void oak_mem_free(void *p);
uint64_t oak_mem_size(void *p);
void oak_mem_copy(void *dst, void *src, uint64_t size);
void oak_mem_fill(void *dst, int32_t byte, uint64_t size);
int32_t oak_mem_equal(void *a, void *b, uint64_t size);
uint64_t oak_mem_align(uint64_t size);

/* Little-endian cells of 8, 16, 32 or 64 bits. A read past the end returns -1
   and a write past the end is ignored, so a bad offset is not a crash. */
int64_t oak_mem_read(void *p, uint64_t offset, uint32_t bits);
void oak_mem_write(void *p, uint64_t offset, uint32_t bits, uint64_t value);

/* An opaque growable buffer of 64-bit cells. Oak only ever holds it as a
   `ptr`, so the layout stays private to this file. */
void *oak_buffer_new(uint64_t capacity);
uint64_t oak_buffer_len(void *b);
uint64_t oak_buffer_push(void *b, uint64_t value);
int64_t oak_buffer_at(void *b, uint64_t index);
void oak_buffer_free(void *b);

#endif