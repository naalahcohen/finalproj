#include "malloc.h"


#define UINT64_MAX ((uint64_t)-1)

typedef struct free_block {
    size_t size;                 // Size of the block, including header
    struct free_block* next;     // Pointer to the next free block
    struct free_block* prev;     // Pointer to the previous free block
    int freed;                   // Indicates whether the block is free (1 for free, 0 for allocated)
} free_block;

typedef struct ptr_with_size {
    void* ptr;
    long size;
} ptr_with_size;

static ptr_with_size *allocation_array;
static size_t array_index = 0;


static free_block* head = NULL; // Start of the free list
static void* heap_start = NULL;           // Start of the heap
static void* heap_end = NULL;             // End of the heap
static int true = 0 ; 
static int total_allocations = 0;




void initialize_heap() {
    if (!heap_start) {
        heap_start = sbrk(0); // Get current program break
        heap_end = heap_start;
    }
}

void free(void* ptr) {
    if (!ptr) {
        // app_printf(0, "free: Null pointer passed, nothing to free.\n");
        return;
    }

    total_allocations--;
    // app_printf(0, "free: Pointer %p received for freeing. Total allocations now: %d\n", 
    //            ptr, total_allocations);

    // Get block header
    free_block* block = (free_block*)((char*)ptr - sizeof(free_block));
    
    // Just mark as freed but DO NOT clear next/prev yet
    block->freed = 1;
    
    // app_printf(0, "free: Block at %p marked as freed. Size: %d\n", block, block->size);

    // If the block was already in the list, we're done
    // This handles blocks that were allocated but remain in the list
    if (block->next || block->prev || block == head) {
        // app_printf(0, "free: Block already in list, just marked as freed\n");
        return;
    }

    // Find position to insert block
    free_block* current = head;
    free_block* prev = NULL;

    while (current && current < block) {
        prev = current;
        current = current->next;
    }

    // Insert block into list
    if (prev) {
        prev->next = block;
        block->prev = prev;
    } else {
        head = block;
        block->prev = NULL;
    }

    if (current) {
        current->prev = block;
        block->next = current;
    } else {
        block->next = NULL;
    }

    // Try to coalesce with next block
    if (block->next && block->next->freed && 
        (char*)block + block->size == (char*)block->next) {
        // app_printf(0, "free: Coalescing with next block\n");
        block->size += block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    // Try to coalesce with previous block
    if (block->prev && block->prev->freed && 
        (char*)block->prev + block->prev->size == (char*)block) {
        // app_printf(0, "free: Coalescing with previous block\n");
        block->prev->size += block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }

    // app_printf(0, "free: Final block list:\n");
    current = head;
    while (current) {
        // app_printf(0, "  Block at %p: size=%d freed=%d next=%p prev=%p\n",
        //           current, current->size, current->freed, 
        //           current->next, current->prev);
        current = current->next;
    }
}


void* malloc(uint64_t sz) {
    if (sz == 0) {
        return NULL; // Do not allocate zero-sized memory
    }

    // Align size to 8 bytes
    uint64_t align_size = (sz + 7) & ~7; // Round up to the nearest multiple of 8
    uint64_t total_size = align_size + sizeof(free_block);

    // Find the best-fitting free block
    free_block* best_fit = NULL;
    free_block* current = head;
    uint64_t min_size_diff = UINT64_MAX;

    while (current) {
        if (current->freed == 1 && current->size >= total_size) {
            uint64_t size_diff = current->size - total_size;
            if (size_diff < min_size_diff) {
                min_size_diff = size_diff;
                best_fit = current;
            }
        }
        current = current->next;
    }

    // If a suitable block was found, allocate from it
    if (best_fit) {
        // If there's enough space left in the block, split it
        if (best_fit->size >= total_size + sizeof(free_block) + 8) {
            free_block* new_block = (free_block*)((char*)best_fit + total_size);
            new_block->size = best_fit->size - total_size;
            new_block->freed = 1;
            new_block->next = best_fit->next;
            new_block->prev = best_fit;

            best_fit->size = total_size;
            best_fit->next = new_block;
            if (new_block->next) {
                new_block->next->prev = new_block;
            }
        }

        // Mark the block as not free and return the usable memory region
        best_fit->freed = 0;
        total_allocations++;
        return (char*)best_fit + sizeof(free_block);
    }

    // If no suitable block was found, request more memory from the system
    void* new_block_addr = sbrk(total_size);
    if (new_block_addr == (void*)-1) {
        return NULL; // Failed to allocate memory
    }

    // Initialize the new block
    free_block* new_block = (free_block*)new_block_addr;
    new_block->size = total_size;
    new_block->freed = 0;
    new_block->next = NULL;
    new_block->prev = NULL;

    // Add the new block to the free list
    if (head == NULL) {
        head = new_block;
    } else {
        // Traverse to the end of the list and append the new block
        current = head;
        while (current->next) {
            current = current->next;
        }
        current->next = new_block;
        new_block->prev = current;
    }

    total_allocations++;
    return (char*)new_block + sizeof(free_block);
}



void * calloc(uint64_t num, uint64_t sz) {
    // Ensure that num and sz are not zero
    if (num == 0 || sz == 0) {
        return NULL; // Or return a unique pointer that can be freed
    }

    // Check for multiplication overflow
    if (num > UINT64_MAX / sz) {
        // Handle overflow error
        return NULL;
    }

    size_t total_size = (size_t)(num * sz);

    // Proceed with allocation
    void* ptr = malloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

void * realloc(void * ptr, uint64_t sz) {
    if (!ptr) return malloc(sz);        // Equivalent to malloc
    if (sz == 0) {
        free(ptr);                        // Equivalent to free
        return NULL;
    }

    free_block* block = (free_block*)((char*)ptr - sizeof(free_block));
    if (block->size >= sz + sizeof(free_block)) {
        return ptr; // Block is already large enough
    }

    void* new_ptr = malloc(sz); // Allocate a new block
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size - sizeof(free_block));
        free(ptr);
    }
    return new_ptr;
}


