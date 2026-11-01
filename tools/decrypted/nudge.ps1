<#
.SYNOPSIS
  Find what is still drawn from the game's rounded (integer) camera in one
  frame of a scene: rounding judder and wobble, before a player sees it.

.EXAMPLE
  tools\decrypted\nudge.ps1 -Frame 4000 -Env @{CTR_DEV_RACE=26; CTR_DEV_MODE='adv'}
  Runs the Adventure hub to frame 4000 once as is and once per camera axis
  with player 1's integer camera nudged by a unit (CTR_DEV_NUDGE), all in
  parallel, and prints tools\decrypted\nudge_diff.py's report for each axis.
  Anything listed jumps whenever the game's rounding of the camera steps.

.NOTES
  The scene switches are devrun.ps1's (docs/DEV-TOOLS.md). The frame must show
  the follow camera (a race or a hub, not a menu or the race intro).
#>
param(
	[Parameter(Mandatory)] [int]$Frame,
	[hashtable]$Env = @{},
	[hashtable]$Config = @{},
	[string[]]$Axes = @('pitch', 'yaw', 'roll', 'x', 'y', 'z'),
	[int]$By = 1,
	[string]$Exe = "$PSScriptRoot\..\..\build-vanilla\ctr_native.exe",
	[string]$Out = ""                # default: new folder under %TEMP%\ctrdev
)
$ErrorActionPreference = 'Stop'

if (-not $Out) { $Out = Join-Path (Join-Path $env:TEMP 'ctrdev') ("nudge-" + (Get-Date -Format 'HHmmss')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$devrun = Join-Path $PSScriptRoot 'devrun.ps1'
$exePath = (Resolve-Path $Exe).Path

$runs = @('base') + $Axes
$jobs = foreach ($name in $runs) {
	$env2 = @{} + $Env
	$env2['CTR_DEV_DUMP_FRAME'] = $Frame
	if ($name -ne 'base') {
		$env2['CTR_DEV_NUDGE'] = $name
		$env2['CTR_DEV_NUDGE_BY'] = $By
	}
	Start-Job -ArgumentList $devrun, $exePath, $Frame, (Join-Path $Out $name), (Join-Path $Out "run-$name"), $env2, $Config -ScriptBlock {
		param($devrun, $exe, $frame, $out, $run, $env2, $config)
		& $devrun -Track -1 -Frames ($frame + 1) -From $frame -Every 1 -Exe $exe -Out $out -RunDir $run -Env $env2 -Config $config | Out-Null
	}
}
$jobs | Wait-Job | Receive-Job
$jobs | Remove-Job

$dump = 'polys-{0:D6}.txt' -f $Frame
$base = Join-Path (Join-Path $Out 'base') $dump
$status = 0
foreach ($axis in $Axes) {
	Write-Output "== $axis +$By"
	python (Join-Path $PSScriptRoot 'nudge_diff.py') $base (Join-Path (Join-Path $Out $axis) $dump) --png (Join-Path $Out "$axis.png")
	if ($LASTEXITCODE -ne 0) { $status = 1 }
}
Write-Output "Frames, dumps and paintings: $Out"
exit $status
