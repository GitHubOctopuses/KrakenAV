# -*- coding: utf-8 -*-
# 创建 GitHub Release 并上传 zip 资产
import json, os, subprocess, sys, urllib.request
sys.stdout.reconfigure(encoding='utf-8')

def token():
    p = subprocess.run(['git', 'credential', 'fill'], input='protocol=https\nhost=github.com\n',
                       capture_output=True, text=True)
    for line in p.stdout.splitlines():
        if line.startswith('password='):
            return line.split('=', 1)[1]
    raise SystemExit('no token')

T = token()
HDR = {'Authorization': f'token {T}', 'Accept': 'application/vnd.github+json'}
REPO = 'GitHubOctopuses/KrakenAV'

body = {
    'tag_name': 'v1.1.1',
    'target_commitish': 'main',
    'name': 'KrakenAV v1.1.1',
    'body': (
        'First public release.\n\n'
        '**KrakenAV** - open-source Windows antivirus built on the ClamAV 1.5.4 engine.\n\n'
        '### What is included\n'
        '- `KrakenAV.exe` + engine DLLs + `freshclam.exe` + `certs/` (complete runtime)\n'
        '- `db_backup.7z` - AES-encrypted virus-database snapshot (~3.64M signatures, password: `KrakenAV2026`) '
        'so you can restore the DB even if another antivirus quarantines it\n'
        '- `7zr.exe` - extractor for the backup\n'
        '- `test_console.exe` - 13-check self-test\n\n'
        '### Quick start\n'
        '1. Unzip anywhere (Chinese paths are fine) and run `KrakenAV.exe`\n'
        '2. Restore the database: `7zr.exe x -pKrakenAV2026 db_backup.7z -aoa -odatabase`\n'
        '   (or click *Update DB* in-app for the latest signatures, ~110 MB)\n'
        '3. Scan. Reports land in `reports\\`, quarantined files in `quarantine\\`\n\n'
        'Requires Windows 10/11 x64. GPLv2 - see LICENSE.\n'
    ),
    'draft': False,
    'prerelease': False,
}
req = urllib.request.Request(f'https://api.github.com/repos/{REPO}/releases',
                             data=json.dumps(body).encode(), headers=HDR, method='POST')
try:
    with urllib.request.urlopen(req) as r:
        rel = json.load(r)
    print('release created:', rel['html_url'], 'id:', rel['id'])
except urllib.error.HTTPError as e:
    print('create failed:', e.code, e.read().decode()[:300])
    raise SystemExit(1)

# 上传 zip
zip_path = r'D:\Zcode\杀毒软件\KrakenAV\KrakenAV-v1.1.1-win64.zip'
size = os.path.getsize(zip_path)
name = os.path.basename(zip_path)
url = f'https://uploads.github.com/repos/{REPO}/releases/{rel["id"]}/assets?name={name}'
req2 = urllib.request.Request(url, data=open(zip_path, 'rb').read(),
                              headers={**HDR, 'Content-Type': 'application/zip'}, method='POST')
print(f'uploading {name} ({size/1048576:.1f} MB)...')
with urllib.request.urlopen(req2, timeout=1800) as r:
    asset = json.load(r)
print('asset uploaded:', asset['browser_download_url'], f"{asset['size']/1048576:.1f} MB")
