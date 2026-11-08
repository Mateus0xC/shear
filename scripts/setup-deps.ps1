# Baixa libmpv (dev) e ffmpeg (GPL, shared) para third_party/. Idempotente.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$tp = Join-Path $root 'third_party'
New-Item -ItemType Directory -Force $tp | Out-Null

function Get-Asset($repo, $pattern) {
    $r = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest"
    $a = $r.assets | Where-Object { $_.name -match $pattern } | Select-Object -First 1
    if (-not $a) { throw "Asset $pattern não encontrado em $repo" }
    $a
}

# --- ffmpeg (exe + dll + headers + libs) ---
$ff = Join-Path $tp 'ffmpeg'
if (-not (Test-Path "$ff\bin\ffmpeg.exe")) {
    $a = Get-Asset 'BtbN/FFmpeg-Builds' '^ffmpeg-n8\.1-latest-win64-gpl-shared-8\.1\.zip$'
    $zip = Join-Path $tp $a.name
    Invoke-WebRequest $a.browser_download_url -OutFile $zip
    $tmp = Join-Path $tp 'ffmpeg_tmp'
    Expand-Archive $zip $tmp -Force
    Move-Item (Get-ChildItem $tmp | Select-Object -First 1).FullName $ff -Force
    Remove-Item $tmp, $zip -Recurse -Force
}

# --- libmpv ---
$mpv = Join-Path $tp 'mpv'
if (-not (Test-Path "$mpv\libmpv-2.dll")) {
    $a = Get-Asset 'zhongfly/mpv-winbuild' '^mpv-dev-lgpl-x86_64-2.*\.7z$'
    $z = Join-Path $tp $a.name
    Invoke-WebRequest $a.browser_download_url -OutFile $z
    New-Item -ItemType Directory -Force $mpv | Out-Null
    tar -xf $z -C $mpv
    Remove-Item $z -Force
}

# --- gera mpv.lib (MSVC) a partir da DLL ---
if (-not (Test-Path "$mpv\mpv.lib")) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -property installationPath
    $libexe = Get-ChildItem "$vs\VC\Tools\MSVC" -Recurse -Filter lib.exe | Where-Object FullName -match 'Hostx64\\x64' | Select-Object -First 1
    $dumpbin = Join-Path $libexe.DirectoryName 'dumpbin.exe'
    $names = & $dumpbin /exports "$mpv\libmpv-2.dll" | ForEach-Object {
        if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)$') { $Matches[1] } }
    @('LIBRARY libmpv-2.dll', 'EXPORTS') + $names | Set-Content "$mpv\mpv.def" -Encoding ascii
    & $libexe.FullName /nologo /def:"$mpv\mpv.def" /out:"$mpv\mpv.lib" /machine:x64
}
Write-Host "Dependências prontas em $tp"
