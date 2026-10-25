// KrakenAV - common.cpp
#include "common.h"
#include <shlobj.h>
#include <shellapi.h>

std::wstring g_exeDir, g_engineDir, g_dbDir, g_quarDir, g_reportDir, g_confDir;
int g_lang = LANG_ZH;
Settings g_set;
static int g_dpi = 96;

int DPI() { return g_dpi; }
int SX(int x) { return MulDiv(x, g_dpi, 96); }
void SetGlobalDpi(int d) { if (d >= 96 && d <= 480) g_dpi = d; }

// ---------- 双语文本表 ----------
static const wchar_t* ZH[STR_COUNT] = {
    L"扫描", L"工具", L"关于",
    L"快速扫描", L"扫描系统关键区域", L"全盘扫描", L"扫描所有本地磁盘",
    L"定制扫描", L"扫描指定目录",
    L"设置", L"软件偏好设置", L"报告", L"查看扫描报告", L"更新", L"更新病毒库",
    L"隔离区", L"查看被隔离文件",
    L"已扫描", L"威胁", L"用时", L"正在加载病毒库引擎",
    L"停止", L"确 定", L"浏览...",
    L"病毒库", L"未安装", L"个特征", L"更新病毒库 ▸",
    L"版本 " KRAKENAV_VERSION_W, L"扫描引擎：ClamAV", L"基于 ClamAV (GPLv2) 开源引擎构建", L"https://github.com/GitHubOctopuses/KrakenAV",
    L"提示", L"病毒库尚未安装，是否现在打开更新界面？", L"病毒库正在更新，请稍候再扫描。", L"选择要扫描的文件夹",
    L"开始快速扫描", L"开始全盘扫描", L"开始定制扫描",
    L"引擎", L"个特征", L"病毒库日期",
    L"发现威胁", L"扫描完成，未发现威胁。",
    L"（仅报告）", L"（已删除）", L"（删除失败！）",
    L"（已移至隔离区）", L"（移入隔离区失败！）",
    L"扫描结束", L"已停止", L"完成", L"（跳过：无法读取）",
    L"KrakenAV 扫描报告", L"扫描方式", L"引擎", L"病毒库", L"时间",
    L"已扫描文件", L"发现威胁", L"错误", L"耗时",
    L"更新病毒库 - KrakenAV", L"正在更新病毒库……", L"病毒库更新完成！", L"病毒库更新失败，请稍后重试。",
    L"设置 - KrakenAV", L"语言", L"中文", L"English",
    L"扫描设置", L"不扫描大于", L"MB 的压缩包（解压总量）",
    L"不扫描包含超过", L"不扫描嵌套超过",
    L"不扫描的文件类型（分号分隔）：", L"路径设置", L"杀毒引擎位置：", L"病毒库位置：",
    L"隔离区位置：", L"受感染的文件", L"仅报告", L"删除", L"移至隔离文件夹",
    L"MB 的文件", L"个文件的压缩包", L"层的压缩包",
    L"暂无扫描报告", L"目录不存在，已自动创建。", L"==== 隔离区清单（原文路径 → 隔离文件 ; 病毒名 ; 时间）====",
    L"病毒库文件缺失。本机其他杀毒软件可能把病毒库(.cvd)当威胁隔离，请将 KrakenAV 目录加入其信任/排除列表后重新更新病毒库。",
    L"[!] 程序目录含中文，病毒库更新会失败。请从 C:\\KrakenAV\\KrakenAV.exe 启动（可右键它创建桌面快捷方式，详见 README）。",
};

