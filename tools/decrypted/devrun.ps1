<#
.SYNOPSIS
  Run the game unattended (hidden window, fast clock) and capture frames.
  Built for automated debugging: no window, no input, no waiting in real time.

.EXAMPLE
  tools\decrypted\devrun.ps1 -Track 0 -Frames 3000 -Every 150 -Env @{CTR_PGXP_DEBUG=1}
  Captures Dingo Canyon's attract demo every 150 frames into a fresh folder
  and prints the folder path. Track IDs: include/namespace_Level.h.

.NOTES
  See docs/DEV-TOOLS.md for every switch.
#>
param(
	[int]$Track = 3,                 # LevelID for the attract demo (3 = Crash Cove), -1 = no demo
	[int]$Frames = 2400,             # quit after this many presented frames
	[int]$Every = 120,               # capture every N frames
	[int]$From = 0,                  # first captured frame
	[string]$Exe = "$PSScriptRoot\..\..\build-vanilla\ctr_native.exe",
	[string]$Bin = "C:\Users\gurva\Games\CTR\_dump\CTR - Crash Team Racing (USA).bin",
	[string]$Out = "",               # capture folder (default: new folder under %TEMP%\ctrdev)
	[hashtable]$Config = @{},        # config.ini keys to override, e.g. @{pgxp=0; render_scale=2}
	[hashtable]$Env = @{},           # extra environment variables, e.g. @{CTR_PGXP_DEBUG=1}
	[switch]$Visible,                # show the window instead of hiding it
	[switch]$RealTime,               # keep wall-clock pacing
	[int]$TimeoutSec = 300
)
$ErrorActionPreference = 'Stop'

$exePath = (Resolve-Path $Exe).Path
$root = Join-Path $env:TEMP 'ctrdev'
$run = Join-Path $root 'run'
New-Item -ItemType Directory -Force $run | Out-Null
if (-not $Out) { $Out = Join-Path $root ("shots-" + (Get-Date -Format 'HHmmss')) }
New-Item -ItemType Directory -Force $Out | Out-Null

# A run folder of its own, so the owner's install and saves are never touched.
Copy-Item $exePath (Join-Path $run 'ctr_native.exe') -Force
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$assets = Join-Path $run 'assets'
if (-not (Test-Path $assets)) { Copy-Item (Join-Path $repo 'assets') $assets -Recurse }
$disc = Join-Path $assets 'ctr-u.bin'
if (-not (Test-Path $disc)) { Copy-Item $Bin $disc }

# Fixed baseline config, then the caller's overrides.
$cfg = [ordered]@{ fps60 = 'true'; pgxp = 2; aspect_ratio = 1; render_scale = 0 }
foreach ($k in $Config.Keys) { $cfg[$k] = $Config[$k] }
$lines = @('[Video & QoL]') + ($cfg.Keys | ForEach-Object { "$_ = $($cfg[$_])" })
Set-Content (Join-Path $run 'config.ini') $lines

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = Join-Path $run 'ctr_native.exe'
$psi.WorkingDirectory = $run
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true   # the game log goes to run\*.log, keep the pipeline clean
$psi.RedirectStandardError = $true
$vars = @{
	CTR_DEV_DEMO = $Track; CTR_DEV_EXIT_FRAME = $Frames
	CTR_DEV_SHOT_DIR = $Out; CTR_DEV_SHOT_EVERY = $Every; CTR_DEV_SHOT_FROM = $From
	CTR_DEV_HIDDEN = [int](-not $Visible); CTR_DEV_FAST = [int](-not $RealTime)
}
foreach ($k in $Env.Keys) { $vars[$k] = $Env[$k] }
foreach ($k in $vars.Keys) { $psi.EnvironmentVariables[$k] = "$($vars[$k])" }

$p = [System.Diagnostics.Process]::Start($psi)
$null = $p.StandardOutput.ReadToEndAsync(); $null = $p.StandardError.ReadToEndAsync()
if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); Write-Warning "timed out after $TimeoutSec s" }

# Convert to PNG (smaller, and what image viewers and agents read best).
Add-Type -AssemblyName System.Drawing
Get-ChildItem $Out -Filter *.bmp | ForEach-Object {
	$img = [System.Drawing.Image]::FromFile($_.FullName)
	$img.Save([IO.Path]::ChangeExtension($_.FullName, '.png'), [System.Drawing.Imaging.ImageFormat]::Png)
	$img.Dispose(); Remove-Item $_.FullName
}

# Contact sheet of every capture (labelled by frame), for a one-image overview.
$shots = @(Get-ChildItem $Out -Filter 'f*.png' | Sort-Object Name)
if ($shots.Count -gt 0) {
	$cols = [Math]::Min(5, $shots.Count); $rows = [Math]::Ceiling($shots.Count / $cols)
	$first = [System.Drawing.Image]::FromFile($shots[0].FullName)
	$tw = 384; $th = [int]($tw * $first.Height / $first.Width); $first.Dispose()
	$sheet = New-Object System.Drawing.Bitmap ($tw * $cols), ($th * $rows)
	$g = [System.Drawing.Graphics]::FromImage($sheet)
	$font = New-Object System.Drawing.Font 'Consolas', 10
	for ($i = 0; $i -lt $shots.Count; $i++) {
		$img = [System.Drawing.Image]::FromFile($shots[$i].FullName)
		$x = ($i % $cols) * $tw; $y = [Math]::Floor($i / $cols) * $th
		$g.DrawImage($img, $x, $y, $tw, $th); $img.Dispose()
		$g.FillRectangle([System.Drawing.Brushes]::Black, $x, $y, 70, 16)
		$g.DrawString($shots[$i].BaseName, $font, [System.Drawing.Brushes]::White, $x + 2, $y + 1)
	}
	$g.Dispose(); $sheet.Save((Join-Path $Out 'sheet.png')); $sheet.Dispose()
}
$Out
