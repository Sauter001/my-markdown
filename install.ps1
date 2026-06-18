# MyMD per-user 등록 (관리자 권한 불필요)
#  1) 'mymd' 명령 실행: App Paths + 사용자 PATH + 시작 메뉴 바로가기
#  2) .md / .markdown 연결 프로그램 등록 (기본값은 최초 1회 사용자가 선택)
# 제거하려면 uninstall.ps1 실행.

$ErrorActionPreference = 'Stop'

$exe = Join-Path $PSScriptRoot 'MyMD.exe'
if (-not (Test-Path $exe)) { throw "MyMD.exe 가 없습니다. 먼저 build.bat 로 빌드하세요: $exe" }
$dir = Split-Path $exe -Parent
$cmd = '"{0}" "%1"' -f $exe

Write-Host "대상 실행 파일: $exe"

# --- 1) App Paths (실행/검색창에서 'mymd') ---------------------------------
$appPaths = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\mymd.exe'
New-Item -Path $appPaths -Force | Out-Null
Set-ItemProperty -Path $appPaths -Name '(default)' -Value $exe
Set-ItemProperty -Path $appPaths -Name 'Path' -Value $dir
Write-Host "App Paths 등록 완료 (Win+R 또는 검색창에서 'mymd')"

# --- 2) 사용자 PATH 추가 (cmd/powershell 에서 'mymd') -----------------------
$curPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (-not $curPath) { $curPath = '' }
$parts = $curPath.Split(';') | Where-Object { $_ -ne '' }
if ($parts -notcontains $dir) {
  $newPath = (@($parts) + $dir) -join ';'
  [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
  Write-Host "사용자 PATH에 추가: $dir (새 터미널부터 적용)"
} else {
  Write-Host "사용자 PATH에 이미 등록됨"
}

# --- 3) 시작 메뉴 바로가기 (Windows 검색에 'MyMD' 노출) ---------------------
$startDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$lnkPath = Join-Path $startDir 'MyMD.lnk'
$ws = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut($lnkPath)
$lnk.TargetPath = $exe
$lnk.WorkingDirectory = $dir
$lnk.Description = 'MyMD 마크다운 에디터'
$lnk.Save()
Write-Host "시작 메뉴 바로가기 생성: $lnkPath"

# --- 4) 파일 형식 등록 (ProgID + 연결 프로그램 목록) ------------------------
$classes = 'HKCU:\Software\Classes'
$progId = 'MyMD.markdown'

New-Item -Path "$classes\$progId\shell\open\command" -Force | Out-Null
Set-ItemProperty -Path "$classes\$progId" -Name '(default)' -Value 'Markdown 문서'
Set-ItemProperty -Path "$classes\$progId\shell\open\command" -Name '(default)' -Value $cmd
New-Item -Path "$classes\$progId\DefaultIcon" -Force | Out-Null
Set-ItemProperty -Path "$classes\$progId\DefaultIcon" -Name '(default)' -Value ('{0},0' -f $exe)

foreach ($ext in '.md', '.markdown') {
  New-Item -Path "$classes\$ext\OpenWithProgids" -Force | Out-Null
  New-ItemProperty -Path "$classes\$ext\OpenWithProgids" -Name $progId -PropertyType String -Value '' -Force | Out-Null
  # 기존 기본값이 없으면 best-effort 로 기본 ProgID 지정 (Win10 UserChoice 가 있으면 무시됨)
  $cur = (Get-ItemProperty -Path "$classes\$ext" -Name '(default)' -ErrorAction SilentlyContinue).'(default)'
  if (-not $cur) { Set-ItemProperty -Path "$classes\$ext" -Name '(default)' -Value $progId }
}

# Applications 등록 ("연결 프로그램"에 친화적 이름과 지원 형식 노출)
$appExe = "$classes\Applications\MyMD.exe"
New-Item -Path "$appExe\shell\open\command" -Force | Out-Null
Set-ItemProperty -Path "$appExe\shell\open\command" -Name '(default)' -Value $cmd
Set-ItemProperty -Path "$appExe" -Name 'FriendlyAppName' -Value 'MyMD'
New-Item -Path "$appExe\SupportedTypes" -Force | Out-Null
New-ItemProperty -Path "$appExe\SupportedTypes" -Name '.md' -PropertyType String -Value '' -Force | Out-Null
New-ItemProperty -Path "$appExe\SupportedTypes" -Name '.markdown' -PropertyType String -Value '' -Force | Out-Null
Write-Host "파일 형식 등록 완료 (.md, .markdown)"

# --- 5) 탐색기에 연결 변경 통지 --------------------------------------------
Add-Type -Namespace Win32 -Name Shell -MemberDefinition @'
[DllImport("shell32.dll")] public static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);
'@
[Win32.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero) # SHCNE_ASSOCCHANGED

Write-Host ""
Write-Host "등록 완료."
Write-Host "- 새 터미널에서 'mymd' 또는 'mymd 파일.md' 로 실행"
Write-Host "- Win+R 또는 시작 검색에서 'mymd' / 'MyMD'"
Write-Host "- .md 를 기본으로 열려면: .md 파일 우클릭 > 연결 프로그램 > 다른 앱 선택 > MyMD > 항상"
