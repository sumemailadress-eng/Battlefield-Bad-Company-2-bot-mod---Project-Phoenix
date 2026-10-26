#include "win_common.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

static FILE *transcript;
static void message(const char *format,...)
{
    char text[2048];va_list args;va_start(args,format);vsnprintf(text,sizeof text,format,args);va_end(args);
    phx_hide_user_names(text,sizeof text);
    printf("%s",text);fflush(stdout);
    if(transcript){fputs(text,transcript);fflush(transcript);}
}
static void windows_error(const char *operation)
{ message("%s failed (Windows error %lu).\n",operation,(unsigned long)GetLastError()); }
static DWORD find_server(void)
{
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE)return 0;
    PROCESSENTRY32W entry;memset(&entry,0,sizeof entry);entry.dwSize=sizeof entry;
    DWORD found=0;unsigned count=0;
    if(Process32FirstW(snap,&entry))do{
        if(!_wcsicmp(entry.szExeFile,L"Frost.Game.Main_Win32_Final.exe")){found=entry.th32ProcessID;count++;}
    }while(Process32NextW(snap,&entry));
    CloseHandle(snap);
    if(count>1){message("Several BC2 server processes are running. Use Start-Bots.bat --pid NUMBER (see Task Manager, Details tab).\n");return 0;}
    return found;
}
/* Timeout never frees a remote argument that the target thread may still use.
   No TerminateThread, forced unload or engine calls from remote threads. */