static const wchar_t* EN[STR_COUNT] = {
    L"Scan", L"Tools", L"About",
    L"Quick Scan", L"Scan critical areas", L"Full Scan", L"Scan all local drives",
    L"Custom Scan", L"Scan a folder of choice",
    L"Settings", L"Application preferences", L"Report", L"View scan reports", L"Update", L"Update virus DB",
    L"Quarantine", L"View quarantined files",
    L"Scanned", L"Threats", L"Elapsed", L"Loading virus database engine",
    L"Stop", L"OK", L"Browse...",
    L"Virus DB", L"not installed", L"signatures", L"Update DB ▸",
    L"Version " KRAKENAV_VERSION_W, L"Engine: ClamAV", L"Built on the ClamAV (GPLv2) open-source engine", L"https://github.com/GitHubOctopuses/KrakenAV",
    L"Notice", L"Virus database is not installed. Open the update window now?", L"Virus database is updating, please wait.", L"Select a folder to scan",
    L"Quick scan started", L"Full scan started", L"Custom scan started",
    L"Engine", L"signatures", L"DB date",
    L"THREAT FOUND", L"Scan finished. No threats found.",
    L"(report only)", L"(deleted)", L"(delete FAILED!)",
    L"(moved to quarantine)", L"(quarantine FAILED!)",
    L"Scan finished", L"Stopped", L"Done", L"(skipped: unreadable)",
    L"KrakenAV Scan Report", L"Mode", L"Engine", L"Virus DB", L"Time",
    L"Files scanned", L"Threats found", L"Errors", L"Elapsed",
    L"Update Virus DB - KrakenAV", L"Updating virus database...", L"Virus database updated!", L"Virus database update failed. Try again later.",
    L"Settings - KrakenAV", L"Language", L"中文", L"English",
    L"Scan Settings", L"Skip files larger than", L"MB of archives (total unpacked)",
    L"Skip archives with more than", L"Skip archives nested deeper than",
    L"Excluded file types (semicolon separated):", L"Paths", L"Engine location:", L"Database location:",
    L"Quarantine location:", L"Infected files", L"Report only", L"Delete", L"Move to quarantine",
    L"MB files", L"files per archive", L"layers of nesting",
    L"No scan report yet", L"Directory was missing and has been created.", L"==== Quarantine manifest (original path -> quarantined file ; virus ; time) ====",
    L"Database files missing. Another antivirus on this machine may quarantine the .cvd database files. Add the KrakenAV folder to its trust/exclusion list, then update again.",
    L"[!] Program path contains non-ASCII characters: database update will fail. Launch via C:\\KrakenAV\\KrakenAV.exe (right-click it to create a desktop shortcut; see README).",
};

const wchar_t* STR(StrId id) { return g_lang == LANG_ZH ? ZH[id] : EN[id]; }

// ---------- 编码转换 ----------
std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n);
    return r;
}
std::string WToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n, nullptr, nullptr);
    return r;
}

