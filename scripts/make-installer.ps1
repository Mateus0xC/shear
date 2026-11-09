# Gera dist\shear-setup.exe (precisa do Inno Setup: winget install JRSoftware.InnoSetup).
# Compile antes: cmake --build build
$root = Split-Path $PSScriptRoot -Parent
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup não encontrado (winget install JRSoftware.InnoSetup)." }
if (-not (Test-Path "$root\build\src\app\shear.exe")) { throw "Compile antes: cmake --build build" }
# DLLs do runtime do Visual C++ (opcional: se não achar, o instalador sai sem elas)
$crt = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
       Sort-Object FullName | Select-Object -Last 1
$defs = @()
if ($crt) { $defs += "/DVcRedist=$($crt.FullName)" } else { Write-Warning "Runtime do Visual C++ não encontrado: sairá sem ele." }
& $iscc @defs "$root\installer\shear.iss"
if ($LASTEXITCODE -eq 0) { Get-Item "$root\dist\shear-setup.exe" | Select-Object Name, @{n='MB';e={[math]::Round($_.Length/1MB,1)}} }
