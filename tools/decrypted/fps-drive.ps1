<#
.SYNOPSIS
  Drive the same scripted input at 30 and at 60 FPS and compare the kart.

.DESCRIPTION
  Both runs boot into a Time Trial on -Track with nobody at the controls but
  -Drive (CTR_DEV_DRIVE, timed in race milliseconds, so both frame rates get
  the same input at the same moment), trace player 1 (CTR_DEV_TRACE) and quit
  after -Until ms of race time. race_trace.py fps then prints, per field, how
  far the 60 FPS run strays from the 30 FPS one. Physics that scale with frame
  time keep every field close until the runs part; a field that strays alone,
  early, is a frame-counted step that was not converted for 60 FPS.

.EXAMPLE
  tools\decrypted\fps-drive.ps1 -Track 3 -Drive '0:X:5000,1500:L:1500' -Until 4000
  Throttle from the lights, steering left from 1.5 s to 3 s, on Crash Cove.
#>
param(
	[int]$Track = 3,
	[Parameter(Mandatory)][string]$Drive,
	[int]$Until = 4000,
	[string]$Out = "$env:TEMP\ctrdev\fpsdrive",
	[string]$Exe = "$PSScriptRoot\..\..\build-vanilla\ctr_native.exe"
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null

$jobs = foreach ($fps60 in 'false', 'true') {
	Start-Job -ArgumentList $fps60, $Track, $Drive, $Out, (Resolve-Path $Exe).Path, $PSScriptRoot -ScriptBlock {
		param($fps60, $track, $drive, $out, $exe, $tools)
		$dir = Join-Path $out $fps60
		New-Item -ItemType Directory -Force $dir | Out-Null
		$envs = @{ CTR_DEV_RACE = $track; CTR_DEV_MODE = 'tt'; CTR_DEV_DRIVE = $drive; CTR_DEV_TRACE = "$dir\trace.csv" }
		# The lights take about 1,200 frames at 30 FPS; the cap ends the run
		# well after -Until, which the comparison trims to.
		& "$tools\devrun.ps1" -Exe $exe -RunDir (Join-Path $out "run-$fps60") -Track -1 -Frames 6000 -From 1000000 -Out $dir `
			-Config @{ fps60 = $fps60 } -Env $envs -TimeoutSec 300 | Out-Null
	}
}
$jobs | Wait-Job | Receive-Job
$jobs | Remove-Job
python -I (Join-Path $PSScriptRoot 'race_trace.py') fps "$Out\false\trace.csv" "$Out\true\trace.csv" --until $Until
