<#
.SYNOPSIS
  Race a converted track unattended and check it (docs/TRACK-CONVERTER.md).

.EXAMPLE
  tools\tracks\check_track.ps1 -Rom C:\roms\mkds.nds -Course cross_course
  Installs the package tools\tracks\mkds_convert.py left in C:\t\mkds-cross_course\packages into a
  run folder, races three laps on autopilot with the collision probe, and fails (exit 1) on any
  refused draw, dropped BSP record, crash or collision hole. Frames land in <Work>\shots.
#>
param(
	[Parameter(Mandatory)][string]$Rom,
	[Parameter(Mandatory)][string]$Course,
	[string]$Work = "C:\t\mkds-$Course",
	[string]$RunDir = "C:\t\mkds-run",
	[string]$Exe = "$PSScriptRoot\..\..\build-mrg-client\ctr_native_authoring.exe",
	[int]$Frames = 12000
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$pin = (Get-Content (Join-Path $Work 'pin.txt')).Trim()

# devrun copies the repo's assets only into a run folder that has none: do that first, then add the package
$assets = Join-Path $RunDir 'assets'
if (-not (Test-Path $assets)) { New-Item -ItemType Directory -Force $RunDir | Out-Null; Copy-Item (Join-Path $repo 'assets') $assets -Recurse }
$store = Join-Path $assets 'tracks\packages'
New-Item -ItemType Directory -Force $store | Out-Null
Get-ChildItem $store -Directory | Where-Object Name -ne $pin | Remove-Item -Recurse -Force
Copy-Item (Join-Path $Work "packages\$pin") $store -Recurse -Force

$probe = Join-Path $Work 'probe.csv'
$shots = Join-Path $Work 'shots'
Remove-Item $probe, (Join-Path $RunDir 'crash.txt'), $shots -Recurse -Force -ErrorAction SilentlyContinue
& (Join-Path $repo 'tools\decrypted\devrun.ps1') -Track -1 -Frames $Frames -From 1500 -Every 1500 -Exe $Exe -RunDir $RunDir -Out $shots `
	-Env @{CTR_DEV_CUSTOM = $pin; CTR_DEV_AUTOPILOT = 1; CTR_DEV_COLL_PROBE = $probe; CTR_DEV_COLL_PROBE_IN = (Join-Path $Work "${Course}_probe.txt")} | Out-Null

$failed = @()
# release builds put the version in the log's name
$log = (Get-ChildItem $RunDir -Filter 'CTR Decrypted*.log' | Sort-Object LastWriteTime | Select-Object -Last 1).FullName
$text = Get-Content $log -Raw
if ($text -notmatch "custom track $pin, \d+ laps") { $failed += 'the race did not load the package (see the log)' }
# CustomTrackDiag_FlushLevelLoad's report for the custom level (CTR_CUSTOM_LEVEL_ID 0x80)
$report = [regex]::Matches($text, 'level 128 over (\d+) frames: .*?(\d+) primitives across (\d+) leaves\); reserve refused (\d+), rendered list peak (\d+) of (\d+), rendered list full (\d+), bsp records dropped (\d+)', 'Singleline') | Select-Object -Last 1
if ($report -eq $null) { $failed += 'no render report for the custom level' }
else {
	$g = $report.Groups
	"render: $($g[1]) frames, worst frame $($g[2]) primitives, rendered list peak $($g[5]) of $($g[6]), reserve refused $($g[4]), list full $($g[7]), bsp records dropped $($g[8])"
	if ([int]$g[4].Value -ne 0) { $failed += 'primitive reserves refused' }
	if ([int]$g[7].Value -ne 0) { $failed += 'rendered-quadblock list full: blocks were not drawn' }
	if ([int]$g[8].Value -ne 0) { $failed += 'BSP records dropped: subtrees were not drawn' }
}
if (Test-Path (Join-Path $RunDir 'crash.txt')) { $failed += "crash (see $RunDir\crash.txt)" }
if (-not (Test-Path $probe)) { $failed += 'no collision probe output' }
else {
	python (Join-Path $PSScriptRoot 'collision_check.py') $probe $Rom $Course $Work
	if ($LASTEXITCODE -ne 0) { $failed += 'collision holes or ground off its layer' }
}
"frames: $shots"
if ($failed) { $failed | ForEach-Object { Write-Warning $_ }; exit 1 }
'PASS'
