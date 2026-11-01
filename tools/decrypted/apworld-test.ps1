# Runs the CTR world's pytest suite the way .github/workflows/apworld.yml does: inside an
# Archipelago 0.6.7 source checkout, with apworld/ctr copied to worlds/ctr.
#
#   pwsh tools/decrypted/apworld-test.ps1                    # the whole suite
#   pwsh tools/decrypted/apworld-test.ps1 -k starting_roster # extra arguments go to pytest
#
# The first run clones Archipelago and installs its Python packages into a virtualenv
# (Python 3.12, from the py launcher) under %LOCALAPPDATA%\ctr-decrypted\archipelago.
# -Reinstall starts that over.
param(
    [switch]$Reinstall,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$PytestArgs
)
$ErrorActionPreference = "Stop"
$apRef = "debe4cf035c7c15efe6fb95f72343af0d420c68c"   # Archipelago 0.6.7, as in apworld.yml
$repo = (git rev-parse --show-toplevel).Trim()
$ap = Join-Path $env:LOCALAPPDATA "ctr-decrypted\archipelago"
$python = Join-Path $ap ".venv\Scripts\python.exe"
$marker = Join-Path $ap ".ctrd-ready"

function Run([string]$exe, [string[]]$argv) {
    & $exe @argv
    if ($LASTEXITCODE) { throw "$exe $($argv -join ' ') failed ($LASTEXITCODE)" }
}

if ($Reinstall -and (Test-Path $ap)) { Remove-Item -Recurse -Force $ap }
if (-not (Test-Path $marker) -or (Get-Content $marker) -ne $apRef) {
    if (Test-Path $ap) { Remove-Item -Recurse -Force $ap }
    New-Item -ItemType Directory -Force $ap | Out-Null
    Run git @("-C", $ap, "init", "-q")
    Run git @("-C", $ap, "fetch", "-q", "--depth", "1", "https://github.com/ArchipelagoMW/Archipelago.git", $apRef)
    Run git @("-C", $ap, "-c", "advice.detachedHead=false", "checkout", "-q", "FETCH_HEAD")
    Run py @("-3.12", "-m", "venv", (Join-Path $ap ".venv"))
    Push-Location $ap
    try {
        Run $python @("-m", "pip", "install", "--disable-pip-version-check", "-r", "ci-requirements.txt")
        Run $python @("ModuleUpdate.py", "--yes", "--force", "--append", "WebHostLib/requirements.txt")
        $env:SKIP_REQUIREMENTS_UPDATE = "1"
        Run $python @("Launcher.py", "--update_settings")
    } finally { Pop-Location }
    Set-Content -NoNewline $marker $apRef
}

$world = Join-Path $ap "worlds\ctr"
if (Test-Path $world) { Remove-Item -Recurse -Force $world }
Copy-Item -Recurse (Join-Path $repo "apworld\ctr") $world
Get-ChildItem $world -Recurse -Directory -Filter __pycache__ | Remove-Item -Recurse -Force

Push-Location $ap
try {
    $env:SKIP_REQUIREMENTS_UPDATE = "1"
    & $python -m pytest worlds/ctr -n auto -q @PytestArgs
    exit $LASTEXITCODE
} finally { Pop-Location }
