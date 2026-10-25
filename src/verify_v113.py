# -*- coding: utf-8 -*-
# v1.1.3 全量验证：test_console(含i18n) + 隔离XOR/恢复往返 + 单实例 + GUI冒烟
import ctypes, os, subprocess, sys, time
sys.stdout.reconfigure(encoding='utf-8')
u = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
PROD = r'D:\Zcode\杀毒软件\KrakenAV'

def mappings():
    out = []
    m = kernel32.GetLogicalDrives()
    for i in range(26):
        if m & (1 << i):
            d = chr(65 + i) + ':'
            buf = ctypes.create_unicode_buffer(1024)
            if kernel32.QueryDosDeviceW(d, buf, 1024):
                t = buf.value
                if t.startswith(chr(92)) and '??' in t[:6] and 'KrakenAV' in t:
                    out.append(d)
    return out

print('=== 初始盘符:', [chr(65+i)+':' for i in range(26) if kernel32.GetLogicalDrives()>>i & 1])

# ---- 1) GUI 启动（D盘中文路径→自动盘符映射） ----
subprocess.Popen([PROD + r'\KrakenAV.exe'], cwd=PROD)
time.sleep(1.2)
print('父实例退出后盘符映射:', mappings(), '（应为1个K:）')
time.sleep(6)
main1 = u.FindWindowW(None, 'KrakenAV')
print('实例1窗口:', main1)
r = subprocess.run(['powershell', '-NoProfile', '-Command',
                    '(Get-Process KrakenAV -ErrorAction SilentlyContinue).Count'],
                   capture_output=True, text=True)
print('进程数:', r.stdout.strip(), '（应=1）')

# ---- 2) 单实例：再启动一次 ----
subprocess.Popen([PROD + r'\KrakenAV.exe'], cwd=PROD)
time.sleep(4)
r = subprocess.run(['powershell', '-NoProfile', '-Command',
                    '(Get-Process KrakenAV -ErrorAction SilentlyContinue).Count'],
                   capture_output=True, text=True)
print('二次启动后进程数:', r.stdout.strip(), '（应=1）')
print('二次启动后映射:', mappings(), '（应=1个，不新增）')

# ---- 3) test_console（含i18n检查，从映射盘符跑） ----
drv = mappings()[0] if mappings() else None
if drv:
    r = subprocess.run([drv + r'\test_console.exe', drv], capture_output=True, cwd=drv)
    out = r.stdout.decode('utf-16-le', errors='ignore') or r.stdout.decode('gbk', errors='ignore')
    lines = [l for l in out.splitlines() if any(k in l for k in ('PASS', 'FAIL', 'passed', 'i18n'))]
    for l in lines:
        print('  ', l[:90])
    print('test_console exit:', r.returncode)
    # ---- 4) 隔离恢复往返 ----
    qfiles = [f for f in os.listdir(PROD + r'\quarantine') if f.endswith('.quarantine')]
    if qfiles:
        qf = os.path.join(PROD, 'quarantine', sorted(qfiles)[-1])
        raw = open(qf, 'rb').read()
        print('隔离文件已混淆(≠EICAR明文):', not raw.startswith(b'X5O!'))
        dst = qf.replace('.quarantine', '.restored')
        r2 = subprocess.run([drv + r'\quarantine_restore.exe', qf, dst], capture_output=True, cwd=drv)
        content = open(dst, 'rb').read()
        eicar = b'X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*'
        print('恢复内容==EICAR原文:', content == eicar)
        os.remove(dst)
    else:
        print('无隔离文件（test_console应已产生）')
else:
    print('!! 无映射盘符，test_console跳过')

# ---- 5) 关闭 GUI：映射应清零 ----
u.PostMessageW(main1, 0x0010, 0, 0)
time.sleep(3)
print('退出后映射:', mappings(), '（应为空）')
print('最终盘符:', [chr(65+i)+':' for i in range(26) if kernel32.GetLogicalDrives()>>i & 1])
