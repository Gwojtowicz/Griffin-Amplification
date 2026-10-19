# Applies Griffin Amplification's library patches to the pinned submodules.
# Safe to run more than once: a patch that is already applied is skipped.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
git -C $root submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { Write-Error "git submodule update failed"; exit 1 }
foreach ($lib in 'iPlug2', 'NeuralAmpModelerCore', 'AudioDSPTools') {
  $patch = Join-Path $root "patches\$lib.patch"
  $dir = Join-Path $root $lib
  $null = cmd /c "git -C `"$dir`" apply --reverse --check --whitespace=nowarn `"$patch`" 2>nul"
  if ($LASTEXITCODE -eq 0) { Write-Host "$lib : already applied"; continue }
  cmd /c "git -C `"$dir`" apply --whitespace=nowarn `"$patch`""
  if ($LASTEXITCODE -ne 0) { Write-Error "$lib : patch does not apply. Is the submodule at the pinned commit? (git submodule status)"; exit 1 }
  Write-Host "$lib : applied"
}
