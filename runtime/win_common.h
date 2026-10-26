#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#define PHX_ATTACH_MAGIC 0x50585835u
typedef struct PhoenixStartConfig {
    uint32_t magic,size;
    WCHAR logPath[1024];
} PhoenixStartConfig;
/* Replaces the Windows user name in C:\Users\<name>\ with "~" so logs and reports never show it. */
void phx_hide_user_names(char *s, size_t cap);
int phx_verify_server_file(const WCHAR *path);
int phx_read(HANDLE process,uint32_t address,void *buffer,unsigned bytes);
uint32_t phx_remote_module(DWORD pid,const WCHAR *name,const WCHAR *fullPath);
uint32_t phx_remote_system_proc(DWORD pid,const char *name);
uint32_t phx_remote_export(HANDLE process,uint32_t module,const char *name);
