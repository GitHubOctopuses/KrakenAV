# -*- coding: utf-8 -*-
# PrintWindow 离屏捕获：无视屏幕遮挡，直接渲染窗口自身内容
import subprocess, sys, time, ctypes, os
sys.stdout.reconfigure(encoding='utf-8')
ctypes.windll.user32.SetProcessDPIAware()
u = ctypes.windll.user32
g = ctypes.windll.gdi32
PROD = r'D:\Zcode\杀毒软件\KrakenAV'

subprocess.Popen([PROD + r'\KrakenAV.exe'], cwd=PROD)
time.sleep(8)
main = u.FindWindowW(None, 'KrakenAV')
assert main, 'window not found'

# 切关于页
lp = (int(252*1.5) << 16) | int(95*1.5)
u.PostMessageW(main, 0x0201, 1, lp); time.sleep(0.15)
u.PostMessageW(main, 0x0202, 0, lp); time.sleep(0.6)

class RECT(ctypes.Structure):
    _fields_ = [('l', ctypes.c_long), ('t', ctypes.c_long), ('r', ctypes.c_long), ('b', ctypes.c_long)]
rect = RECT()
u.GetWindowRect(main, ctypes.byref(rect))
w, h = rect.r - rect.l, rect.b - rect.t

hdc = u.GetDC(main)
mem = g.CreateCompatibleDC(hdc)
bmp = g.CreateCompatibleBitmap(hdc, w, h)
g.SelectObject(mem, bmp)
# PW_RENDERFULLCONTENT = 2
ok = u.PrintWindow(main, mem, 2)
print('PrintWindow:', ok)

class BMIH(ctypes.Structure):
    _fields_ = [('biSize', ctypes.c_uint32), ('biWidth', ctypes.c_int32), ('biHeight', ctypes.c_int32),
                ('biPlanes', ctypes.c_uint16), ('biBitCount', ctypes.c_uint16), ('biCompression', ctypes.c_uint32),
                ('biSizeImage', ctypes.c_uint32), ('biXPPM', ctypes.c_int32), ('biYPPM', ctypes.c_int32),
                ('biClrUsed', ctypes.c_uint32), ('biClrImportant', ctypes.c_uint32)]
class BMI(ctypes.Structure):
    _fields_ = [('bmiHeader', BMIH), ('colors', ctypes.c_uint32 * 3)]
bi = BMIH()
bi.biSize = ctypes.sizeof(bi)
bi.biWidth, bi.biHeight, bi.biPlanes, bi.biBitCount = w, -h, 1, 32
bi.biCompression = 0
buf = ctypes.create_string_buffer(w * h * 4)
g.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(BMI(bi)), 0)

from PIL import Image
img = Image.frombytes('RGBA', (w, h), buf.raw, 'raw', 'BGRA').convert('RGB')
img.save(os.path.join(PROD, 'docs', 'screenshots', 'about.png'))
print('saved', img.size)

g.DeleteObject(bmp)
g.DeleteDC(mem)
u.ReleaseDC(main, hdc)
u.PostMessageW(main, 0x0010, 0, 0)
time.sleep(2)
print('done')
