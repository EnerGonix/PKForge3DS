# Runs PKForge.3dsx in Azahar with an autotest script and converts its shots to PNG.
# Usage: powershell -File tools\run-autotest.ps1 <script.txt> <png out dir> [timeout seconds] [app]
# app defaults to PKForge.3dsx; build\cia\PKForge.cxi runs what the CIA carries.
param([string]$Script, [string]$Out, [int]$Timeout = 180, [string]$App = "")
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sd = "$env:APPDATA\Azahar\sdmc\3ds\PKForge"
New-Item -ItemType Directory -Force "$sd\shots" | Out-Null
$stamp = Get-Date
Copy-Item $Script "$sd\autotest.txt" -Force
$expected = (Select-String -Path $Script -Pattern '^shot ').Count
if (-not $App) { $App = "$root\PKForge.3dsx" }
$p = Start-Process -FilePath "C:\Program Files\Azahar\azahar.exe" -ArgumentList "`"$App`"" -PassThru
$deadline = (Get-Date).AddSeconds($Timeout)
while ((Get-Date) -lt $deadline) {
    $fresh = @(Get-ChildItem "$sd\shots" -Filter *.bmp | Where-Object { $_.LastWriteTime -gt $stamp })
    if ($fresh.Count -ge $expected -or $p.HasExited) { break }
    Start-Sleep -Milliseconds 1000
}
Start-Sleep -Seconds 2
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
New-Item -ItemType Directory -Force $Out | Out-Null
$fresh = @(Get-ChildItem "$sd\shots" -Filter *.bmp | Where-Object { $_.LastWriteTime -gt $stamp })
$tmp = Join-Path $env:TEMP ("pkforge-shots-" + (Get-Date -Format yyyyMMddHHmmss))
New-Item -ItemType Directory -Force $tmp | Out-Null
foreach ($f in $fresh) { Copy-Item $f.FullName $tmp -Force }
# MSYS2's python reads C:/ paths, not backslashes.
$slash = { param($p) [regex]::Replace(($p -replace '\\', '/'), '^([A-Za-z]):', { param($m) '/' + $m.Groups[1].Value.ToLower() }) }
& C:\msys64\usr\bin\python3.exe -I (& $slash "$root\tools\bmp2png.py") (& $slash $tmp) (& $slash $Out)
"$($fresh.Count) of $expected shots"
