#ifndef HMEM_H
#define HMEM_H

// Husky Malloc Interface
// cs3650 Starter Code

typedef struct om_stats {
    long pages_mapped;
    long pages_unmapped;
    long nu_malloc_chunks;
    long nu_free_chunks;
    long free_length;
} om_stats;

om_stats* ogetstats();
void oprintstats();

void* omalloc(size_t size);
void ofree(void* item);
void* orealloc(void* addr, size_t size);

#endif