static int remote_call(HANDLE process,uint32_t function,const void *argument,SIZE_T size,DWORD timeout,DWORD *result)
{
    void *remote=NULL;HANDLE thread=NULL;SIZE_T wrote=0;int okay=0,finished=0;
    if(size){
        remote=VirtualAllocEx(process,NULL,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!remote){windows_error("VirtualAllocEx");return 0;}
        if(!WriteProcessMemory(process,remote,argument,size,&wrote) || wrote!=size){windows_error("WriteProcessMemory");goto done;}
    }
    thread=CreateRemoteThread(process,NULL,0,(LPTHREAD_START_ROUTINE)(uintptr_t)function,remote,0,NULL);
    if(!thread){windows_error("CreateRemoteThread");goto done;}
    DWORD wait=WaitForSingleObject(thread,timeout);
    if(wait!=WAIT_OBJECT_0){
        message("Remote initialization is still pending or unavailable (wait=%lu). No second attempt will be injected.\n",(unsigned long)wait);
        goto done;
    }
    finished=1;
    if(!GetExitCodeThread(thread,result)){windows_error("GetExitCodeThread");goto done;}
    okay=1;
done:
    if(remote && (!thread || finished))VirtualFreeEx(process,remote,0,MEM_RELEASE);
    if(thread)CloseHandle(thread);
    return okay;
}
static int monitor(HANDLE process,const WCHAR *path)
{
    message("Attached. Bots join as soon as a level is running. Showing the first 40 seconds of the bot log:\n\n");
    HANDLE file=INVALID_HANDLE_VALUE;static char buffer[1<<20];DWORD start=GetTickCount(),shown=0;int result=-1;
    while(GetTickCount()-start<40000){
        if(WaitForSingleObject(process,0)==WAIT_OBJECT_0){message("\nBC2 server exited - see runtime.log in the report folder.\n");if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);return 31;}
        if(file==INVALID_HANDLE_VALUE)file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
        if(file!=INVALID_HANDLE_VALUE){
            DWORD got=0;SetFilePointer(file,(LONG)shown,NULL,FILE_BEGIN);
            if(ReadFile(file,buffer,sizeof buffer-1,&got,NULL) && got){
                buffer[got]=0;char *lastNl=strrchr(buffer,'\n');
                if(lastNl){lastNl[1]=0;shown+=(DWORD)(lastNl+1-buffer);message("%s",buffer);
                    if(strstr(buffer,"RESULT FAILED"))result=32;}
            }
        }
        if(result>0)break;
        Sleep(400);
    }
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(result>0){message("\nStart failed - see the lines above.\n");return result;}
    message("\nPhoenix keeps running inside the server. The full log is: %ls\nRun Stop-Bots.bat to remove the bots.\n",path);
    return 0;
}
int wmain(int argc,WCHAR **argv)
{
    int stopping=0,pauseAtEnd=0;DWORD pid=0;
    for(int i=1;i<argc;i++){
        if(!wcscmp(argv[i],L"--stop"))stopping=1;
        else if(!wcscmp(argv[i],L"--start"))stopping=0;
        else if(!wcscmp(argv[i],L"--pause"))pauseAtEnd=1;
        else if(!wcscmp(argv[i],L"--pid") && i+1<argc){
            WCHAR *end=NULL;unsigned long value=wcstoul(argv[++i],&end,10);
            if(!value || *end){message("Invalid PID.\n");return 2;}pid=(DWORD)value;
        }else{message("Usage: PhoenixAttach.exe --start|--stop [--pid NUMBER] [--pause]\n");return 2;}
    }
    WCHAR modulePath[1024],directory[1024],dllPath[1024],reportRoot[1024],runDirectory[1024],path[1024];
    DWORD length=GetModuleFileNameW(NULL,modulePath,1024);
    if(!length || length>=1024)return 3;
    wcscpy(directory,modulePath);WCHAR *slash=wcsrchr(directory,L'\\');if(!slash)return 3;*slash=0;
    if(swprintf(dllPath,1024,L"%ls\\PhoenixBC2.dll",directory)<0)return 3;
    slash=wcsrchr(directory,L'\\');if(!slash)return 3;*slash=0;
    if(swprintf(reportRoot,1024,L"%ls\\reports",directory)<0)return 3;
    CreateDirectoryW(reportRoot,NULL);
    SYSTEMTIME time;GetLocalTime(&time);
    if(swprintf(runDirectory,1024,L"%ls\\%04u%02u%02u-%02u%02u%02u-%03u-%lu",reportRoot,
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,(unsigned long)GetCurrentProcessId())<0)return 3;
    if(!CreateDirectoryW(runDirectory,NULL)){windows_error("Create report directory");return 3;}
    swprintf(path,1024,L"%ls\\injector.log",runDirectory);transcript=_wfopen(path,L"wb");
    if(!transcript){message("Cannot create injector report.\n");return 3;}
    swprintf(path,1024,L"%ls\\latest.txt",reportRoot);FILE *latest=_wfopen(path,L"wb");
    if(latest){
        int bytes=WideCharToMultiByte(CP_UTF8,0,runDirectory,-1,NULL,0,NULL,NULL);
        char utf8[4096];
        if(bytes>0 && bytes<=(int)sizeof utf8){WideCharToMultiByte(CP_UTF8,0,runDirectory,-1,utf8,bytes,NULL,NULL);fwrite(utf8,1,(size_t)bytes-1,latest);}
        fclose(latest);
    }
    message("Project Phoenix BC2 0.6 - bot runtime %s\n",stopping?"stop":"attach");
    if(!pid)pid=find_server();
    if(!pid){message("No single Frost.Game.Main_Win32_Final.exe server found. Start BF-Portable BC2 first.\n");return 4;}
    message("Server PID: %lu\n",(unsigned long)pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid);
    if(!process){windows_error("OpenProcess");message("The server runs as administrator; this tool must too.\n");if(transcript)fclose(transcript);return 5;}
    int exitCode=6;WCHAR serverPath[1024];DWORD pathLength=1024;
    if(!QueryFullProcessImageNameW(process,0,serverPath,&pathLength)){windows_error("Query server executable");goto cleanup;}
    if(!phx_verify_server_file(serverPath)){message("Executable SHA256 does not match the supplied CA8169FA... build. No module loaded.\n");goto cleanup;}
    message("Exact server executable SHA256 matched.\n");
    uint32_t remoteModule=phx_remote_module(pid,L"PhoenixBC2.dll",dllPath);
    if(!remoteModule && phx_remote_module(pid,L"PhoenixBC2.dll",NULL)){
        message("A PhoenixBC2.dll from another folder is already loaded. Restart the server before using this package.\n");goto cleanup;
    }
    if(stopping && !remoteModule){message("Phoenix is not loaded from this folder. No action taken.\n");exitCode=0;goto cleanup;}
    if(!stopping && remoteModule){message("Phoenix is already loaded. Bots are already running in this server. Run Stop-Bots.bat, or restart the server to load a new version.\n");exitCode=8;goto cleanup;}
    if(!remoteModule){
        uint32_t loadLibrary=phx_remote_system_proc(pid,"LoadLibraryW");DWORD loaded=0;
        if(!loadLibrary){message("Cannot resolve LoadLibraryW in the target modules.\n");goto cleanup;}
        if(!remote_call(process,loadLibrary,dllPath,(wcslen(dllPath)+1)*sizeof(WCHAR),10000,&loaded))goto cleanup;
        if(!loaded){message("Windows could not load PhoenixBC2.dll. No engine calls made.\n");goto cleanup;}
        remoteModule=phx_remote_module(pid,L"PhoenixBC2.dll",dllPath);
        if(!remoteModule || loaded!=remoteModule){message("Loaded-module identity check failed. Restart server before retry.\n");goto cleanup;}
    }
    /* Resolve from the loaded image, so stop never uses offsets from a DLL
       that may have been replaced on disk after the original attach. */
    uint32_t exported=phx_remote_export(process,remoteModule,stopping?"PhoenixStop":"PhoenixStart");
    if(!exported){message("Expected export absent in loaded module. Restart server before retry.\n");goto cleanup;}
    DWORD result=0;
    if(stopping){
        if(!remote_call(process,exported,NULL,0,10000,&result))goto cleanup;
        message("Stop returned %lu. All Phoenix bots were removed. Restart the server before attaching again.\n",(unsigned long)result);exitCode=result?7:0;
    }else{
        PhoenixStartConfig config;memset(&config,0,sizeof config);config.magic=PHX_ATTACH_MAGIC;config.size=sizeof config;
        if(swprintf(config.logPath,1024,L"%ls\\runtime.log",runDirectory)<0)goto cleanup;
        if(!remote_call(process,exported,&config,sizeof config,15000,&result))goto cleanup;
        message("Module start returned %lu.\n",(unsigned long)result);
        if(result){message("Start codes: 10 bad config; 11 already started (restart server); 12 server exe hash; 13 log file; 14 code fingerprint; 15 hook install.\n");exitCode=8;goto cleanup;}
        exitCode=monitor(process,config.logPath);
    }
cleanup:
    CloseHandle(process);message("Report folder: %ls\n",runDirectory);
    if(transcript){fclose(transcript);transcript=NULL;}
    if(pauseAtEnd){printf("\nPress Enter to close this window.");fflush(stdout);getchar();}
    return exitCode;
}
