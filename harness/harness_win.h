/* Minimal Windows shims so the runtime's world/bots/nav glue compile on Linux for the offline harness. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#undef near
#else
#include <time.h>
#include <strings.h>
#define _strnicmp strncasecmp
typedef uint32_t DWORD; typedef int32_t LONG; typedef void *HANDLE; typedef wchar_t WCHAR;
#define WINAPI
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
static inline LONG InterlockedExchange(volatile LONG *t, LONG v) { return __sync_lock_test_and_set(t, v); }
typedef DWORD (*LPTHREAD_START_ROUTINE)(void *);
static inline DWORD GetTickCount(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (DWORD)(t.tv_sec * 1000 + t.tv_nsec / 1000000); }
#endif

/* Keep threaded work synchronous so the mock world advances only on the harness tick. */
static inline HANDLE harness_create_thread(void *a, size_t s, LPTHREAD_START_ROUTINE fn, void *arg, DWORD f, DWORD *id)
{ (void)a; (void)s; (void)f; (void)id; fn(arg); return (HANDLE)1; }
static inline int harness_set_thread_priority(HANDLE h, int p) { (void)h; (void)p; return 1; }
static inline int harness_close_handle(HANDLE h) { (void)h; return 1; }
#define CreateThread harness_create_thread
#define SetThreadPriority harness_set_thread_priority
#define CloseHandle harness_close_handle
int harness_scan(uint32_t value, uint32_t *out, int max);
