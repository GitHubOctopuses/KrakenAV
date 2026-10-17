// KrakenAV - test_thread.cpp
// 最小复现：std::thread 里 cl_load（模拟 GUI 扫描线程路径）
#include "common.h"
#include "engine.h"
#include <iostream>
#include <thread>
#include <fcntl.h>
#include <io.h>

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U16TEXT);
    g_exeDir = argc > 1 ? argv[1] : L"D:\\Zcode\\杀毒软件\\KrakenAV";
    g_engineDir = g_exeDir;
    g_dbDir = g_exeDir + L"\\database";
    g_quarDir = g_exeDir + L"\\quarantine";
    g_reportDir = g_exeDir + L"\\reports";
    g_confDir = g_exeDir + L"\\conf";
    LoadSettings();

    std::wcout << L"cl_init on main: " << EngineInitLibrary() << L"\n";
    std::wcout << L"effective db: " << EffectiveDbDir() << L"\n";

    std::thread t([] {
        bool ok = EngineEnsure(nullptr);
        std::wcout << L"thread EngineEnsure: " << (ok ? L"OK" : L"FAIL") << L"\n";
    });
    t.join();
    return 0;
}
