# -*- coding: utf-8 -*-
# 重新截取关于页（严格确认窗口）
import subprocess, sys, time, ctypes, os
sys.stdout.reconfigure(encoding='utf-8')
ctypes.windll.user32.SetProcessDPIAware()
u = ctypes.windll.user32
PROD = r'D:\Zcode\杀毒软件\KrakenAV'

subprocess.Popen([PROD + r'\KrakenAV.exe'], cwd=PROD)
time.sleep(8)
main = u.FindWindowW(None, 'KrakenAV')
print('主窗口:', main)
assert main, 'window not found'

# 切关于页
lp = (int(252*1.5) << 16) | int(95*1.5)
u.PostMessageW(main, 0x0201, 1, lp); time.sleep(0.15)
u.PostMessageW(main, 0x0202, 0, lp); time.sleep(0.6)

class RECT(ctypes.Structure):
    _fields_ = [('l', ctypes.c_long), ('t', ctypes.c_long), ('r', ctypes.c_long), ('b', ctypes.c_long)]
rect = RECT()
u.GetWindowRect(main, ctypes.byref(rect))
print('窗口矩形:', rect.l, rect.t, rect.r, rect.b)
u.SetForegroundWindow(main)
time.sleep(0.6)
from PIL import Image, ImageGrab
img = ImageGrab.grab(bbox=(rect.l, rect.t, rect.r, rect.b), all_screens=True)
img.save(os.path.join(PROD, 'docs', 'screenshots', 'about.png'))
print('已保存 about.png', img.size)
u.PostMessageW(main, 0x0010, 0, 0)
time.sleep(2)
print('程序已关闭')
