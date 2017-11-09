/*
 *  Basically what was wrongi was that before each call to insert, we were not setting the cell's previous and next field to null.
 *  This meant that when we inserted, we were still getting some of the previous and next from the last place it was in during a split.
 *  I also did something similar in the free function to set the previous and next to null before I inserted it back into the bin.  
 * 
 */
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
nu_bin_insert(int index, nu_free_cell* cell) {  
    nu_free_cell* head = bins[index].head;
    
    if(head == NULL || head > cell) {
        bins[index].head = cell;
        return;
    }
    else {
        nu_free_cell* current = head;
        for(;current->next!=NULL; current = current->next) {
            if(cell < current) {
                cell->prev = current->prev;
                cell->next = current;
                current->prev->next = cell;
                current->prev = cell;
                return;
            }
        }
        //set cell to be after current because the current is the last element in the free list
        cell->prev = current;
        cell->next= NULL;
        current->next = cell;
    }
}

void nu_remove(nu_free_cell* to_remove) {
    nu_free_cell* prev = to_remove->prev;
    nu_free_cell* next = to_remove->next;
    if(prev != NULL) {
        prev->next = next;
    }
    if(next != NULL) {
        next->prev = prev;
    }
}

void nu_bin_coalesce(int index, nu_free_cell* to_coalesce) {
    int64_t size = to_coalesce->size;
    nu_free_cell* next = to_coalesce->next;
    nu_free_cell* prev = to_coalesce->prev;
    nu_free_cell* new_block;
    //check the next cell
    if(index < NUM_BINS-1) {
        if(next != NULL) {
            if((int64_t)to_coalesce + size == (int64_t)next) {
                nu_remove(next);
                nu_remove(to_coalesce);
                new_block = to_coalesce;
                new_block->size = size << 1;
                new_block->next = NULL;
                new_block->prev = NULL;
                nu_bin_insert(index+1, new_block);
                nu_bin_coalesce(index+1, new_block);
                return;
            }
        }
        if(prev != NULL) {
            if((int64_t)prev + prev->size == (int64_t)to_coalesce) {
                nu_remove(prev);
                nu_remove(to_coalesce);
                new_block = prev;
                new_block->size = size << 1;
                new_block->next = NULL;
                new_block->prev = NULL;
                nu_bin_insert(index+1, new_block);
                nu_bin_coalesce(index+1, new_block);
            }
        }
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
    //TODO: remove this print
    if(ret_val == NULL) {
        printf("TRYING TO REMOVE HEAD FROM EMPTY LIST\n");
    }

    nu_free_cell* new_head = ret_val->next;
    if(new_head != NULL) {
        new_head->prev = NULL;
    }
    bins[bin_index].head = new_head;
    
    return ret_val;
}

//mmap more data to our largest bin
void add_more_big_space() {
    for(int j = 0; j < 10; j++) {
        nu_free_cell* cell = make_cell();
        cell->size = 2048;
        cell->next = NULL;
        cell->prev = NULL;
        nu_bin_insert(NUM_BINS - 1, cell);
        cell = (void*)cell + 2048;
        cell->size = 2048;
        cell->prev = NULL;
        cell->next = NULL;
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
    if(index >= NUM_BINS) {
        add_more_big_space();
        return ofind_data(NUM_BINS-1, alloc_size);
    }

    //if the current bin is empty
    if(bins[index].head == NULL) {
        return ofind_data(index+1, alloc_size);
    }
    else {
        if(bins[index].bin_size == alloc_size) {
            return nu_remove_head(index);
        }
        else {
            //remove current block to be split
            nu_free_cell* cur_data = nu_remove_head(index);
            int64_t new_size = bins[index - 1].bin_size;
            //set new size to lower bin size
            cur_data->size = new_size;
            //set the prev and next to null
            cur_data->prev = NULL;
            cur_data->next = NULL;
            //insert first half into the smaller bin
            nu_bin_insert(index - 1, cur_data);
            //move pointer for second half
            nu_free_cell* data2 = (void*)cur_data + new_size;
            //set new size
            data2->size = new_size;
            //set the prev and next to null
            data2->prev = NULL;
            data2->next = NULL;
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

        pthread_mutex_unlock(&mutex);
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
    int index;
    cell->size = size;
    cell->prev = NULL;
    cell->next = NULL;

    //if given size is larger than our largest bin, unmap it
    if (size > CHUNK_SIZE) {
        nu_free_chunks += 1;
        munmap((void*) cell, size);
    }
    else {
        //go through all of the bins and insert the data into the correct bin
        for(int i = 0; i < NUM_BINS; i++) {
            if(bins[i].bin_size == size) {
                index = i;
                nu_bin_insert(index, cell);
                break;
            }
        }
    }
    nu_bin_coalesce(index, cell);
    pthread_mutex_unlock(&mutex);
}

void*
orealloc(void* addr, size_t size)
{
    void* new_addr = omalloc(size);
    memcpy(new_addr, addr, size);
    ofree(addr);
    return new_addr;
}
