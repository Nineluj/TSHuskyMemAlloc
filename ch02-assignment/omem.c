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

//structure to represent a bin
typedef struct nu_bin {
    size_t bin_size;
    nu_free_cell* head;
} nu_bin;

static const int64_t CHUNK_SIZE = 2048;
static const int64_t CELL_SIZE  = 32; //(int64_t)sizeof(nu_free_cell);
#define NUM_BINS 7
static long nu_malloc_chunks = 0;
static long nu_free_chunks = 0;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

//global bins to store data
static nu_bin bins[NUM_BINS];

//flag to determine if we need to initialize the bins
int bins_init = 0;

/*
//TODO: remove this?
int64_t
nu_free_list_length(nu_free_cell* nu_free_list)
{
    int len = 0;

    for (nu_free_cell* pp = nu_free_list; pp != 0; pp = pp->next) {
        len++;
    }

    return len;
}*/

//TODO: remove this?
/*void
nu_print_free_list(nu_free_cell* nu_free_list)
{
    nu_free_cell* pp = nu_free_list;
    printf("= Free list: =\n");

    for (; pp != 0; pp = pp->next) {
        printf("%lx: (cell %ld %lx)\n", (int64_t) pp, pp->size, (int64_t) pp->next);

    }
}*/

//insert the given cell into the bin at given index
static
void
nu_bin_insert(int index, nu_free_cell* cell)
{
    nu_free_cell* head = bins[index].head;
    if(head == NULL) {
        bins[index].head = cell;
    }
    else {
        //push the item to the beginning of the free_list by moving everything forward by 1 element
        head->prev = cell;
        cell->prev = NULL;
        cell->next = head;
        *head = *cell;
    }
}

//TODO: not sure if this is needed
//static
/*nu_free_cell*
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
*/

//mmaps a cell of CHUNK_SIZE and returns it.  Used in the bin initialization
static
nu_free_cell*
make_cell()
{
    void* addr = mmap(0, CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    nu_free_cell* cell = (nu_free_cell*) addr;
    cell->size = CHUNK_SIZE;
    return cell;
}

nu_free_cell* nu_remove_head(int bin_index) {
    nu_free_cell* ret_val = bins[bin_index].head;
    bins[bin_index].head = bins[bin_index].head->next;
    bins[bin_index].head->prev = NULL;
    return ret_val;
}

//mmap more data to our largest bin
void add_more_big_space() {
    for(int j = 0; j < 100; j++) {
        nu_free_cell* cell = make_cell();
        nu_bin_insert(NUM_BINS - 1, cell);
    }
}

//function to initialize the bins, by setting all low bins to null and inserting a lot of mapped memory into the largest bin
void initialize_bins() {
    int i;
    //initialize the sizes of the bins and set content to null
    for(i = 0; i < NUM_BINS; i++) {
           bins[i].bin_size = 1 << (i+5);  //i+5 is starting from 2^5 (32)
           bins[i].head = NULL;
    }

    //insert mmapped blocks into the 2048 size bin
    add_more_big_space();
    bins_init = 1;
}

//routine to find a block inside the given bin index.  It calls itself recursively, breaking down large bins as needed
//and the alloc_size should remain unchanged across recursive calls
void* ofind_data(int index, int64_t alloc_size) {
    printf("find data index: %d\n", index);
    printf("allocation size: %d\n", alloc_size);
    if(index >= NUM_BINS) {
        printf("need to add more space\n");
        add_more_big_space();
        return ofind_data(NUM_BINS-1, alloc_size);
    }

    //if the current bin is empty
    if(bins[index].head == NULL) {
        return ofind_data(index+1, alloc_size);
    }
    else {
        if(bins[index].bin_size == alloc_size) {
            printf("found correct bin\n");
            return nu_remove_head(index);
        }
        else {
            //remove current block to be split
            nu_free_cell* cur_data = nu_remove_head(index);
            int64_t new_size = bins[index - 1].bin_size;
            //set new size to lower bin size
            cur_data->size = new_size;
            //insert first half into the smaller bin
            nu_bin_insert(index - 1, cur_data);
            //move pointer for second half
            nu_free_cell* data2 = (void*)cur_data + new_size;
            //set new size
            data2->size = new_size;
            //insert into bin of smaller size
            nu_bin_insert(index - 1, data2);
            //recursively call on the smaller size bin
            return ofind_data(index - 1, alloc_size);
        }
    }
}

//optimal malloc function
void*
omalloc(size_t usize)
{
    pthread_mutex_lock(&mutex);
    if (!bins_init) {
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

    //go through all bins to find the bin index that is just large enough for the requested size
    for(int i = 0; i < NUM_BINS; i++) {
        if(bins[i].bin_size >= alloc_size) {
           cell = ofind_data(i, bins[i].bin_size);
           break;
        }
    }

    printf("here\n");

    //set the size of the cell
    *((int64_t*)cell) = alloc_size;
    pthread_mutex_unlock(&mutex);
    return ((void*)cell) + sizeof(int64_t);
}

void
ofree(void* addr)
{
    pthread_mutex_lock(&mutex);
    //get size of given address
    nu_free_cell* cell = (nu_free_cell*)(addr - sizeof(int64_t));
    int64_t size = *((int64_t*) cell);

    //if given size is larger than our largest bin, unmap it
    if (size > CHUNK_SIZE) {
        nu_free_chunks += 1;
        munmap((void*) cell, size);
    }
    else {
        //go through all of the bins and insert the data into the correct bin
        for(int i = 0; i < NUM_BINS; i++) {
            if(bins[i].bin_size == size) {
                nu_bin_insert(i, cell);
                break;
            }
        }
    }

    pthread_mutex_unlock(&mutex);
    //nu_bin_coalesce();
}

void*
orealloc(void* addr, size_t size)
{
    void* new_addr = omalloc(size);
    memcpy(new_addr, addr, size);
    ofree(addr);
    return new_addr;
}
