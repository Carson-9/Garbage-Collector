#ifndef SIMPLE_GC_H
#define SIMPLE_GC_H

#include "../Data_Structures/structures.h"

#define __GC_HEAP_TINY      256
#define __GC_HEAP_SMALL     512
#define __GC_HEAP_MEDIUM    1024
#define __GC_HEAP_LARGE     2048
#define __GC_HEAP_HUGE      4096
#define GC_LOWER_MASK       0x00000000FFFFFFFF
#define GC_UPPER_MASK       0xFFFFFFFF00000000
#define BYTE_SIZE           8

typedef enum{
    AVAILABLE, // CAN BE MALLOC-D
    ALLOCATED, // HAS BEEN MALLOC-D
    USING, // IN-USE
} mem_state;

typedef u64 mem_block; // 1 (REACH STATE) + 23 (EMPTY) +  8(MEM_STATE) + 32 (SIZE)

mem_block mem_info(u32 size, mem_state state);
mem_block mem_set_reach(mem_block m, b8 reachable);
u32 mem_block_get_size(mem_block m);
b8 mem_block_is_reachable(mem_block m);
mem_state mem_block_get_state(mem_block m);
void mem_block_log(mem_block m, const char* s);



typedef struct GC_t{
    u64* heap;
    u32 heap_size; // NB OF ALLOCATED BLOCK, EACH BLOCK IS 8 BYTES
    u32 free_space;
    u32 first_free; // Index of first free chunk (index of indicator block)
    void* base_stack_pointer; // Pointer to lowest point of stack containing the GC
} GC;

void* c_malloc_collect_fun(GC* gc, u64 size, b8 collect);
void* c_malloc_fun(GC* gc, u64 size);
b8 c_realloc_fun(GC* gc, void** ptr);
b8 c_free_fun(GC* gc, void* ptr);

b8 gc_is_in_heap(GC* gc, void* ptr);
void gc_mark(GC* gc, void* ptr);
u64 gc_collect_fun(GC* gc); // Returns the recycled size (in blocks)
b8 gc_shutdown_fun(GC* gc);

void gc_log_fun(GC* gc, const char* s);
void gc_log_full_fun(GC* gc, const char* s);


static GC* __GC;

void gc_init(u32 heap_size, void* stack_beginning);

void* c_malloc(u64 size);
b8 c_realloc(void** ptr);
b8 c_free(void* ptr);
void gc_log(const char* s);
void gc_log_full(const char* s);
u64 gc_collect();
b8 gc_shutdown();


#endif