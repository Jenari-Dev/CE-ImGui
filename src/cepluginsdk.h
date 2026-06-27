#pragma once
// cepluginsdk.h — Cheat Engine plugin SDK (minimal, x64).
//
// IMPORTANT: NO #pragma pack here. CE uses the compiler's natural alignment.
// Forcing pack(1) corrupts pointer offsets on x64 and crashes CE (this was a
// real bug in the previous attempt — see project notes).

#include <windows.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// CE plugin SDK version this plugin targets.
#define CESDK_VERSION 6

typedef struct _PluginVersion {
    unsigned int version;
    char*        pluginname;
} PluginVersion, *PPluginVersion;

// --- Function pointer typedefs CE passes to us via ExportedFunctions ---
typedef void     (__stdcall *CEP_SHOWMESSAGE)(char* message);
typedef int      (__stdcall *CEP_REGISTERFUNCTION)(int pluginid, int functiontype, void* init);
typedef BOOL     (__stdcall *CEP_UNREGISTERFUNCTION)(int pluginid, int functionid);
typedef HWND     (__stdcall *CEP_GETMAINWINDOWHANDLE)(void);
typedef void*    (__stdcall *CEP_GETLUASTATE)(void);
typedef void     (__stdcall *CEP_MAINLUARUNSCRIPT)(char* script);

// ExportedFunctions — CE fills this and hands it to CEPlugin_InitializePlugin.
// We only rely on the first few fields + the trailing Lua helpers; everything
// in between is kept as raw pointers so the struct layout matches CE's exactly.
typedef struct _ExportedFunctions {
    int sizeofExportedFunctions;

    CEP_SHOWMESSAGE          ShowMessage;
    CEP_REGISTERFUNCTION     RegisterFunction;
    CEP_UNREGISTERFUNCTION   UnregisterFunction;

    PULONG                   OpenedProcessID;
    PHANDLE                  OpenedProcessHandle;

    CEP_GETMAINWINDOWHANDLE  GetMainWindowHandle;

    void* AutoAssemble;
    void* Assembler;
    void* Disassembler;
    void* ChangeRegistersAtAddress;
    void* InjectDLL;
    void* FreezeMem;
    void* UnfreezeMem;
    void* FixMem;
    void* ProcessList;
    void* ReloadSettings;
    void* GetAddressFromPointer;

    void* ReadProcessMemory;
    void* WriteProcessMemory;
    void* GetThreadContext;
    void* SetThreadContext;
    void* SuspendThread;
    void* ResumeThread;
    void* OpenProcessEx;
    void* WaitForDebugEvent;
    void* ContinueDebugEvent;
    void* DebugActiveProcess;
    void* StopDebugging;
    void* StopRegisterChange;
    void* VirtualProtectEx;
    void* VirtualAllocExProc;
    void* VirtualFreeExProc;
    void* VirtualQueryExProc;
    void* GetSystemInfoProc;

    CEP_GETLUASTATE          GetLuaState;
    CEP_MAINLUARUNSCRIPT     MainLuaRunScript;
} ExportedFunctions, *PExportedFunctions;