std::wstring GetTimeStamp() {
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t b[32];
    swprintf(b, 32, L"%04d%02d%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}
std::wstring NowStr() {
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t b[40];
    swprintf(b, 40, L"%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}
std::wstring GroupDigits(unsigned long long v) {
    std::wstring s = std::to_wstring(v);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, L",");
    return s;
}
std::wstring ToLowerW(std::wstring s) {
    for (auto& c : s) c = towlower(c);
    return s;
}

bool ExtInList(const std::wstring& path, const std::wstring& list) {
    if (list.empty()) return false;
    size_t dot = path.find_last_of(L".\\/");
    if (dot == std::wstring::npos || path[dot] != L'.') return false;
    std::wstring ext = ToLowerW(path.substr(dot));
    size_t p = 0;
    while (p < list.size()) {
        size_t q = list.find(L';', p);
        if (q == std::wstring::npos) q = list.size();
        std::wstring one = ToLowerW(list.substr(p, q - p));
        if (!one.empty() && one[0] != L'.') one.insert(one.begin(), L'.');
        if (one == ext) return true;
        p = q + 1;
    }
    return false;
}

void EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return;
    CreateDirectoryW(dir.c_str(), nullptr); // 已存在则失败，无碍；多级逐个创建
    DWORD at = GetFileAttributesW(dir.c_str());
    if (at == INVALID_FILE_ATTRIBUTES) {
        // 逐级创建
        std::wstring cur;
        for (size_t i = 0; i < dir.size(); i++) {
            cur += dir[i];
            if (dir[i] == L'\\' && i > 2) CreateDirectoryW(cur.c_str(), nullptr);
        }
        CreateDirectoryW(dir.c_str(), nullptr);
    }
}

// ---------- 长路径（>260）支持 ----------
static const wchar_t* LONG_PREFIX = L"\\\\?\\";

std::wstring ToLongPath(const std::wstring& p) {
    if (p.size() < 248) return p;                       // 预留余量
    if (p.compare(0, 4, LONG_PREFIX) == 0) return p;    // 已带前缀
    if (p.size() >= 3 && p[1] == L':' && p[2] == L'\\') // 盘符绝对路径
        return LONG_PREFIX + p;
    return p;                                           // UNC 等暂不处理
}

std::wstring StripLongPath(const std::wstring& p) {
    if (p.compare(0, 4, LONG_PREFIX) == 0) return p.substr(4);
    return p;
}

// ---------- 隔离文件 XOR 混淆 ----------
bool XorFileInPlace(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        // 只读属性：清掉重试一次
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
    }
    char buf[65536];
    LONG64 off = 0;
    DWORD rd = 0;
    while (ReadFile(h, buf, sizeof(buf), &rd, nullptr) && rd > 0) {
        for (DWORD i = 0; i < rd; i++) buf[i] ^= 0x5A;
        LARGE_INTEGER li{}; li.QuadPart = off;
        SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
        DWORD wr = 0;
        if (!WriteFile(h, buf, rd, &wr, nullptr) || wr != rd) { CloseHandle(h); return false; }
        off += rd;
        LARGE_INTEGER nxt{}; nxt.QuadPart = off;
        SetFilePointerEx(h, nxt, nullptr, FILE_BEGIN);
    }
    CloseHandle(h);
    return true;
}

std::wstring DefaultExcludeList() {
    // 默认排除：纯文本、图片、音视频等无法在 Windows 上直接执行的惰性格式
    // 注意：.js/.vbs/.bat/.pdf/.svg/office 文档等可携带恶意代码的格式不在排除之列
    return L".md;.txt;.log;.json;.xml;.yaml;.yml;.toml;.ini;.cfg;.conf;.csv;.tsv;"
           L".jpg;.jpeg;.png;.gif;.bmp;.ico;.webp;.tif;.tiff;"
           L".mp3;.wav;.flac;.aac;.ogg;.m4a;.wma;"
           L".mp4;.mkv;.avi;.mov;.wmv;.flv;.webm;.m4v;.ts;.m2ts";
}

// ---------- 路径 ASCII 判定 ----------
bool PathHasNonAscii(const std::wstring& s) {
    for (wchar_t c : s) if ((unsigned short)c > 0x7F) return true;
    return false;
}

// ---------- 安全打开文件/目录 ----------
void SafeShellOpen(HWND hwnd, const std::wstring& path) {
    if (path.empty()) return;
    // 1) 默认关联打开
    HINSTANCE r = ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r > 32) return;
    // 2) 回退：显式 explorer 打开
    std::wstring arg = L"\"" + path + L"\"";
    r = ShellExecuteW(hwnd, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r > 32) return;
    // 3) 诊断
    wchar_t msg[1024];
    swprintf(msg, 1024, L"ShellExecute failed (code %d):\n%s", (int)(INT_PTR)r, path.c_str());
    MessageBoxW(hwnd, msg, L"KrakenAV", MB_OK | MB_ICONWARNING);
}

