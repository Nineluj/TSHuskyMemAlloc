#ifndef HMEM_H
#define HMEM_H

// Husky Malloc Interface
// cs3650 Starter Code

typedef struct hm_stats {
    long pages_mapped;
    long pages_unmapped;
    long nu_malloc_chunks;
    long nu_free_chunks;
    long free_length;
} hm_stats;

hm_stats* hgetstats();
void hprintstats();

void* hmalloc(size_t size);
void hfree(void* item);
void* hrealloc(void* addr, size_t size);

#endif
