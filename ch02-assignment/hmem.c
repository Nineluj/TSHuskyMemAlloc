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

#include "hmem.h"

typedef struct nu_free_cell {
    int64_t              size;
    struct nu_free_cell* next;
    struct nu_free_cell* prev;
} nu_free_cell;

typedef struct nu_bin {
    int64_t size;
    nu_free_cell* head;
} nu_bin;

static const int64_t CHUNK_SIZE = 2048;
static const int64_t CELL_SIZE  = (int64_t)sizeof(nu_free_cell);

static long nu_malloc_chunks = 0;
static long nu_free_chunks = 0;

__thread nu_bin bins[7];

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
make_cell()
{
    void* addr = mmap(0, CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    nu_free_cell* cell = (nu_free_cell*) addr; 
    cell->size = CHUNK_SIZE;
    return cell;
}

void*
omalloc(size_t usize)
{
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

    nu_free_cell* cell = free_list_get_cell(alloc_size);
    if (!cell) {
        cell = make_cell();
    }

    // Return unused portion to free list.
    int64_t rest_size = cell->size - alloc_size;
    if (rest_size >= CELL_SIZE) {
        void* addr = (void*) cell;
        nu_free_cell* rest = (nu_free_cell*) (addr + alloc_size);
        rest->size = rest_size;
        nu_free_list_insert(rest);
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
        nu_free_list_insert(cell);
    }
}

void* 
orealloc(void* addr, size_t size) 
{
    void* new_addr = hmalloc(size);
    memcpy(new_addr, addr, size);
    hfree(addr);
    return new_addr;
}
