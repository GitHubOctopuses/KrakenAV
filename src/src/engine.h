// KrakenAV - engine.h
// ClamAV 引擎封装：加载/编译/复用，扫描线程，威胁处理，报告与病毒库信息
#pragma once
#include "common.h"
#include <clamav.h>

// 扫描模式
enum ScanMode { MODE_QUICK = 0, MODE_FULL, MODE_CUSTOM };

// 扫描状态（UI 定时器读取）
struct ScanState {
    std::atomic<bool> running{false};
    std::atomic<bool> cancel{false};
    std::atomic<int>  phase{0};          // 0 idle 1 loading 2 scanning
    std::atomic<int>  engineProgress{-1}; // -1 无进度
    std::atomic<unsigned long long> files{0};
    std::atomic<unsigned long long> threats{0};
    std::atomic<unsigned long long> errors{0};
    std::chrono::steady_clock::time_point t0;
};
extern ScanState g_scan;

// UI 日志推送（线程安全，UI 线程定时拉取）
extern std::mutex g_logMtx;
extern std::deque<std::wstring> g_logPending;
void PushLog(const std::wstring& line);
void DrainLogToEdit(HWND edit);

// 引擎
bool EngineInitLibrary();                // cl_init，进程内一次
bool EngineEnsure(HWND notifyWnd);       // 确保 engine 就绪（按需重建），失败返回 false
void EngineMarkDirty();
void EngineShutdown();
struct cl_engine* EngineRaw();           // 测试用

// 病毒库信息（状态栏/报告）
struct DbInfo { bool ok = false; std::wstring dateStr; unsigned long long sigs = 0; };
DbInfo GetDbInfo();

// 扫描入口（内部开线程）
void StartScan(ScanMode mode, const std::wstring& customRoot, HWND mainWnd);
bool ScanBusy();

// 威胁处理
std::wstring ApplyThreat(const std::wstring& path, const char* virname);

// Unicode 安全扫描（CreateFileW + fmap，绕过中文路径缺陷）
cl_error_t ScanFileUnicode(const std::wstring& path, const char** virname,
                           unsigned long* scanned, struct cl_engine* eng,
                           struct cl_scan_options* so);
// 生效毒库目录：原目录为纯 ASCII 直接用；含中文则重定向 C:\ProgramData\KrakenAV\db
std::wstring EffectiveDbDir();

// 报告
void WriteReport(ScanMode mode, unsigned long long files, unsigned long long threats,
                 unsigned long long errors, long long ms, const std::vector<std::wstring>& threatLines);
std::wstring LatestReportPath();

// 快速扫描目标目录
std::vector<std::wstring> QuickScanRoots();