void defrag() {
    if (!head) {
        return;
    }

    // app_printf(0, "Starting defrag\n");
    // print_free_chunks("before defrag");

    int did_merge;
    do {
        did_merge = 0;
        free_block* current = head;
        
        while (current && current->next) {
            free_block* next = current->next;
            
            if (current->freed && next->freed) {
                char* current_end = (char*)current + current->size;
                
                // app_printf(0, "Checking blocks: current=%p (size=%d) next=%p (size=%d)\n",
                //           current, current->size, next, next->size);
                // app_printf(0, "  current_end=%p\n", current_end);
                
                if (current_end == (char*)next) {
                    // app_printf(0, "  Merging blocks! New size will be %d\n", 
                    //          current->size + next->size);
                    current->size += next->size;
                    current->next = next->next;
                    if (next->next) {
                        next->next->prev = current;
                    }
                    did_merge = 1;
                    // Stay on current block to check for more merges
                } else {
                    // app_printf(0, "  Blocks not adjacent\n");
                    current = next;
                }
            } else {
                current = next;
            }
        }
    } while (did_merge);

    // app_printf(0, "Defrag complete\n");
    // print_free_chunks("after defrag");
}




//code from: https://www.geeksforgeeks.org/c-program-for-merge-sort/
//modified so that it sorts from largest to smallest instead of vice versa
//like in the orignal code from the website 
// Modify merge and mergeSort for long arrays
void mergeLong(long arr[], int left, int mid, int right) {
    int i, j, k;
    int n1 = mid - left + 1;
    int n2 = right - mid;

    // Create temporary arrays
    long leftArr[n1], rightArr[n2];

    // Copy data to temporary arrays
    for (i = 0; i < n1; i++)
        leftArr[i] = arr[left + i];
    for (j = 0; j < n2; j++)
        rightArr[j] = arr[mid + 1 + j];

    // Merge the temporary arrays back into arr[left..right]
    i = 0;
    j = 0;
    k = left;
    while (i < n1 && j < n2) {
        // Sort from largest to smallest
        if (leftArr[i] >= rightArr[j]) {
            arr[k] = leftArr[i];
            i++;
        }
        else {
            arr[k] = rightArr[j];
            j++;
        }
        k++;
    }

    // Copy the remaining elements of leftArr[], if any
    while (i < n1) {
        arr[k] = leftArr[i];
        i++;
        k++;
    }

    // Copy the remaining elements of rightArr[], if any
    while (j < n2) {
        arr[k] = rightArr[j];
        j++;
        k++;
    }
}

