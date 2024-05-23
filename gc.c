#include "gc.h"

//GC* __GC;

mem_block mem_info(u32 size, mem_state state){
    return (u64)(size + ((u64)state << 32));
}

mem_block mem_set_reach(mem_block m, b8 reachable){
    u64 one = 1; // That's ridiculous but warning if one is directly inserted
    if(reachable) return ((one<<63) | m);
    return (((one<<63) - 1) & m);
}

u32 mem_block_get_size(mem_block m){
    return (u32)(m & GC_LOWER_MASK);
}

b8 mem_block_is_reachable(mem_block m){
    return (m>>63);
}

mem_state mem_block_get_state(mem_block m){
    return (mem_state)(m >> 32);
}


void mem_block_log(mem_block m, const char* s){

    // Cannot initialize a string array easily...

    switch(mem_block_get_state(m)){
        case AVAILABLE:
            printf("%s : State = AVAILABLE, Size = %u\n", s,mem_block_get_size(m));
            break;

        case ALLOCATED:
            printf("%s : State = ALLOCATED, Size = %u\n", s, mem_block_get_size(m));
            break;

        case USING:
            printf("%s : State = IN USE, Size = %u\n", s, mem_block_get_size(m));
            break;

        default:
            printf("%s : State = UNKNOWN, Size = %u\n", s, mem_block_get_size(m));
            break;
    }
}


u64 bytes_to_index(u64 offset){
    return ((offset + (BYTE_SIZE - 1)) / BYTE_SIZE);
    //return ((offset + (BYTE_SIZE - 1)) >> 6);
}



void gc_init(u32 heap_size, void* stack_beginning){
    __GC = (GC*)malloc(sizeof(GC));
    __GC->heap_size = heap_size;
    __GC->heap = (u64*)malloc(heap_size);
    __GC->heap[0] = mem_info(heap_size, AVAILABLE);
    __GC->free_space = heap_size;
    __GC->first_free = 0;
    __GC->base_stack_pointer = stack_beginning;
    //return &__GC;
}


void* c_malloc_collect_fun(GC* gc, u64 size, b8 collect){
    
    if(size == 0) return NULL;

    u64 heap_index = (u64)gc->first_free;
    u32 required_blocks = bytes_to_index(size);
    u32 current_size = mem_block_get_size(gc->heap[gc->first_free]);
    while((heap_index < gc->heap_size) && 
            ((mem_block_get_state(gc->heap[heap_index]) != AVAILABLE) ||
            (current_size < (required_blocks + 1) ))){
                // Current_size holds descriptor block + available space
        heap_index += current_size;
        current_size = mem_block_get_size(gc->heap[heap_index]);

    }

    if(heap_index >= gc->heap_size && collect){
        gc_collect_fun(gc);
        return c_malloc_collect_fun(gc, size, false);
    }

    if(heap_index >= gc->heap_size) return NULL;

    // If place for another pointer (8 bytes or more remaining), 
    // add a new free pointer to the remainder

    if(current_size >= required_blocks + 2){
        gc->heap[heap_index] = mem_info(required_blocks + 1, ALLOCATED);
        gc->heap[heap_index + required_blocks + 1] = mem_info((current_size - required_blocks) - 1, AVAILABLE);
        gc->free_space -= required_blocks + 1;
        if(heap_index == gc->first_free) gc->first_free += required_blocks + 1;
    }

    else{
        gc->heap[heap_index] = mem_info(current_size, ALLOCATED);
        gc->free_space -= current_size;
        if(heap_index == gc->first_free) gc->first_free += current_size;
    }

    
    // Else : we did not allocate on first free block, no need to change

    return (void*)(&gc->heap[heap_index + 1]);
}

void* c_malloc_fun(GC* gc, u64 size){
    return c_malloc_collect_fun(gc, size, true);
}


b8 c_realloc_fun(GC* gc, void** ptr){

    if(*ptr < (void*)(&gc->heap[1]) || *ptr >= (void*)(&gc->heap[gc->heap_size - 1])){
        printf("c_realloc -> Address Outside of Heap!\n");
        return false;
    }

    u64 heap_index = 0;
    u32 current_size = mem_block_get_size(gc->heap[0]); 
    while(heap_index < gc->heap_size && *ptr > (void*)(&gc->heap[heap_index + 1])){
        heap_index += current_size;
        current_size = mem_block_get_size(gc->heap[heap_index]);
    }

    if(*ptr != &gc->heap[heap_index + 1]){
        printf("c_realloc -> Incorrect Address!\n");
        return false;
    }

    void* new_addr = c_malloc_fun(gc, (current_size - 1));
    if(new_addr >= *ptr) c_free_fun(gc, new_addr);
    if(new_addr == NULL || new_addr >= *ptr) return false;

    // Find first free block 
    while(heap_index < gc->heap_size && mem_block_get_state(gc->heap[heap_index]) != AVAILABLE){
        heap_index += current_size;
        current_size = mem_block_get_size(gc->heap[heap_index]);
    }

    gc->first_free = heap_index; // CAN BE OVER HEAP_SIZE IF HEAP IS FULL!

    array_copy(*ptr, new_addr, 0, current_size - 2);
    c_free_fun(gc, *ptr);
    *ptr = new_addr;
    return true;
}


