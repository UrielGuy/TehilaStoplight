#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
inline void* heap_caps_malloc(size_t n, unsigned) { return malloc(n); }
inline void* heap_caps_realloc(void* p, size_t n, unsigned) { return realloc(p, n); }
inline void heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_total_size(unsigned) { return 300000; }
