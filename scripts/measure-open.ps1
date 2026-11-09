# Abre cada vídeo SEM limpar o resultado guardado e mostra os tempos (abertura com a análise já feita).
# Uso: powershell -NoProfile -File scripts\measure-open.ps1 "<video1.mp4>" ...   (feche o shear antes)
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Videos)
$exe = Join-Path (Split-Path $PSScriptRoot -Parent) "build\src\app\shear.exe"
$log = "$env:LOCALAPPDATA\Shear\Shear\metrics.log"
foreach ($v in $Videos) {
    Remove-Item $log -ErrorAction SilentlyContinue
    $p = Start-Process $exe -ArgumentList "`"$v`"" -PassThru
    Start-Sleep 6
    Stop-Process $p -Force
    "== $v"
    Get-Content $log -Encoding UTF8 -ErrorAction SilentlyContinue |
        Select-String "corrompido|primeiro frame|análise|keyframes lidos" | ForEach-Object { $_.Line.Substring(21) }
}
