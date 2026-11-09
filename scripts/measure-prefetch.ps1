# Testa a análise adiantada em segundo plano: abre o shear na tela de clipes, espera a análise dos clipes recentes e
# depois abre um vídeo (repassado à janela aberta). Mostra quanto demorou o primeiro quadro.
# Uso: powershell -NoProfile -File scripts\measure-prefetch.ps1 "<video.mp4>" [segundos de espera, padrão 25]
# Feche o shear antes. O vídeo precisa estar entre os 8 clipes mais recentes da pasta do ShadowPlay.
param([string]$Video, [int]$Wait = 25)
$exe = Join-Path (Split-Path $PSScriptRoot -Parent) "build\src\app\shear.exe"
$log = "$env:LOCALAPPDATA\Shear\Shear\metrics.log"
Remove-Item "HKCU:\Software\Shear\Shear\damage" -Recurse -ErrorAction SilentlyContinue
Remove-Item $log -ErrorAction SilentlyContinue
$main = Start-Process $exe -PassThru
"esperando $Wait s a análise em segundo plano…"
Start-Sleep $Wait
"resultados guardados: " + @(Get-ItemProperty "HKCU:\Software\Shear\Shear\damage" -ErrorAction SilentlyContinue |
    Get-Member -MemberType NoteProperty | Where-Object { $_.Name -notlike 'PS*' }).Count
Remove-Item $log -ErrorAction SilentlyContinue
Start-Process $exe -ArgumentList "`"$Video`"" | Out-Null   # repassa o vídeo para a janela aberta
Start-Sleep 6
Stop-Process $main -Force
"== $Video"
Get-Content $log -Encoding UTF8 -ErrorAction SilentlyContinue |
    Select-String "corrompido|primeiro frame|análise" | ForEach-Object { $_.Line.Substring(21) }
