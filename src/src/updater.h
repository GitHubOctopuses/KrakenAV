// KrakenAV - updater.h
#pragma once
#include "common.h"

// 显示更新窗口（非模态，可重复调用置前）。freshclam 成功后向 mainWnd 发 WM_APP+4
void ShowUpdateWindow(HWND mainWnd);
bool UpdateRunning();
void UpdateForceClose();
