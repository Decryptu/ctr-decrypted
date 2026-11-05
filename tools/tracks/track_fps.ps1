<#
.SYNOPSIS
  Time a converted track at the owner's settings (docs/TRACK-CONVERTER.md).

.EXAMPLE
  tools\tracks\track_fps.ps1 -Course clock_course
  Races the package tools\tracks\mkds_convert.py left in C:\t\mkds-clock_course\packages on autopilot,
  eight karts, far camera, increased draw distance, a 1920x1080 window, and leaves frame_times.csv and
  trace.csv in <Work>\fps. Nothing else should be running: a frame's time is the point.
#>
param(
	[Parameter(Mandatory)][string]$Course,
	[string]$Work = "C:\t\mkds-$Course",
	[string]$RunDir = "C:\t\mkds-fps",
	[string]$Exe = "$PSScriptRoot\..\..\build-mrg-client\ctr_native_authoring.exe",
	[int]$Frames = 5200
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

$out = Join-Path $Work 'fps'
Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $out | Out-Null
& (Join-Path $repo 'tools\decrypted\devrun.ps1') -Track -1 -Frames $Frames -From 100000000 -Exe $Exe -RunDir $RunDir -Out $out -TimeoutSec 900 `
	-Config @{increase_draw_distance = 'true'; camera_far = 'true'; window_w = 1920; window_h = 1080; window_maximized = 'false'} `
	-GameArgs '--perf-dir', $out `
	-Env @{CTR_DEV_CUSTOM = $pin; CTR_DEV_AUTOPILOT = 3; CTR_DEV_TRACE = (Join-Path $out 'trace.csv')} | Out-Null
