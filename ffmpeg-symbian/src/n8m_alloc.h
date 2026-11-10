#ifndef N8M_ALLOC_H
#define N8M_ALLOC_H

#include <stddef.h>

void *n8m_hook_malloc(size_t size);
void *n8m_hook_realloc(void *ptr, size_t size);
void n8m_hook_free(void *ptr);

#endif
