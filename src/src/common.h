// KrakenAV - common.h
// 公共定义：配色、双语文本、路径、工具函数、设置结构
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

// ---------- 路径 ----------
extern std::wstring g_exeDir;      // exe 所在目录（含尾反斜杠）
extern std::wstring g_engineDir;   // ClamAV 引擎目录（DLL + freshclam.exe）
extern std::wstring g_dbDir;       // 病毒库目录
extern std::wstring g_quarDir;     // 隔离区目录
extern std::wstring g_reportDir;   // 报告目录
extern std::wstring g_confDir;     // 配置目录 (settings.ini / freshclam.conf)

// ---------- 双语文本 ----------
enum { LANG_ZH = 0, LANG_EN = 1 };
extern int g_lang;

enum StrId {
    STR_NAV_SCAN, STR_NAV_TOOLS, STR_NAV_ABOUT,
    STR_SCAN_QUICK, STR_SCAN_QUICK_SUB, STR_SCAN_FULL, STR_SCAN_FULL_SUB,
    STR_SCAN_CUSTOM, STR_SCAN_CUSTOM_SUB,
    STR_TOOL_SETTINGS, STR_TOOL_SETTINGS_SUB, STR_TOOL_REPORT, STR_TOOL_REPORT_SUB,
    STR_TOOL_UPDATE, STR_TOOL_UPDATE_SUB, STR_TOOL_QUAR, STR_TOOL_QUAR_SUB,
    STR_STAT_SCANNED, STR_STAT_THREATS, STR_STAT_ELAPSED, STR_STAT_LOADING,
    STR_BTN_STOP, STR_BTN_OK, STR_BTN_BROWSE,
    STR_STATUS_DB, STR_STATUS_NO_DB, STR_STATUS_SIGS, STR_STATUS_UPDATE,
    STR_ABOUT_VER, STR_ABOUT_ENGINE, STR_ABOUT_GPL, STR_ABOUT_URL,
    STR_MSG_NO_DB_TITLE, STR_MSG_NO_DB, STR_MSG_UPDATING, STR_MSG_PICK_FOLDER,
    STR_LOG_START_QUICK, STR_LOG_START_FULL, STR_LOG_START_CUSTOM,
    STR_LOG_ENGINE, STR_LOG_SIGS, STR_LOG_DB_DATE,
    STR_LOG_FOUND, STR_LOG_FOUND_CLEAN_NOTE,
    STR_ACT_REPORTED, STR_ACT_DELETED, STR_ACT_DELETED_FAIL,
    STR_ACT_QUAR_OK, STR_ACT_QUAR_FAIL,
    STR_LOG_SUMMARY, STR_LOG_CANCELLED, STR_LOG_DONE, STR_LOG_ERROR_SKIP,
    STR_REPORT_TITLE, STR_REPORT_MODE, STR_REPORT_ENGINE, STR_REPORT_DB, STR_REPORT_DATETIME,
    STR_REPORT_FILES, STR_REPORT_THREATS, STR_REPORT_ERRORS, STR_REPORT_TIME,
    STR_UPD_TITLE, STR_UPD_RUNNING, STR_UPD_DONE_OK, STR_UPD_DONE_FAIL,
    STR_SET_TITLE, STR_SET_LANG, STR_SET_LANG_ZH, STR_SET_LANG_EN,
    STR_SET_SCAN, STR_SET_SKIP_BIG_FILE, STR_SET_SKIP_BIG_ARCH,
    STR_SET_SKIP_MANY_ARCH, STR_SET_SKIP_DEEP_ARCH,
    STR_SET_EXCLUDE, STR_SET_PATHS, STR_SET_ENGINE_DIR, STR_SET_DB_DIR,
    STR_SET_QUAR_DIR, STR_SET_ACTION, STR_SET_ACT_REPORT, STR_SET_ACT_DELETE,
    STR_SET_ACT_QUAR, STR_SET_MB, STR_SET_FILES_UNIT, STR_SET_LAYERS,
    STR_MSG_NO_REPORT, STR_MSG_DIR_NA, STR_QUAR_MANIFEST_HEAD,
    STR_LOG_KAV_HINT, STR_LOG_PATH_HINT,
    STR_COUNT
};

const wchar_t* STR(StrId id);

// ---------- 设置 ----------
struct Settings {
    int lang = LANG_ZH;
    bool skipBigFiles = true;  int maxFileMB = 30;
    bool skipBigArch = true;   int maxArchMB = 30;
    bool skipManyArch = true;  int maxArchFiles = 30;
    bool skipDeepArch = true;  int maxArchDepth = 30;
    std::wstring excludeExt;   // 分号分隔，如 .md;.txt
    std::wstring engineDir, dbDir, quarDir;
    int infectedAction = 2;    // 0 仅报告 1 删除 2 隔离
};

extern Settings g_set;
void LoadSettings();
void SaveSettings();
std::wstring DefaultExcludeList();

// ---------- 工具 ----------
std::wstring Utf8ToW(const std::string& s);
std::string WToUtf8(const std::wstring& s);
std::wstring GetTimeStamp();                 // YYYYMMDD_HHMMSS
std::wstring NowStr();                       // YYYY-MM-DD HH:MM:SS
std::wstring GroupDigits(unsigned long long v);
std::wstring ToLowerW(std::wstring s);
bool ExtInList(const std::wstring& path, const std::wstring& list);
void EnsureDir(const std::wstring& dir);
int  DPI();

// 全局 DPI 缩放
int SX(int x);
void SetGlobalDpi(int d);

// 文件夹选择对话框（返回空串表示取消）
std::wstring PickFolderDialog(HWND owner);

// 用系统默认方式打开文件/目录，失败时回退 explorer.exe，再失败弹诊断框
void SafeShellOpen(HWND hwnd, const std::wstring& path);

// 路径是否含非 ASCII 字符（ClamAV 不支持中文路径的场景判定）
bool PathHasNonAscii(const std::wstring& s);
