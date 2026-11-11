# Empacota um plugin do shear para publicar numa Release do GitHub e atualiza o catálogo (plugins\catalog.json).
# Uso: powershell -File scripts\make-plugin-package.ps1 [-Id valorant]      (compile antes: cmake --build build)
# Saída: dist\plugins\<id>\ (os arquivos que vão como "assets" da Release plugin-<id>-<versão>) e plugins\catalog.json.
# O catálogo leva o SHA-256 de cada arquivo: o shear confere o fingerprint do que baixou antes de instalar.
param([string]$Id = "valorant")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$app = Join-Path $root "build\src\app"
$src = Join-Path $app "plugins\$Id"
if (-not (Test-Path "$src\plugin.json")) { throw "Plugin '$Id' não encontrado em $src. Compile antes: cmake --build build" }
$meta = Get-Content "$src\plugin.json" -Raw -Encoding UTF8 | ConvertFrom-Json
$version = $meta.version
$out = Join-Path $root "dist\plugins\$Id"
Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$out\tls" | Out-Null

# arquivos do plugin: (caminho dentro da pasta do plugin) -> (arquivo de origem)
$files = [ordered]@{
    $meta.library = "$src\$($meta.library)"
    "plugin.json" = "$src\plugin.json"
}
if ($Id -eq "valorant") { $files["tls/qschannelbackend.dll"] = "$app\tls\qschannelbackend.dll" } # HTTPS do Qt (o shear base não leva o tls)
foreach ($k in $files.Keys) {
    $dest = Join-Path $out ($k -replace '/', '\')
    New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
    Copy-Item $files[$k] $dest -Force
}

# a Release se chama plugin-<id>-<versão>; assets não têm "/" no nome: tls/x.dll vira tls-x.dll
$base = "https://github.com/Mateus0xC/shear/releases/download/plugin-$Id-$version"
$entries = @()
foreach ($k in $files.Keys) {
    $path = Join-Path $out ($k -replace '/', '\')
    $entries += [ordered]@{ path = $k; url = "$base/$($k -replace '/', '-')"; sha256 = (Get-FileHash $path -Algorithm SHA256).Hash.ToLower() }
}
$ver = [ordered]@{ version = $version; apiMajor = $meta.apiMajor; apiMinor = $meta.apiMinor; minHostApiMinor = $meta.minHostApiMinor; files = $entries }

$catPath = Join-Path $root "plugins\catalog.json"
New-Item -ItemType Directory -Force (Split-Path $catPath) | Out-Null
$catalog = if (Test-Path $catPath) { Get-Content $catPath -Raw -Encoding UTF8 | ConvertFrom-Json } else { [pscustomobject]@{ plugins = @() } }
$plugins = @($catalog.plugins | Where-Object { $_.id -ne $Id })
$old = @($catalog.plugins | Where-Object { $_.id -eq $Id } | ForEach-Object { $_.versions } | Where-Object { $_ -and $_.version -ne $version })
$plugins += [ordered]@{ id = $Id; versions = @($old + $ver) }
[pscustomobject]@{ plugins = $plugins } | ConvertTo-Json -Depth 8 | Set-Content $catPath -Encoding UTF8

Write-Host "Plugin $Id $version empacotado em $out"
Get-ChildItem $out -Recurse -File | ForEach-Object { "{0,8:N0} KB  {1}" -f ($_.Length / 1KB), $_.FullName.Substring($out.Length + 1) }
Write-Host "Catálogo: $catPath"
Write-Host "Publique os arquivos de $out como assets da Release 'plugin-$Id-$version' (tls\qschannelbackend.dll com o nome tls-qschannelbackend.dll)."