// ---------- 文件夹选择 ----------
std::wstring PickFolderDialog(HWND owner) {
    std::wstring out;
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(dlg->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR p = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                    out = p; CoTaskMemFree(p);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    return out;
}

// ---------- 设置持久化 ----------
static std::wstring IniPath() { return g_confDir + L"settings.ini"; }
static std::wstring ReadIni(const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[2048];
    GetPrivateProfileStringW(sec, key, def, buf, 2048, IniPath().c_str());
    return buf;
}
static int ReadIniInt(const wchar_t* sec, const wchar_t* key, int def) {
    return GetPrivateProfileIntW(sec, key, def, IniPath().c_str());
}

void LoadSettings() {
    // 路径默认值取自 exe 所在目录（全局变量已先行赋值）
    // 路径策略：ini 留空 = 跟随程序目录（便携/开源分发友好）；
    // 中文路径 ClamAV 引擎不支持，一律回退默认
    g_set.engineDir = ReadIni(L"paths", L"engineDir", L"");
    g_set.dbDir = ReadIni(L"paths", L"dbDir", L"");
    g_set.quarDir = ReadIni(L"paths", L"quarDir", L"");
    auto fixPath = [&](std::wstring& v, const std::wstring& def) {
        if (v.empty() || PathHasNonAscii(v)) { v = def; return; }
        if (GetFileAttributesW(v.c_str()) == INVALID_FILE_ATTRIBUTES) v = def;
    };
    fixPath(g_set.engineDir, g_engineDir);
    fixPath(g_set.dbDir, g_dbDir);
    fixPath(g_set.quarDir, g_quarDir);
    g_set.lang = ReadIniInt(L"general", L"language", LANG_ZH);
    g_set.skipBigFiles = ReadIniInt(L"scan", L"skipBigFiles", 1) != 0;
    g_set.maxFileMB = ReadIniInt(L"scan", L"maxFileMB", 30);
    g_set.skipBigArch = ReadIniInt(L"scan", L"skipBigArch", 1) != 0;
    g_set.maxArchMB = ReadIniInt(L"scan", L"maxArchMB", 30);
    g_set.skipManyArch = ReadIniInt(L"scan", L"skipManyArch", 1) != 0;
    g_set.maxArchFiles = ReadIniInt(L"scan", L"maxArchFiles", 30);
    g_set.skipDeepArch = ReadIniInt(L"scan", L"skipDeepArch", 1) != 0;
    g_set.maxArchDepth = ReadIniInt(L"scan", L"maxArchDepth", 30);
    g_set.excludeExt = ReadIni(L"scan", L"excludeExt", DefaultExcludeList().c_str());
    g_set.infectedAction = ReadIniInt(L"action", L"infected", 2);
    if (g_set.infectedAction < 0 || g_set.infectedAction > 2) g_set.infectedAction = 2;
    if (g_set.lang != LANG_ZH && g_set.lang != LANG_EN) g_set.lang = LANG_ZH;
}

void SaveSettings() {
    auto w = [](const wchar_t* sec, const wchar_t* key, const std::wstring& v) {
        WritePrivateProfileStringW(sec, key, v.c_str(), IniPath().c_str());
    };
    auto wi = [&](const wchar_t* sec, const wchar_t* key, int v) {
        wchar_t b[32]; swprintf(b, 32, L"%d", v); w(sec, key, b);
    };
    wi(L"general", L"language", g_set.lang);
    wi(L"scan", L"skipBigFiles", g_set.skipBigFiles ? 1 : 0);
    wi(L"scan", L"maxFileMB", g_set.maxFileMB);
    wi(L"scan", L"skipBigArch", g_set.skipBigArch ? 1 : 0);
    wi(L"scan", L"maxArchMB", g_set.maxArchMB);
    wi(L"scan", L"skipManyArch", g_set.skipManyArch ? 1 : 0);
    wi(L"scan", L"maxArchFiles", g_set.maxArchFiles);
    wi(L"scan", L"skipDeepArch", g_set.skipDeepArch ? 1 : 0);
    w(L"scan", L"excludeExt", g_set.excludeExt);
    // 与程序目录相同的路径写空（跟随程序目录），仅自定义路径才持久化
    w(L"paths", L"engineDir", g_set.engineDir == g_engineDir ? L"" : g_set.engineDir);
    w(L"paths", L"dbDir", g_set.dbDir == g_dbDir ? L"" : g_set.dbDir);
    w(L"paths", L"quarDir", g_set.quarDir == g_quarDir ? L"" : g_set.quarDir);
    wi(L"action", L"infected", g_set.infectedAction);
}
