#pragma once
#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
#define HEAP_ZERO_MEMORY 8
static inline void *GetProcessHeap(void) { return NULL; }
static inline void *HeapAlloc(void *heap, unsigned flags, size_t bytes)
{ (void)heap; return flags & HEAP_ZERO_MEMORY ? calloc(1,bytes) : malloc(bytes); }
static inline int HeapFree(void *heap, unsigned flags, void *ptr)
{ (void)heap; (void)flags; free(ptr); return 1; }
static inline int QueryPerformanceFrequency(LARGE_INTEGER *v)
{ v->QuadPart=1000000000; return 1; }
static inline int QueryPerformanceCounter(LARGE_INTEGER *v)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); v->QuadPart=(int64_t)t.tv_sec*1000000000+t.tv_nsec; return 1; }
static inline unsigned GetTickCount(void)
{ LARGE_INTEGER t; QueryPerformanceCounter(&t); return (unsigned)(t.QuadPart/1000000); }
#endif
