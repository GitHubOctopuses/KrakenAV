# -*- coding: utf-8 -*-
# 打 Release zip：程序运行时（exe/DLL/certs/7zr/README，不含毒库与运行数据）
import os, shutil, subprocess, sys, zipfile
sys.stdout.reconfigure(encoding='utf-8')

PROD = r'D:\Zcode\杀毒软件\KrakenAV'
STAGE = r'D:\Zcode\杀毒软件\KrakenAV_release_stage'
OUT = os.path.join(PROD, 'KrakenAV-v1.1.1-win64.zip')

if os.path.isdir(STAGE):
    shutil.rmtree(STAGE, ignore_errors=True)
os.makedirs(STAGE)

# 主程序 + DLL + freshclam + certs + COPYING
shutil.copy2(os.path.join(PROD, 'KrakenAV.exe'), STAGE)
n = 0
for f in os.listdir(PROD):
    if f.endswith('.dll'):
        shutil.copy2(os.path.join(PROD, f), STAGE); n += 1
shutil.copy2(os.path.join(PROD, 'freshclam.exe'), STAGE)
shutil.copytree(os.path.join(PROD, 'certs'), os.path.join(STAGE, 'certs'))
shutil.copy2(os.path.join(PROD, 'COPYING.txt'), STAGE)
shutil.copy2(os.path.join(PROD, 'README.md'), STAGE)
shutil.copy2(os.path.join(PROD, '7zr.exe'), STAGE)
shutil.copy2(os.path.join(PROD, 'test_console.exe'), STAGE)
for d in ['database', 'quarantine', 'reports', 'conf']:
    os.makedirs(os.path.join(STAGE, d), exist_ok=True)
# db_backup.7z 一并放入（首次离线恢复用）
shutil.copy2(os.path.join(PROD, 'db_backup.7z'), STAGE)

# zip（store方式对已压缩数据更快，但7z包用deflate无妨）
if os.path.exists(OUT):
    os.remove(OUT)
with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for root, dirs, files in os.walk(STAGE):
        for f in files:
            fp = os.path.join(root, f)
            arc = os.path.relpath(fp, STAGE)
            z.write(fp, os.path.join('KrakenAV', arc))
shutil.rmtree(STAGE, ignore_errors=True)
print('zip:', OUT, f'{os.path.getsize(OUT)/1048576:.1f} MB')
