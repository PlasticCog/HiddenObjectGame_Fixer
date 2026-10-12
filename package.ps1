# Assembles a release zip from a completed Release build.
# Usage: powershell -ExecutionPolicy Bypass -File package.ps1 [-Version v0.1.0]
param([string]$Version = "v0.1.1")
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$bin = Join-Path $root "build\Release\bin"
foreach ($f in "HOGFixer.exe", "hogfix.dll", "hogrun.exe") {
    if (-not (Test-Path (Join-Path $bin $f))) { throw "missing $f - build Release first (build.cmd Release)" }
}

$stage = Join-Path $env:TEMP "hogfixer_pkg\HOGFixer-$Version"
if (Test-Path (Split-Path $stage)) { Remove-Item (Split-Path $stage) -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item (Join-Path $bin "HOGFixer.exe") $stage
Copy-Item (Join-Path $bin "hogfix.dll") $stage
Copy-Item (Join-Path $bin "hogrun.exe") $stage
Copy-Item (Join-Path $root "docs\QUICKSTART.md") $stage
Copy-Item (Join-Path $root "LICENSE") (Join-Path $stage "LICENSE.txt")

# Third-party license bundle.
$notice = Join-Path $stage "NOTICE.txt"
"HOG Fixer $Version - third-party component licenses`r`n" | Out-File -Encoding utf8 $notice
"This release statically links MinHook (BSD-2), SDL3 (zlib) and Dear ImGui (MIT).`r`n" | Out-File -Encoding utf8 -Append $notice
foreach ($c in "minhook", "SDL", "imgui") {
    "`r`n################################################################" | Out-File -Encoding utf8 -Append $notice
    "## $c" | Out-File -Encoding utf8 -Append $notice
    "################################################################`r`n" | Out-File -Encoding utf8 -Append $notice
    foreach ($lf in "LICENSE.txt", "LICENSE.md", "LICENSE") {
        $p = Join-Path $root "third_party\$c\$lf"
        if (Test-Path $p) { Get-Content $p | Out-File -Encoding utf8 -Append $notice; break }
    }
}

$zip = Join-Path $root "HOGFixer-$Version-win32.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Split-Path $stage -Leaf | ForEach-Object { Join-Path (Split-Path $stage) $_ }) -DestinationPath $zip
Write-Host "Packaged: $zip"
Get-ChildItem $stage | Select-Object Name, Length
