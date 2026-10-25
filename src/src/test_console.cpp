// KrakenAV - test_console.cpp
// 控制台测试：验证引擎加载/病毒库解析/EICAR查杀/隔离/删除/报告/CVD信息/中文路径
#include "common.h"
#include "engine.h"
#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <io.h>
#include <filesystem>

// EICAR 标准反病毒测试串（业界通用、无恶意载荷，专用于测试杀软）
static const char EICAR[] =
    "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*";

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U16TEXT);
    if (argc < 2) {
        std::wcout << L"Usage: test_console.exe <KrakenAV install directory>\n";
        return 2;
    }
    g_exeDir = argv[1];
    g_engineDir = g_exeDir;
    g_dbDir = g_exeDir + L"\\database";
    g_quarDir = g_exeDir + L"\\quarantine";
    g_reportDir = g_exeDir + L"\\reports";
    g_confDir = g_exeDir + L"\\conf";
    EnsureDir(g_quarDir); EnsureDir(g_reportDir); EnsureDir(g_confDir);
    LoadSettings();
    g_set.infectedAction = 2; // 测试隔离模式

    int pass = 0, fail = 0;
    auto check = [&](const wchar_t* name, bool ok) {
        std::wcout << (ok ? L"[PASS] " : L"[FAIL] ") << name << L"\n";
        ok ? pass++ : fail++;
    };

    // 1) cl_init
    check(L"cl_init", EngineInitLibrary());

    // 2) 病毒库信息解析
    DbInfo db = GetDbInfo();
    std::wcout << L"  DB: ok=" << db.ok << L" date=" << db.dateStr << L" sigs=" << db.sigs << L"\n";
    check(L"GetDbInfo (cvd present)", db.ok);

    // 3) 引擎加载
    std::wcout << L"  effective db dir: " << EffectiveDbDir() << L"\n";
    std::wcout << L"  loading engine...\n";
    if (!EngineEnsure(nullptr)) {
        check(L"EngineEnsure", false);
        std::wcout << L"==== aborted: engine unavailable ====\n";
        return 1;
    }
    check(L"EngineEnsure", true);

    // 4) EICAR 测试
    std::wstring testDir = g_exeDir + L"\\eicar_selftest";
    EnsureDir(testDir);
    std::wstring eicarPath = testDir + L"\\eicar_test.com";
    {
        std::ofstream f(std::filesystem::path(eicarPath), std::ios::binary);
        f.write(EICAR, sizeof(EICAR) - 1);
    }
    std::wstring cleanPath = testDir + L"\\clean.txt";
    {
        std::ofstream f(std::filesystem::path(cleanPath), std::ios::binary);
        f << "hello world, just a clean file" << std::endl;
    }

    const char* virname = nullptr;
    unsigned long scanned = 0;
    struct cl_scan_options so{};
    so.parse = CL_SCAN_PARSE_ARCHIVE | CL_SCAN_PARSE_PDF | CL_SCAN_PARSE_MAIL | CL_SCAN_PARSE_OLE2 |
               CL_SCAN_PARSE_HTML | CL_SCAN_PARSE_PE | CL_SCAN_PARSE_ELF;
    int rc = ScanFileUnicode(eicarPath, &virname, &scanned, EngineRaw(), &so);
    std::wcout << L"  EICAR rc=" << rc << L" virname=" << (virname ? Utf8ToW(virname) : L"(null)") << L"\n";
    check(L"EICAR detected", rc == CL_VIRUS && virname);

    rc = ScanFileUnicode(cleanPath, &virname, &scanned, EngineRaw(), &so);
    check(L"clean file clean", rc == CL_CLEAN);

    // 5) 隔离动作
    std::wstring act = ApplyThreat(eicarPath, "Eicar-Test");
    std::wcout << L"  quarantine action: " << act << L"\n";
    bool gone = GetFileAttributesW(eicarPath.c_str()) == INVALID_FILE_ATTRIBUTES;
    check(L"infected file moved (gone from origin)", gone);
    {
        namespace fs = std::filesystem;
        int n = 0;
        for (auto& e : fs::directory_iterator(g_quarDir)) n++;
        check(L"quarantine dir non-empty", n > 0);
    }

    // 6) 排除扩展
    check(L"ExtInList(.md)", ExtInList(L"C:\\a\\b\\note.MD", L".md;.txt"));
    check(L"ExtInList(.mp4)", ExtInList(L"C:\\a\\b\\movie.MP4", DefaultExcludeList()));
    check(L"ExtInList(no-ext false)", !ExtInList(L"C:\\a\\b\\noext", DefaultExcludeList()));

    // 7) 报告
    std::vector<std::wstring> threats{ L"  Eicar-Test | " + eicarPath + L" (moved)" };
    WriteReport(MODE_CUSTOM, 123, 1, 0, 4567, threats);
    std::wstring rp = LatestReportPath();
    std::wcout << L"  report: " << rp << L"\n";
    check(L"report written", !rp.empty() && GetFileAttributesW(rp.c_str()) != INVALID_FILE_ATTRIBUTES);

    // 8) 删除模式
    g_set.infectedAction = 1;
    {
        std::ofstream f(std::filesystem::path(eicarPath), std::ios::binary);
        f.write(EICAR, sizeof(EICAR) - 1);
    }
    std::wstring act2 = ApplyThreat(eicarPath, "Eicar-Test");
    check(L"delete action removes file", GetFileAttributesW(eicarPath.c_str()) == INVALID_FILE_ATTRIBUTES);

    // i18n completeness: both language tables must be non-empty for every id
    // (guards against silent misalignment when a string is inserted mid-table)
    {
        int ok = 0;
        for (int id = 0; id < STR_COUNT; id++) {
            g_lang = LANG_ZH; const wchar_t* a = STR((StrId)id);
            g_lang = LANG_EN;  const wchar_t* b = STR((StrId)id);
            if (a && *a && b && *b) ok++;
        }
        g_lang = LANG_ZH;
        std::wcout << L"  i18n: " << ok << L"/" << STR_COUNT << L"\n";
        check(L"i18n table complete", ok == STR_COUNT);
    }

    std::wcout << L"\n==== " << pass << L" passed, " << fail << L" failed ====\n";

    // Bonus: Chinese path + Chinese filename EICAR (handle-based scan)
    {
        std::wstring cnDir = g_exeDir + L"\\eicar_中文路径测试";
        EnsureDir(cnDir);
        std::wstring cnPath = cnDir + L"\\病毒样本.com";
        {
            std::ofstream f(std::filesystem::path(cnPath), std::ios::binary);
            f.write(EICAR, sizeof(EICAR) - 1);
        }
        const char* vn = nullptr;
        unsigned long sc = 0;
        int r2 = ScanFileUnicode(cnPath, &vn, &sc, EngineRaw(), &so);
        std::wcout << L"[SCAN-CN] rc=" << r2 << L" virname=" << (vn ? Utf8ToW(vn) : L"(null)") << L"\n";
        check(L"chinese path scan (handle)", r2 == CL_VIRUS && vn);
        DeleteFileW(cnPath.c_str());
        RemoveDirectoryW(cnDir.c_str());
    }
    return fail ? 1 : 0;
}