b8 c_free_fun(GC* gc, void* ptr){

    if(ptr < (void*)(&gc->heap[1]) || ptr >= (void*)(&gc->heap[gc->heap_size - 1])){
        printf("c_free -> Cannot free an adress outside of the Heap!\n");
        return false;
    }

    u64 heap_index = 0;
    u64 previous_index = 0;
    u32 current_size = mem_block_get_size(gc->heap[0]);
    u32 previous_size = current_size; 
    while(heap_index < gc->heap_size && ptr > (void*)(&gc->heap[heap_index + 1])){
        previous_index = heap_index;
        previous_size = current_size;
        heap_index += current_size;
        current_size = mem_block_get_size(gc->heap[heap_index]);
    }

    if(ptr != &gc->heap[heap_index + 1]){
        printf("c_free -> Attempting to free an non-allocated address!\n");
        return false;
    }

    // Address is correct, see if next or previous block is also free to recombine

    u32 stitch_size = current_size;

    if(mem_block_get_state(gc->heap[heap_index + current_size]) == AVAILABLE){
        stitch_size = current_size + mem_block_get_size(gc->heap[heap_index + current_size]);
        gc->heap[heap_index] = mem_info(stitch_size, AVAILABLE);
    }

    else gc->heap[heap_index] = mem_info(current_size, AVAILABLE);

    if(heap_index > 0 && mem_block_get_state(gc->heap[previous_index]) == AVAILABLE)
        gc->heap[previous_index] = mem_info(previous_size + stitch_size, AVAILABLE);


    // Do not need to check with before, as it would contradict first_free
    if(heap_index < gc->first_free) gc->first_free = heap_index;

    gc->free_space += current_size;
    return true;

}


b8 gc_is_in_heap(GC* gc, void* ptr){
    if ((ptr <= (void*)gc->heap) || (ptr >= (void*)(gc->heap + gc->heap_size))) return false;

    u64 heap_index = 0;
    u32 current_size = mem_block_get_size(gc->heap[0]); 
    while(heap_index < gc->heap_size && ptr > (void*)(&gc->heap[heap_index + 1])){
        heap_index += current_size;
        current_size = mem_block_get_size(gc->heap[heap_index]);
    }

    if(heap_index >= gc->heap_size || ptr != &gc->heap[heap_index + 1]) return false;
    return true;
}

void gc_mark(GC* gc, void* ptr){
    if(!gc_is_in_heap(gc, ptr)) return;
    u64* cast_ptr = (u64*)ptr;
    u64* desc_block = (cast_ptr - 1);
    *desc_block = mem_set_reach(*desc_block, true);
    u32 size = mem_block_get_size(*desc_block);
    for(u32 i = 1; i < size; i++){
        if(gc_is_in_heap(gc, (void*)(desc_block + i))) gc_mark(gc, (void*)(desc_block + i));
    }
}



u64 gc_collect_fun(GC* gc){
    
    // Stack traversal : Mark every root as reachable in stack && every son of reachable object
    // as reachable
    
    u64* stack_upper = (u64*) __builtin_frame_address(0); // Stack upper grows towards 0!

    for(u64 offset = 0; (void*)(stack_upper + offset) < gc->base_stack_pointer; offset++){
        void* ptr_reference = (void*)*(stack_upper + offset);
        if(gc_is_in_heap(gc, ptr_reference)){
            gc_mark(gc, ptr_reference);
        }
    }

    // Heap traversal : If a block is unreachable, free it!

    u64 heap_index = 0;
    u64 freed_size = 0;
    u32 current_size = mem_block_get_size(gc->heap[0]);
    while(heap_index < gc->heap_size){
        if(mem_block_get_state(gc->heap[heap_index]) == ALLOCATED &&
         !mem_block_is_reachable(gc->heap[heap_index])){

            c_free_fun(gc, &gc->heap[heap_index + 1]);
            freed_size += current_size;
         }

        heap_index += current_size;
        current_size = mem_block_get_size(gc->heap[heap_index]);

    }

    return freed_size;

}



b8 gc_shutdown_fun(GC* gc){
    free(gc->heap);
    return true;
}



void gc_log_fun(GC* gc, const char* s){
    printf("%s : \n * Total size = %u Bytes\n * Free space = %u Bytes\n * Allocated space = %u Bytes\n\n",
    s, gc->heap_size * 8, gc->free_space * 8, (gc->heap_size - gc->free_space) * 8);
}

void gc_log_full_fun(GC* gc, const char* s){
    printf("%s : \n * Total size = %u Bytes\n * Free space = %u Bytes\n * Allocated space = %u Bytes\n+--------------\n",
    s, gc->heap_size * 8, gc->free_space * 8, (gc->heap_size - gc->free_space) * 8);

    u64 heap_index = 0;
    u32 current_size = mem_block_get_size(gc->heap[0]); 
    while(heap_index < gc->heap_size){
        printf("[Block #%llu]", heap_index);
        mem_block_log(gc->heap[heap_index], "");
        heap_index += current_size;
        current_size = mem_block_get_size(gc->heap[heap_index]);
    }

    printf("+--------------\n\n");

}





// CDECL FUNCTIONS

void* c_malloc(u64 size){
    return c_malloc_fun(__GC, size);
}

b8 c_realloc(void** ptr){
    return c_realloc_fun(__GC, ptr);
}

b8 c_free(void* ptr){
    return c_free_fun(__GC, ptr);
}


u64 gc_collect(){
    return gc_collect_fun(__GC);
}

b8 gc_shutdown(){
    return gc_shutdown_fun(__GC);
}


void gc_log(const char* s){
    return gc_log_fun(__GC, s);
}

void gc_log_full(const char* s){
    return gc_log_full_fun(__GC, s);
}