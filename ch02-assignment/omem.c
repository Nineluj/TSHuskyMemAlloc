// CS3650 CH02 starter code
// Fall 2017
//
// Author: Nat Tuck
// Once you've read this, you're done
// with HW07.


#include <stdint.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

#include "omem.h"

typedef struct nu_free_cell {
    int64_t              size;
    struct nu_free_cell* next;
    struct nu_free_cell* prev;
} nu_free_cell;

typedef struct nu_bin {
    size_t bin_size;
    nu_free_cell* head;
} nu_bin;

static const int64_t CHUNK_SIZE = 2048;
static const int64_t CELL_SIZE  = (int64_t)sizeof(nu_free_cell);
static const int NUM_BINS = 7;

static long nu_malloc_chunks = 0;
static long nu_free_chunks = 0;

__thread static nu_bin bins[NUM_BINS];
bool bins_init = false;

int64_t
nu_free_list_length(nu_free_cell* nu_free_list)
{
    int len = 0;

    for (nu_free_cell* pp = nu_free_list; pp != 0; pp = pp->next) {
        len++;
    }

    return len;
}

void
nu_print_free_list(nu_free_cell* nu_free_list)
{
    nu_free_cell* pp = nu_free_list;
    printf("= Free list: =\n");

    for (; pp != 0; pp = pp->next) {
        printf("%lx: (cell %ld %lx)\n", (int64_t) pp, pp->size, (int64_t) pp->next); 

    }
}

static
void
nu_free_list_insert(nu_free_cell* nu_free_list, nu_free_cell* cell)
{
    if (nu_free_list == 0 || ((uint64_t) nu_free_list) > ((uint64_t) cell)) {
        cell->next = nu_free_list;
        cell->prev = NULL;
        nu_free_list = cell;
        return;
    }

    nu_free_cell* pp = nu_free_list;

    while (pp->next != 0 && ((uint64_t)pp->next) < ((uint64_t) cell)) {
        pp = pp->next;
    }

    cell->next = pp->next;
    cell->prev = pp;
    pp->next = cell;
}

static
nu_free_cell*
free_list_get_cell(int64_t size)
{
    nu_free_cell** prev = &nu_free_list;

    for (nu_free_cell* pp = nu_free_list; pp != 0; pp = pp->next) {
        if (pp->size >= size) {
            *prev = pp->next;
            return pp;
        }
        prev = &(pp->next);
    }
    return 0;
}

static
nu_free_cell*
make_cell()
{
    void* addr = mmap(0, CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    nu_free_cell* cell = (nu_free_cell*) addr; 
    cell->size = CHUNK_SIZE;
    return cell;
}

void initialize_bins() {
    int i;
    for(i = 0; i < NUM_BINS; i++) {
           bins[i].size = 1 << i+5;
           bins[i].head = NULL;
    }

    
    for(int j = 0; j < 100; j++) {
        nu_free_cell cell = make_cell();
        nu_free_list_insert(bins[NUM_BINS].head, &cell);
    }
}

void* ofind_data(int index, int64_t alloc_size) {
    if(bin[index].head == NULL) {
        return ofind_data(index+1, alloc_size; 
    }
    else {
        if(bin[index].size == alloc_size) {
            return nu_remove_head(bin[index].head);
        }
        else {
            void* cur_data = nu_remove_head(bin[index].head);
            int64_t new_size = bin[index - 1].size;

        }
    }
    //find the correct bin
    //take it from the start of bin if the head is not null
    //if the head is null, look one bin forward
    //if you find a larger bin, keep splitting in half until you are at the correct power of 2 to give to the user
}

void*
omalloc(size_t usize)
{
    if(!bins_init) {
        initialize_bins();
    }

    int64_t size = (int64_t) usize;

    // space for size
    int64_t alloc_size = size + sizeof(int64_t);

    // space for free cell when returned to list
    if (alloc_size < CELL_SIZE) {
        alloc_size = CELL_SIZE;
    }

    // TODO: Handle large allocations.
    if (alloc_size > CHUNK_SIZE) {
        void* addr = mmap(0, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        *((int64_t*)addr) = alloc_size;
        nu_malloc_chunks += 1;
        return addr + sizeof(int64_t);
    }

    void* cell;

    for(int i = 0; i < 0; i++) {
        if(bin[i].size >= alloc_size) {
           cell = ofind_data(i, bin[i].size);
        }
    }

    *((int64_t*)cell) = alloc_size;
    return ((void*)cell) + sizeof(int64_t);
}

void
ofree(void* addr) 
{
    nu_free_cell* cell = (nu_free_cell*)(addr - sizeof(int64_t));
    int64_t size = *((int64_t*) cell);

    if (size > CHUNK_SIZE) {
        nu_free_chunks += 1;
        munmap((void*) cell, size);
    }
    else {
        cell->size = size;
        //insert the block of memory into the correct bin based on the size
        //nu_free_list_insert(cell);
    }

    nu_bin_coalesce();
    
}

void* 
orealloc(void* addr, size_t size) 
{
    void* new_addr = omalloc(size);
    memcpy(new_addr, addr, size);
    ofree(addr);
    return new_addr;
}
