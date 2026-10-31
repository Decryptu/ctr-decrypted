<#
.SYNOPSIS
  Drive the same Time Trial at 30 and at 60 FPS on each track and compare them.

.DESCRIPTION
  Each run boots straight into a Time Trial (CTR_DEV_RACE, CTR_DEV_MODE=tt),
  drives it with the autopilot (CTR_DEV_AUTOPILOT=1, the AI's nav line through
  the normal kart physics) and traces it (CTR_DEV_TRACE). race_trace.py parity
  then prints, per track, both finish times and how jumps launched from the
  same spot compare. The 30 FPS ghost is also raced at 60 and its drift from
  the run that recorded it is printed next to the same race at 30.

  The autopilot reacts once per frame, so it never drives exactly the same
  line and its laps vary by a few percent even at one frame rate: read the
  finish times for stalls and falls that happen at one rate only. Matched
  jumps land within a 30 FPS frame of airtime. For smaller differences, use
  fps-drive.ps1 (the same scripted input at both rates).

.EXAMPLE
  tools\decrypted\fps-parity.ps1 -Tracks 3,7 -Ghosts
#>
param(
	[int[]]$Tracks = 0..17,           # LevelIDs (include/namespace_Level.h)
	[switch]$Ghosts,                  # also race the 30 FPS ghost at 30 and at 60
	[string]$Out = "$env:TEMP\ctrdev\parity",
	[string]$Exe = "$PSScriptRoot\..\..\build-vanilla\ctr_native.exe"
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null
$trace = Join-Path $PSScriptRoot 'race_trace.py'

function Run([string]$name, [int]$track, [string]$fps60, [hashtable]$extra = @{}) {
	$dir = Join-Path $Out $name
	Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
	$envs = @{ CTR_DEV_RACE = $track; CTR_DEV_MODE = 'tt'; CTR_DEV_AUTOPILOT = 1; CTR_DEV_EXIT_AFTER_RACE = 1
		CTR_DEV_TRACE = "$dir\trace.csv"; CTR_DEV_GHOST_OUT = "$dir\ghost.bin" }
	foreach ($k in $extra.Keys) { $envs[$k] = $extra[$k] }
	# Generous frame cap: a stuck autopilot ends here instead of hanging.
	& "$PSScriptRoot\devrun.ps1" -Exe $Exe -RunDir (Join-Path $Out 'run') -Track -1 -Frames 40000 -From 1000000 -Out $dir `
		-Config @{ fps60 = $fps60 } -Env $envs -TimeoutSec 600 | Out-Null
	return "$dir\trace.csv"
}

foreach ($t in $Tracks) {
	$a = Run "$t-30" $t 'false'
	$b = Run "$t-60" $t 'true'
	Write-Output ("track {0,2}: {1}" -f $t, (python -I $trace parity $a $b))
	if ($Ghosts -and (Test-Path "$Out\$t-30\ghost.bin")) {
		$g = "$Out\$t-ghost30.bin"
		Copy-Item "$Out\$t-30\ghost.bin" $g -Force
		$p30 = Run "$t-g30" $t 'false' @{ CTR_DEV_GHOST_IN = $g }
		$p60 = Run "$t-g60" $t 'true' @{ CTR_DEV_GHOST_IN = $g }
		Write-Output ("          ghost at 30: {0}" -f (python -I $trace ghost $a $p30))
		Write-Output ("          ghost at 60: {0}" -f (python -I $trace ghost $a $p60))
	}
}
