# Imports an upstream branch into refs/remotes/clean/<name>, with AI co-author trailers stripped
# from commit messages. The rewrite is deterministic, so running it again later produces the same
# hashes for commits already imported and a normal `git merge clean/<name>` keeps working.
#
#   pwsh tools/decrypted/sync-upstream.ps1                       # dowlle main (the default)
#   pwsh tools/decrypted/sync-upstream.ps1 -Remote ctr-native -Branch master
#   git merge clean/dowlle-main
#
# -Path keeps only one folder of the upstream repo and -Prefix moves it, so the CTR world, which
# lives in an Archipelago fork, lands in our apworld/ folder with its own history:
#   pwsh tools/decrypted/sync-upstream.ps1 -Remote apworld -Branch main -Path worlds/ctr -Prefix apworld/ctr
#
# Needs git-filter-repo: python -m pip install git-filter-repo
param(
    [string]$Remote = "dowlle",
    [string]$Branch = "main",
    [string]$Path = "",
    [string]$Prefix = ""
)
$ErrorActionPreference = "Stop"
$repo = (git rev-parse --show-toplevel).Trim()
$url = (git -C $repo remote get-url $Remote).Trim()
$work = Join-Path ([IO.Path]::GetTempPath()) "ctrd-sync-$Remote-$Branch"

if (Test-Path $work) { Remove-Item -Recurse -Force $work }
git clone --bare --single-branch --branch $Branch --no-tags $url $work
if ($LASTEXITCODE) { throw "clone failed" }

$callback = @'
import re
pat = re.compile(rb'(?im)^[ \t]*Co-Authored-By:[^\n]*(claude|anthropic|openai|copilot|chatgpt|gemini)[^\n]*\n?')
if pat.search(message):
    message = pat.sub(b'', message).rstrip() + b'\n'
return message
'@
$pathArgs = @()
if ($Path) { $pathArgs += @("--path", $Path) }
if ($Path -and $Prefix) { $pathArgs += @("--path-rename", "${Path}:$Prefix") }
python -m git_filter_repo --source $work --target $work --force --quiet --message-callback $callback @pathArgs
if ($LASTEXITCODE) { throw "filter-repo failed" }

git -C $repo fetch --no-tags $work "+refs/heads/${Branch}:refs/remotes/clean/$Remote-$Branch"
if ($LASTEXITCODE) { throw "fetch failed" }
Remove-Item -Recurse -Force $work
Write-Host "Updated clean/$Remote-$Branch -> $((git -C $repo rev-parse --short "clean/$Remote-$Branch").Trim())"
