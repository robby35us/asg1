#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>
#include "threads/old_palloc.h"
bool install_page (void *upage, void *kpage, bool writable);
void palloc_init (size_t user_page_limit);
void *palloc_get_page (enum palloc_flags);
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt);
bool palloc_multiple (enum palloc_flags flags, size_t page_cnt, void * upage, bool writable);
void * palloc_page (enum palloc_flags flags, void * upage, bool writable);
void palloc_free_page (void *);
void palloc_free_multiple (void *, size_t page_cnt);
void palloc_free_upage (void *);
void palloc_free_umultiple (void *, size_t page_cnt);


#endif /* threads/palloc.h */
