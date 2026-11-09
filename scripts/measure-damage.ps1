# Mede a análise do começo corrompido: abre cada vídeo (limpando o resultado guardado) e mostra os tempos do metrics.log.
# Uso: powershell -NoProfile -File scripts\measure-damage.ps1 "<video1.mp4>" "<video2.mp4>" ...
# Feche o shear antes (um segundo shear só repassa o vídeo para a janela que já está aberta).
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Videos)
$exe = Join-Path (Split-Path $PSScriptRoot -Parent) "build\src\app\shear.exe"
$log = "$env:LOCALAPPDATA\Shear\Shear\metrics.log"
foreach ($v in $Videos) {
    Remove-Item "HKCU:\Software\Shear\Shear\damage" -Recurse -ErrorAction SilentlyContinue
    Remove-Item $log -ErrorAction SilentlyContinue
    $p = Start-Process $exe -ArgumentList "`"$v`"" -PassThru
    Start-Sleep 6
    Stop-Process $p -Force
    "== $v"
    Get-Content $log -Encoding UTF8 -ErrorAction SilentlyContinue |
        Select-String "corrompido|primeiro frame|análise" | ForEach-Object { $_.Line.Substring(21) }
}