void mergeSort(long arr[], int left, int right) {
    if (left < right) {
        // Calculate the midpoint
        int mid = left + (right - left) / 2;

        // Sort first and second halves
        mergeSort(arr, left, mid);
        mergeSort(arr, mid + 1, right);

        // Merge the sorted halves
        mergeLong(arr, left, mid, right);
    }
}

int heap_info(heap_info_struct* info) {
    if (!info) {
        return -1;
    }
    // print_free_chunks("before collecting info"); 

    // First pass: count allocations and gather free space info 
    int counted_allocs = 0;  // separate counter for debugging
    info->free_space = 0;
    info->largest_free_chunk = 0;

    free_block* current = head;
    //app_printf(0, "Starting list traversal. head=%p\n", head);
    
    while (current) {
        if (current->freed) {
            info->free_space += current->size;
            if ((long)current->size > info->largest_free_chunk) {
                info->largest_free_chunk = (long)current->size;
                // app_printf(0, "Found larger free chunk: %ld at %p\n", 
                //           info->largest_free_chunk, current);
            }
        } else {
            counted_allocs++;
        }
        current = current->next;
    }


    // app_printf(0, "List traversal found %d allocations, global count is %d\n", 
    //           counted_allocs, total_allocations);
    
    // Use the global count since it's working
    info->num_allocs = total_allocations;
    if (info->num_allocs == 0) {
        info->size_array = NULL;
        info->ptr_array = NULL;
        return 0;
    }
    //  app_printf(0, "heap_info results: allocations=%d, free_space=%ld, largest_chunk=%ld\n",
    //            info->num_allocs, info->free_space, info->largest_free_chunk);
    // Instead of using malloc, use a pre-allocated buffer or a dedicated region
    // Assuming we have a static buffer or can use sbrk directly:
    static long size_buffer[1024];  // Adjust size as needed
    static void* ptr_buffer[1024];  // Adjust size as needed

    
    if (info->num_allocs > 1024) {  // Or whatever size you choose
        return -1;
    }

    info->size_array = size_buffer;
    info->ptr_array = ptr_buffer;

    // Second pass: fill arrays with allocated blocks
    current = head;
    int b = 0;
    while (current) {
        if (current->freed == 0) {
            // Store info about allocated block
            info->size_array[b] = (long)current->size - sizeof(free_block);  // Return actual usable size
            info->ptr_array[b] = (void*)((char*)current + sizeof(free_block));
            b++;
        }
        current = current->next;
    }

    // Bubble sort instead of merge sort (simpler and we're dealing with small arrays)
    for (int i = 0; i < info->num_allocs - 1; i++) {
        for (int j = 0; j < info->num_allocs - i - 1; j++) {
            if (info->size_array[j] < info->size_array[j + 1]) {
                // Swap sizes
                long temp_size = info->size_array[j];
                info->size_array[j] = info->size_array[j + 1];
                info->size_array[j + 1] = temp_size;
                
                // Swap pointers
                void* temp_ptr = info->ptr_array[j];
                info->ptr_array[j] = info->ptr_array[j + 1];
                info->ptr_array[j + 1] = temp_ptr;
            }
        }
    }

    return 0;
}
