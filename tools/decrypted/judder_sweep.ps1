<#
.SYNOPSIS
  Look for frame-to-frame judder from integer rounding on every race track.

.DESCRIPTION
  Drives a 60 FPS Time Trial on each track (and, unless -NoHub, a drive around
  N. Sanity Beach), hidden and fast-forwarded, and traces each with
  CTR_DEV_TRACE. In the races the autopilot steers along the nav line while a
  CTR_DEV_DRIVE script cuts in to coast (no throttle) and brake, so every run
  has speeding up, coasting, braking, steady speed and turns. The hub drive is
  a CTR_DEV_PAD script of the same moves.

  race_trace.py judder then reads every trace and writes a ranked table of
  suspects (channel, phase, track, size, signature) to judder.txt in -Out.
  See docs/DEV-TOOLS.md, "Unattended races".

.EXAMPLE
  tools\decrypted\judder_sweep.ps1
  tools\decrypted\judder_sweep.ps1 -Tracks 3,7 -NoHub -Out C:\t\jd
#>
param(
	[int[]]$Tracks = 0..17,            # LevelIDs (include/namespace_Level.h)
	[switch]$NoHub,                    # skip the N. Sanity Beach drive
	[int]$Frames = 7000,               # presented frames per race (boot is about 1300); the hub takes 9800
	[int]$Parallel = 4,                # runs at once, each in its own run folder
	[string]$Out = "C:\t\judder",
	[string]$Exe = "$PSScriptRoot\..\..\build-vanilla\ctr_native.exe"
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null
$exePath = (Resolve-Path $Exe).Path
$devrun = Join-Path $PSScriptRoot 'devrun.ps1'

# A 10 s cycle in race time: the autopilot drives (speeding up, steady, turns),
# then 1.5 s of coasting (Up holds nothing that moves the kart, and a held
# script key keeps the autopilot off), the autopilot speeds up again, then a
# 0.6 s brake (Square).
$drive = (0..11 | ForEach-Object { $t = $_ * 10000; "$($t + 3500):U:1500,$($t + 7500):S:600" }) -join ','

# The hub: Aku Aku's hint ends near frame 4000. Loops on the beach (throttle
# and one direction), straights, coasting and braking, in presented frames
# (60 FPS); tight loops keep the kart off the warp pads near the spawn.
$hubPad = (0..3 | ForEach-Object { $t = 4050 + $_ * 1400
	"$($t):XL:400,$($t + 400):X:150,$($t + 550):U:120,$($t + 670):XR:400,$($t + 1070):S:40,$($t + 1110):X:290" }) -join ','

$jobs = @()
foreach ($t in $Tracks) {
	$jobs += @{ Name = "t$t"; Frames = $Frames; Env = @{ CTR_DEV_RACE = $t; CTR_DEV_MODE = 'tt'; CTR_DEV_AUTOPILOT = 1; CTR_DEV_DRIVE = $drive; CTR_DEV_EXIT_AFTER_RACE = 1 } }
}
if (-not $NoHub) {
	$jobs += @{ Name = 'hub26'; Frames = 9800; Env = @{ CTR_DEV_RACE = 26; CTR_DEV_MODE = 'adv'; CTR_DEV_PAD = $hubPad } }
}

$running = @()
for ($k = 0; $k -lt $jobs.Count; $k++) {
	$job = $jobs[$k]
	# Job k uses run folder k mod Parallel: wait for the job before it there.
	if ($k -ge $Parallel) { $running[$k - $Parallel] | Wait-Job | Out-Null }
	$dir = Join-Path $Out $job.Name
	Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
	New-Item -ItemType Directory -Force $dir | Out-Null
	$envs = $job.Env.Clone()
	$envs.CTR_DEV_TRACE = Join-Path $dir 'trace.csv'
	$runDir = Join-Path $Out ("run" + ($k % $Parallel))
	$running += Start-Job -ScriptBlock {
		param($devrun, $exe, $runDir, $dir, $frames, $envs)
		& $devrun -Exe $exe -RunDir $runDir -Track -1 -Frames $frames -From 100000000 -Out $dir `
			-Config @{ fps60 = 'true'; skip_intro = 'true' } -Env $envs -TimeoutSec 900 | Out-Null
	} -ArgumentList $devrun, $exePath, $runDir, $dir, $job.Frames, $envs
}
$running | Wait-Job | Receive-Job -ErrorAction Continue
$running | Remove-Job

$traces = @(Get-ChildItem $Out -Recurse -Filter trace.csv | ForEach-Object FullName)
foreach ($j in $jobs) {
	if (-not (Test-Path (Join-Path $Out "$($j.Name)\trace.csv"))) { Write-Warning "$($j.Name): no trace (see its run folder's log)" }
}
$report = Join-Path $Out 'judder.txt'
python -I (Join-Path $PSScriptRoot 'race_trace.py') judder $traces | Tee-Object $report
Write-Output "Report: $report"
