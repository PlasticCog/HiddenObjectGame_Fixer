# Imports + auto-analyzes each game executable in its own Ghidra project (so they can run
# in parallel), then runs ExportInteresting.java to dump the compatibility-relevant code.
param(
    [string]$GhidraDir = "C:\Users\Plastik2024\Tools\ghidra_12.1.4_PUBLIC",
    [string]$WorkDir   = "$env:TEMP\hogfixer_ghidra",
    [string]$BundleDir = (Resolve-Path "$PSScriptRoot\..\..").Path,
    [int]$MaxParallel  = 5,
    [string[]]$Only
)

$targets = @(
    @{ Name = "LakeHouse";  Exe = "My Games\2\LakeHouse_ChildrenOfSilence.exe"; Mem = "6G" },
    @{ Name = "Kronville";  Exe = "My Games\4\Kronville_StolenDreams.exe";      Mem = "3G" },
    @{ Name = "Mexicana";   Exe = "My Games\6\MexicanaDeadlyHoliday.exe";       Mem = "3G" },
    @{ Name = "OtherSide";  Exe = "My Games\8\TheOtherSide_TowerofSouls.exe";   Mem = "3G" },
    @{ Name = "EMDM";       Exe = "My Games\3\EMDM.exe";                        Mem = "2G" },
    @{ Name = "Panopticon"; Exe = "My Games\5\PanopticonPathofReflections.exe"; Mem = "3G" },
    @{ Name = "CruelGames"; Exe = "My Games\7\CruelGames.exe";                  Mem = "3G" },
    @{ Name = "Launcher";   Exe = "HiddenObject6in1.exe";                       Mem = "2G" }
)
if ($Only) { $targets = $targets | Where-Object { $Only -contains $_.Name } }

$scripts = Join-Path $PSScriptRoot "ghidra_scripts"
$headless = Join-Path $GhidraDir "support\analyzeHeadless.bat"
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$outDir = Join-Path $WorkDir "out"
New-Item -ItemType Directory -Force $outDir | Out-Null

$running = @()
foreach ($t in $targets) {
    while (($running | Where-Object { -not $_.HasExited }).Count -ge $MaxParallel) { Start-Sleep -Seconds 5 }
    $proj = Join-Path $WorkDir "proj_$($t.Name)"
    New-Item -ItemType Directory -Force $proj | Out-Null
    $exe = Join-Path $BundleDir $t.Exe
    $log = Join-Path $WorkDir "$($t.Name).log"
    $env:GHIDRA_HEADLESS_MAXMEM = $t.Mem
    $argList = "`"$proj`" $($t.Name) -import `"$exe`" -overwrite -scriptPath `"$scripts`" " +
               "-postScript ExportInteresting.java `"$outDir`" -analysisTimeoutPerFile 5400"
    Write-Host "Starting $($t.Name) -> $log"
    $p = Start-Process -FilePath $headless -ArgumentList $argList -PassThru -WindowStyle Hidden `
         -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    $running += $p
}
$running | ForEach-Object { $_.WaitForExit() }
Write-Host "All Ghidra jobs finished. Output in $outDir"
