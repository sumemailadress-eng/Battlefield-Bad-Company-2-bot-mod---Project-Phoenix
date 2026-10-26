/* Minimal Windows shims so the runtime's world/bots/nav glue compile on Linux for the offline harness. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <strings.h>
#define _strnicmp strncasecmp
typedef uint32_t DWORD; typedef int32_t LONG; typedef void *HANDLE; typedef wchar_t WCHAR;
#define WINAPI
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
static inline LONG InterlockedExchange(volatile LONG *t, LONG v) { return __sync_lock_test_and_set(t, v); }
typedef DWORD (*LPTHREAD_START_ROUTINE)(void *);
static inline HANDLE CreateThread(void *a, size_t s, LPTHREAD_START_ROUTINE fn, void *arg, DWORD f, DWORD *id)
{ (void)a; (void)s; (void)f; (void)id; fn(arg); return (HANDLE)1; }
static inline int SetThreadPriority(HANDLE h, int p) { (void)h; (void)p; return 1; }
static inline int CloseHandle(HANDLE h) { (void)h; return 1; }
static inline DWORD GetTickCount(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (DWORD)(t.tv_sec * 1000 + t.tv_nsec / 1000000); }
int harness_scan(uint32_t value, uint32_t *out, int max);
