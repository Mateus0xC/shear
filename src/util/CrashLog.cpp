#include "CrashLog.h"
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <string>

namespace shear::crashlog {

static std::wstring g_logPath;

static LONG WINAPI onCrash(EXCEPTION_POINTERS *ep)
{
    FILE *f = _wfopen(g_logPath.c_str(), L"a");
    if (!f)
        return EXCEPTION_EXECUTE_HANDLER;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "\n=== falha %04d-%02d-%02d %02d:%02d:%02d  código 0x%08lX  endereço %p ===\n", st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);

    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);

    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf = {};
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 40; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &sf, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) || sf.AddrPC.Offset == 0)
            break;
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256] = {};
        auto *sym = reinterpret_cast<SYMBOL_INFO *>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        char name[300] = "?";
        if (SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym))
            snprintf(name, sizeof name, "%s+0x%llX", sym->Name, static_cast<unsigned long long>(disp));
        IMAGEHLP_MODULE64 mod = {};
        mod.SizeOfStruct = sizeof(mod);
        const char *modName = SymGetModuleInfo64(proc, sf.AddrPC.Offset, &mod) ? mod.ModuleName : "?";
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisp = 0;
        if (SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &lineDisp, &line))
            fprintf(f, "  #%d %s!%s  (%s:%lu)\n", i, modName, name, line.FileName, line.LineNumber);
        else
            fprintf(f, "  #%d %s!%s\n", i, modName, name);
    }
    fclose(f);
    return EXCEPTION_EXECUTE_HANDLER;
}

void install()
{
    wchar_t base[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return;
    std::wstring dir = std::wstring(base) + L"\\Shear\\Shear";
    CreateDirectoryW((std::wstring(base) + L"\\Shear").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    g_logPath = dir + L"\\crash.log";
    SetUnhandledExceptionFilter(onCrash);
}

} // namespace shear::crashlog
