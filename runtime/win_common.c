#include "win_common.h"
#include <bcrypt.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>
int phx_read(HANDLE process,uint32_t address,void *buffer,unsigned bytes)
{
    SIZE_T done=0;
    if(!buffer || !bytes || address<0x10000u || (uint64_t)address+bytes>0x100000000ULL)return 0;
    return ReadProcessMemory(process,(void *)(uintptr_t)address,buffer,bytes,&done) && done==bytes;
}
int phx_verify_server_file(const WCHAR *path)
{
    static const unsigned char expected[32]={0xca,0x81,0x69,0xfa,0x03,0xa2,0x9e,0x02,0xaf,0x1b,0xf8,0xdb,0x87,0x70,0xab,0x0e,0x68,0xf1,0x3a,0x95,0xb0,0x10,0xc5,0x2e,0xca,0x23,0x73,0x30,0x68,0x29,0x83,0x47};
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;
    HANDLE file=INVALID_HANDLE_VALUE;unsigned char *object=NULL,digest[32],buffer[65536];DWORD bytes=0,objectSize=0,got=0;
    int result=0;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto cleanup;
    if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objectSize,sizeof objectSize,&got,0)<0)goto cleanup;
    object=HeapAlloc(GetProcessHeap(),0,objectSize);if(!object)goto cleanup;
    if(BCryptCreateHash(algorithm,&hash,object,objectSize,NULL,0,0)<0)goto cleanup;
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE)goto cleanup;
    do{
        if(!ReadFile(file,buffer,sizeof buffer,&bytes,NULL))goto cleanup;
        if(bytes && BCryptHashData(hash,buffer,bytes,0)<0)goto cleanup;
    }while(bytes);
    if(BCryptFinishHash(hash,digest,sizeof digest,0)<0)goto cleanup;
    result=!memcmp(digest,expected,32);
cleanup:
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(hash)BCryptDestroyHash(hash);
    if(object)HeapFree(GetProcessHeap(),0,object);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    return result;
}
uint32_t phx_remote_module(DWORD pid,const WCHAR *name,const WCHAR *fullPath)
{
    HANDLE snap=INVALID_HANDLE_VALUE;
    for(int n=0;n<10;n++){
        snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
        if(snap!=INVALID_HANDLE_VALUE || GetLastError()!=ERROR_BAD_LENGTH)break;
        Sleep(10);
    }
    if(snap==INVALID_HANDLE_VALUE)return 0;
    MODULEENTRY32W entry;memset(&entry,0,sizeof entry);entry.dwSize=sizeof entry;
    uint32_t result=0;
    if(Module32FirstW(snap,&entry))do{
        if((!name || !_wcsicmp(entry.szModule,name)) && (!fullPath || !_wcsicmp(entry.szExePath,fullPath))){
            result=(uint32_t)(uintptr_t)entry.modBaseAddr;break;
        }
    }while(Module32NextW(snap,&entry));
    CloseHandle(snap);return result;
}
uint32_t phx_remote_system_proc(DWORD pid,const char *name)
{
    FARPROC function=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name);HMODULE owner=NULL;WCHAR path[1024];
    if(!function || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)(uintptr_t)function,&owner))return 0;
    if(!GetModuleFileNameW(owner,path,1024))return 0;
    WCHAR *base=wcsrchr(path,L'\\');base=base?base+1:path;
    uint32_t remote=phx_remote_module(pid,base,NULL);
    return remote?remote+(uint32_t)((uintptr_t)function-(uintptr_t)owner):0;
}
uint32_t phx_remote_export(HANDLE process,uint32_t module,const char *name)
{
    IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS32 nt;IMAGE_EXPORT_DIRECTORY exports;
    if(!name || strlen(name)>63 || !phx_read(process,module,&dos,sizeof dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
       dos.e_lfanew<0 || (uint32_t)dos.e_lfanew<sizeof dos || dos.e_lfanew>0x100000 ||
       (uint64_t)module+(uint32_t)dos.e_lfanew+sizeof nt>0x100000000ULL ||
       !phx_read(process,module+(uint32_t)dos.e_lfanew,&nt,sizeof nt) || nt.Signature!=IMAGE_NT_SIGNATURE ||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
       nt.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_EXPORT ||
       nt.OptionalHeader.SizeOfImage<4096 || nt.OptionalHeader.SizeOfImage>0x1000000 ||
       (uint64_t)module+nt.OptionalHeader.SizeOfImage>0x100000000ULL)return 0;
    uint32_t imageSize=nt.OptionalHeader.SizeOfImage;
    IMAGE_DATA_DIRECTORY directory=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!directory.VirtualAddress || directory.Size<sizeof exports ||
       (uint64_t)directory.VirtualAddress+directory.Size>imageSize ||
       !phx_read(process,module+directory.VirtualAddress,&exports,sizeof exports) ||
       !exports.NumberOfFunctions || exports.NumberOfFunctions>4096 || exports.NumberOfNames>4096 ||
       !exports.AddressOfNames || !exports.AddressOfNameOrdinals || !exports.AddressOfFunctions ||
       (uint64_t)exports.AddressOfNames+exports.NumberOfNames*4>imageSize ||
       (uint64_t)exports.AddressOfNameOrdinals+exports.NumberOfNames*2>imageSize ||
       (uint64_t)exports.AddressOfFunctions+exports.NumberOfFunctions*4>imageSize)return 0;
    for(uint32_t i=0;i<exports.NumberOfNames;i++){
        uint32_t rva=0,function=0;WORD ordinal=0;char candidate[64];int terminated=0;
        if(!phx_read(process,module+exports.AddressOfNames+i*4,&rva,4) || !rva)return 0;
        for(unsigned j=0;j<sizeof candidate;j++){
            if((uint64_t)rva+j>=imageSize || !phx_read(process,module+rva+j,&candidate[j],1))return 0;
            if(!candidate[j]){terminated=1;break;}
        }
        if(!terminated || strcmp(candidate,name))continue;
        if(!phx_read(process,module+exports.AddressOfNameOrdinals+i*2,&ordinal,2) || ordinal>=exports.NumberOfFunctions ||
           !phx_read(process,module+exports.AddressOfFunctions+ordinal*4,&function,4) ||
           function<nt.OptionalHeader.SizeOfHeaders || function>=imageSize ||
           (function>=directory.VirtualAddress && function<directory.VirtualAddress+directory.Size))return 0;
        return module+function;
    }
    return 0;
}

void phx_hide_user_names(char *s, size_t cap)
{
    static const char tag[] = "\\Users\\";
    const size_t tagLen = sizeof tag - 1;
    char out[2048];
    size_t o = 0;
    const char *p = s;
    if (!s || cap == 0) return;
    while (*p && o + 1 < sizeof out) {
        if (_strnicmp(p, tag, tagLen) == 0) {
            for (size_t i = 0; i < tagLen && o + 1 < sizeof out; i++) out[o++] = p[i];
            p += tagLen;
            while (*p && *p != '\\' && *p != '\r' && *p != '\n') p++;   /* skip the user name */
            if (o + 1 < sizeof out) out[o++] = '~';
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = 0;
    snprintf(s, cap, "%s", out);
}
