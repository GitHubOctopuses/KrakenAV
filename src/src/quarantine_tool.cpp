// KrakenAV - quarantine_tool.cpp
// 隔离区恢复工具：把 XOR 混淆的 .quarantine 文件还原为原始内容
// 用法:
//   quarantine_restore.exe <file.quarantine> [dest]
//   dest 缺省 = 同目录下去掉 .quarantine 后缀
#include "common.h"
#include <cstdio>
#include <fcntl.h>
#include <io.h>

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U16TEXT);
    if (argc < 2) {
        std::wprintf(L"KrakenAV quarantine restore tool\n");
        std::wprintf(L"Usage: quarantine_restore.exe <file.quarantine> [dest]\n");
        return 2;
    }
    std::wstring src = argv[1];
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wprintf(L"[x] file not found: %s\n", src.c_str());
        return 1;
    }
    std::wstring dst = argc > 2 ? argv[2] : src;
    if (dst.size() > 12 && dst.compare(dst.size() - 12, 12, L".quarantine") == 0)
        dst = dst.substr(0, dst.size() - 12);
    if (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::wprintf(L"[x] dest exists (won't overwrite): %s\n", dst.c_str());
        return 1;
    }
    if (!CopyFileW(src.c_str(), dst.c_str(), TRUE)) {
        std::wprintf(L"[x] copy failed: %s\n", dst.c_str());
        return 1;
    }
    if (!XorFileInPlace(dst)) {
        DeleteFileW(dst.c_str());
        std::wprintf(L"[x] restore (XOR) failed\n");
        return 1;
    }
    std::wprintf(L"[OK] restored: %s\n", dst.c_str());
    std::wprintf(L"Note: the restored file was flagged as malicious. Handle with care.\n");
    return 0;
}
