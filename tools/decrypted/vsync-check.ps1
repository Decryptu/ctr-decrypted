<#
.SYNOPSIS
  Measure frame pacing on this machine's display, for every VSync option at
  30 and 60 FPS, windowed and fullscreen.

.DESCRIPTION
  Each run shows the game window in real time (the swap only waits for the
  display when the window is visible) and plays the attract demo with
  --perf-dir. perf_pacing.py then reports, per run, how many display refreshes
  each frame stayed up and how many frames were uneven or long.

  Nobody needs to be at the machine, but the screen must stay on and unlocked
  for the whole run (about two minutes per run by default), and nothing else
  should be in front of the window. The display's current refresh rate is
  read from Windows and printed.

.EXAMPLE
  tools\decrypted\vsync-check.ps1
  tools\decrypted\vsync-check.ps1 -Seconds 30 -VSync 1 -Fps 60
#>
param(
	[int]$Seconds = 60,                    # race time measured per run
	[int]$Track = 3,                       # attract demo LevelID (3 = Crash Cove)
	[int[]]$VSync = @(0, 1, 2),             # Off, On, Adaptive
	[int[]]$Fps = @(30, 60),
	[switch]$WindowedOnly,
	[string]$Out = "$env:TEMP\ctrdev\vsync",
	[string]$Exe = "$PSScriptRoot\..\..\build-vanilla\ctr_native.exe"
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null
$hz = (Get-CimInstance Win32_VideoController | Where-Object CurrentRefreshRate -gt 0 | Select-Object -First 1).CurrentRefreshRate
Write-Output "display: $hz Hz"
$report = Join-Path $PSScriptRoot 'perf_pacing.py'
$modes = if ($WindowedOnly) { @('false') } else { @('false', 'true') }

foreach ($full in $modes) {
	foreach ($f in $Fps) {
		foreach ($v in $VSync) {
			$name = "{0}fps-vsync{1}-{2}" -f $f, $v, $(if ($full -eq 'true') { 'full' } else { 'window' })
			$dir = Join-Path $Out $name
			Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
			# The demo race starts about 1500 frames in at 60 FPS, 750 at 30.
			$frames = ($f / 30) * 800 + $Seconds * $f
			& "$PSScriptRoot\devrun.ps1" -Exe $Exe -RunDir (Join-Path $Out 'run') -Track $Track -Frames $frames -From 100000000 -Out $dir `
				-Visible -RealTime -Config @{ fps60 = $(if ($f -eq 60) { 'true' } else { 'false' }); vsync = $v; fullscreen = $full } `
				-GameArgs '--perf-dir', "$dir\perf" -TimeoutSec ($Seconds + 120) | Out-Null
			$csv = Get-ChildItem "$dir\perf" -Recurse -Filter frame_times.csv -ErrorAction SilentlyContinue | Select-Object -First 1
			$line = if ($csv) { python -I $report $csv.FullName $hz --level $Track } else { 'no perf output' }
			Write-Output ("{0,-24} {1}" -f $name, $line)
		}
	}
}
