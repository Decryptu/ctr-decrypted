# Retakes the README screenshots into docs/images: N. Oxide racing, the Archipelago item
# feed in a race, and the Mods screen's track browser. All at 1920x1080, 16:9, PGXP on.
#
#   pwsh tools/decrypted/readme-shots.ps1                 # all three
#   pwsh tools/decrypted/readme-shots.ps1 -Only oxide     # oxide, archipelago or mods
#
# The Oxide shot is a fast-forwarded run, so it is the same frame every time. The other
# two run in real time: the Archipelago one needs a server and a second slot sending
# items while the race runs, and the Mods one loads the Project Saphi catalogue over the
# network. Their frame can land a little differently from run to run; the run folders
# under C:\t\shots keep every captured frame, so pick another with -ApFrame / -ModsFrame.
#
# The Archipelago shot generates a throwaway two-player seed (Decrypt and Friend, with
# item boxes as checks) in the Archipelago source checkout that apworld-test.ps1 sets up,
# with this repository's apworld, and hosts it on 127.0.0.1:38300.
param(
    [ValidateSet('oxide', 'archipelago', 'mods')][string[]]$Only = @('oxide', 'archipelago', 'mods'),
    [string]$Exe = 'build-ap\ctr_native_ap.exe',
    [int]$OxideFrame = 2380,
    [int]$ApFrame = 2900,
    [int]$ModsFrame = 830
)
$ErrorActionPreference = 'Stop'
$repo = (git rev-parse --show-toplevel).Trim()
$devrun = Join-Path $repo 'tools\decrypted\devrun.ps1'
$images = Join-Path $repo 'docs\images'
$work = 'C:\t\shots'
New-Item -ItemType Directory -Force $images, $work | Out-Null
$video = @{aspect_ratio = 1; increase_draw_distance = 1; pgxp = 2; render_scale = 0
    mute_when_unfocused = 'true'; 'State.window_w' = 1920; 'State.window_h' = 1080}

function Shot([string]$name, [int]$frame, [hashtable]$config, [hashtable]$env, [int]$from, [int]$every, [switch]$RealTime) {
    $rd = Join-Path $work "run-$name"; $out = Join-Path $work $name
    Remove-Item -Recurse -Force $rd, $out -ErrorAction SilentlyContinue
    $env = $env + @{CTR_DEV_SHOT_PRESENTED = 1}
    & $devrun -Track -1 -Frames ($frame + 200) -From $from -Every $every -Exe (Join-Path $repo $Exe) -RunDir $rd -Out $out -Config ($video + $config) -Env $env -RealTime:$RealTime | Out-Null
    $png = Join-Path $out ('f{0:D6}.png' -f $frame)
    if (-not (Test-Path $png)) { throw "$name`: frame $frame was not captured (see $out)" }
    $jpg = Join-Path $images "readme-$name.jpg"
    python -c "from PIL import Image; Image.open(r'$png').convert('RGB').save(r'$jpg', quality=90, optimize=True)"
    "{0}: {1} ({2:N0} KB)" -f $name, $jpg, ((Get-Item $jpg).Length / 1KB)
}

if ($Only -contains 'oxide') {
    # Hot Air Skyway, player 1 as N. Oxide, early in the first lap.
    Shot 'oxide' $OxideFrame @{} @{CTR_DEV_RACE = 7; CTR_DEV_AUTOPILOT = 1; CTR_DEV_CHARACTER = 15} $OxideFrame 1
}

if ($Only -contains 'archipelago') {
    $ap = Join-Path $env:LOCALAPPDATA 'ctr-decrypted\archipelago'
    $python = Join-Path $ap '.venv\Scripts\python.exe'
    if (-not (Test-Path $python)) { throw "No Archipelago checkout at ${ap}: run tools/decrypted/apworld-test.ps1 once first" }
    $seedDir = Join-Path $work 'seed'
    Remove-Item -Recurse -Force $seedDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force "$seedDir\players" | Out-Null
    foreach ($slot in 'Decrypt', 'Friend') {
        "name: $slot`ngame: Crash Team Racing`nCrash Team Racing:`n  box_locations: 'true'`n" | Set-Content -Encoding utf8 "$seedDir\players\$slot.yaml"
    }
    Remove-Item -Recurse -Force (Join-Path $ap 'worlds\ctr') -ErrorAction SilentlyContinue
    Copy-Item -Recurse (Join-Path $repo 'apworld\ctr') (Join-Path $ap 'worlds\ctr')
    $env:SKIP_REQUIREMENTS_UPDATE = '1'
    Push-Location $ap
    try {
        & $python Generate.py --player_files_path "$seedDir\players" --outputpath "$seedDir\out" | Out-Null
        $zip = (Get-ChildItem "$seedDir\out" -Filter AP_*.zip | Select-Object -First 1).FullName
        if (-not $zip) { throw 'seed generation failed' }
        $server = Start-Process $python -ArgumentList 'MultiServer.py', '--host', '127.0.0.1', '--port', '38300', $zip -RedirectStandardOutput "$seedDir\server.log" -RedirectStandardError "$seedDir\server.err" -WindowStyle Hidden -PassThru
    } finally { Pop-Location }
    try {
        Start-Sleep 8
        # Friend's finds for Decrypt land 40 s after this starts, one every 3 s: early in
        # the first lap of the real-time race below.
        $feed = Start-Process python -ArgumentList (Join-Path $repo 'tools\decrypted\ap_feed_demo.py'), '38300', 'Friend', '40', '6', '3' -RedirectStandardOutput "$seedDir\feed.log" -RedirectStandardError "$seedDir\feed.err" -WindowStyle Hidden -PassThru
        # Crash Cove as an Adventure trophy race.
        Shot 'archipelago' $ApFrame @{'Connection.uri' = 'ws://localhost:38300'; 'Connection.slot' = 'Decrypt'} @{CTR_DEV_RACE = 3; CTR_DEV_MODE = 'adv'; CTR_DEV_AUTOPILOT = 1} 2000 15 -RealTime
        $feed.WaitForExit(30000) | Out-Null
    } finally {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
}

if ($Only -contains 'mods') {
    # The Mods screen's Tracks tab, two entries down. Skipping the intros only here: it
    # moves every later frame.
    Shot 'mods' $ModsFrame @{skip_intro = 'true'} @{CTR_DEV_MODS = 1; CTR_DEV_PAD = '680:D,760:D'} 600 10 -RealTime
}
