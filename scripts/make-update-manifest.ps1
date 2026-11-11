# Gera o manifesto de atualização do shear (updates\manifest.json) e separa os arquivos que precisam ser publicados.
# Uso: powershell -File scripts\make-update-manifest.ps1 [-Notes "o que mudou"]      (compile antes: cmake --build build)
#
# O manifesto lista TODOS os arquivos do shear com SHA-256 e tamanho. O shear baixa só os que mudaram em relação ao que o usuário
# já tem. Aqui, arquivos iguais aos do manifesto anterior reaproveitam a URL antiga: só o que mudou vai para a nova Release
# (v<versão>), em dist\update\v<versão>\ (nomes sem "/": platforms\qwindows.dll vira platforms-qwindows.dll).
param(
    [string]$AppDir = "",
    [string]$Version = "",
    [string]$Notes = "",
    [string]$BaseUrl = "",
    [string]$ManifestPath = "",
    [string]$OutDir = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $AppDir) { $AppDir = Join-Path $root "build\src\app" }
if (-not $Version) {
    $cm = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
    if ($cm -notmatch 'project\(Shear VERSION ([0-9.]+)') { throw "Não achei a versão em CMakeLists.txt" }
    $Version = $Matches[1]
}
if (-not $BaseUrl) { $BaseUrl = "https://github.com/Mateus0xC/shear/releases/download/v$Version" }
if (-not $ManifestPath) { $ManifestPath = Join-Path $root "updates\manifest.json" }
if (-not $OutDir) { $OutDir = Join-Path $root "dist\update\v$Version" }
if (-not (Test-Path "$AppDir\shear.exe")) { throw "shear.exe não encontrado em $AppDir. Compile antes: cmake --build build" }

# o mesmo que o instalador deixa de fora (installer\shear.iss)
$skipDirs = 'CMakeFiles', 'shear_autogen', 'plugins', 'tls', '.update'
$skipNames = 'CTestTestfile.cmake', 'ffplay.exe', 'ffprobe.exe', 'vc_redist.x64.exe', 'dxcompiler.dll', 'dxil.dll'
$skipExt = '.cmake', '.ilk', '.pdb', '.lib', '.exp', '.old'
$appFull = (Resolve-Path $AppDir).Path.TrimEnd('\')
$files = Get-ChildItem $AppDir -Recurse -File | Where-Object {
    $rel = $_.FullName.Substring($appFull.Length + 1)
    $top = ($rel -split '\\')[0]
    ($skipDirs -notcontains $top) -and ($skipNames -notcontains $_.Name) -and ($skipExt -notcontains $_.Extension.ToLower()) -and
        -not ($rel -split '\\' | Where-Object { $skipDirs -contains $_ })
}

$old = @{}
if (Test-Path $ManifestPath) {
    $om = Get-Content $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($f in $om.files) { $old[$f.path] = $f }
}
Remove-Item $OutDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $OutDir | Out-Null

$entries = @(); $upload = 0; $uploadBytes = 0
foreach ($f in $files) {
    $rel = ($f.FullName.Substring($appFull.Length + 1)) -replace '\\', '/'
    $hash = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
    $o = $old[$rel]
    if ($o -and $o.sha256 -eq $hash) {
        $url = $o.url                      # igual ao que já está publicado
    } else {
        $flat = $rel -replace '/', '-'
        Copy-Item $f.FullName (Join-Path $OutDir $flat) -Force
        $url = "$BaseUrl/$flat"
        $upload++; $uploadBytes += $f.Length
    }
    $entries += [ordered]@{ path = $rel; url = $url; sha256 = $hash; size = $f.Length }
}
New-Item -ItemType Directory -Force (Split-Path $ManifestPath) | Out-Null
[ordered]@{ version = $Version; notes = $Notes; files = $entries } | ConvertTo-Json -Depth 6 | Set-Content $ManifestPath -Encoding UTF8

Write-Host ("Manifesto da versão {0}: {1} arquivos; {2} novos/alterados ({3:N1} MB) para publicar." -f $Version, $entries.Count, $upload, ($uploadBytes / 1MB))
Write-Host "Manifesto: $ManifestPath"
if ($upload -gt 0) { Write-Host "Publique o conteúdo de $OutDir como assets da Release 'v$Version' e faça commit do manifesto (updates\manifest.json)." }
else { Write-Host "Nenhum arquivo mudou em relação ao manifesto anterior." }
