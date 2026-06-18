# MyMD per-user 등록 제거 (install.ps1 로 등록한 항목 되돌림)
$ErrorActionPreference = 'SilentlyContinue'

$exe = Join-Path $PSScriptRoot 'MyMD.exe'
$dir = Split-Path $exe -Parent

# App Paths
Remove-Item -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\mymd.exe' -Recurse -Force

# 사용자 PATH 에서 제거
$curPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if ($curPath) {
  $parts = $curPath.Split(';') | Where-Object { $_ -ne '' -and $_ -ne $dir }
  [Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')
}

# 시작 메뉴 바로가기
Remove-Item -Path (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\MyMD.lnk') -Force

# 파일 형식 등록
$classes = 'HKCU:\Software\Classes'
$progId = 'MyMD.markdown'
Remove-Item -Path "$classes\$progId" -Recurse -Force
Remove-Item -Path "$classes\Applications\MyMD.exe" -Recurse -Force
foreach ($ext in '.md', '.markdown') {
  Remove-ItemProperty -Path "$classes\$ext\OpenWithProgids" -Name $progId -Force
  $cur = (Get-ItemProperty -Path "$classes\$ext" -Name '(default)' -ErrorAction SilentlyContinue).'(default)'
  if ($cur -eq $progId) { Set-ItemProperty -Path "$classes\$ext" -Name '(default)' -Value '' }
}

Add-Type -Namespace Win32 -Name Shell -MemberDefinition @'
[DllImport("shell32.dll")] public static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);
'@
[Win32.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)

Write-Host "MyMD 등록 제거 완료 (새 터미널부터 PATH 반영)."
