# Desenvolvimento

← [Voltar ao início](index.md)

## Compilar

Requisitos: Windows 11 x64, Visual Studio 2022 Build Tools (carga de trabalho C++), CMake, Ninja e Python (para o aqtinstall).

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools Kitware.CMake Ninja-build.Ninja
pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O D:\Qt      # Qt 6.8.3 pré-compilado
powershell -File scripts\setup-deps.ps1      # baixa libmpv e ffmpeg (GPL, shared) em third_party/
```

Em um prompt “x64 Native Tools” (ou depois de `vcvars64.bat`):

```
cmake --preset release
cmake --build build
ctest --test-dir build        # testes unitários (Qt Test)
```

Se o Qt estiver em outro lugar, ajuste `CMAKE_PREFIX_PATH` em `CMakePresets.json`. O build copia Qt, `libmpv-2.dll`, as DLLs do ffmpeg
e o `ffmpeg.exe` para `build\src\app\` (com os plugins em `build\src\app\plugins\`); essa pasta roda sozinha.

Testes: `core`, `valorant`, `plugins`, `updater` e `navkeys`. Os que usam rede ou arquivos trabalham com `file://` e pastas temporárias.
Variáveis para testar sem rede: `SHEAR_PLUGINS_DIR`, `SHEAR_PLUGIN_CATALOG` e `SHEAR_UPDATE_MANIFEST`.

## Instalador

`winget install JRSoftware.InnoSetup` (uma vez) e, com o projeto compilado, `scripts\make-installer.ps1` gera `dist\shear-setup.exe`
(~110 MB; já inclui o runtime do Visual C++). Instala só para o usuário (sem administrador) e **não leva plugins**. Leva a pasta
`tls\` do Qt, necessária para baixar plugins e atualizações por HTTPS.

Publicar uma versão ou um plugin: veja [Plugins e atualizações](plugins.md).

## Dependências

| Dependência | Origem |
|---|---|
| Qt 6.8 (Widgets) | binários oficiais via `aqtinstall` |
| libmpv | [zhongfly/mpv-winbuild](https://github.com/zhongfly/mpv-winbuild/releases) (`mpv-dev-lgpl-x86_64`, build não oficial) |
| libavformat e `ffmpeg.exe` | [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) (GPL, shared; o GPL inclui `libx264` para o fallback) |

## Estrutura do código

| Pasta | Conteúdo |
|---|---|
| `src/core` | funções puras: comando do ffmpeg, bitrate, planejamento do corte, nomes de arquivo |
| `src/player` | player (libmpv) |
| `src/timeline` | widget da timeline e leitura de keyframes (libavformat) |
| `src/export` | exportação (QProcess) |
| `src/platform` | registro do Windows, área de transferência |
| `src/app` | janelas, gerenciador de plugins, atualizador |
| `src/plugin_api` | interface pública dos plugins (versionada) |
| `src/plugins/valorant` | o plugin do Valorant |
| `scripts` | instalador, pacote de plugin, manifesto de atualização, medições |
| `tests` | testes unitários |

`CLAUDE.md` tem um resumo técnico (build, armadilhas e regras do projeto) para quem trabalha no código, inclusive agentes de IA.

## Medições

O app grava em `%LOCALAPPDATA%\Shear\Shear\metrics.log` o tempo até a janela aparecer, até o primeiro frame, até os keyframes
ficarem prontos e o tempo de cada exportação. Para medir as exportações:

```
build\bench_export.exe third_party\ffmpeg\bin\ffmpeg.exe clipe.mp4 <início> <fim> <keyframe_anterior>
```

## Notas técnicas

- **Preview e decodificação:** o shear pinta o preview por software (sem OpenGL/D3D) e decodifica com `nvdec-copy`. Isso evita que o
  overlay da NVIDIA mostre o aviso "pressione ... para abrir" (o `d3d11va`/`dxva2` aciona). Em Configurações dá para desligar o
  NVDEC e usar só a CPU.
- **Prioridade do ffmpeg:** roda abaixo do normal, para não derrubar o FPS de um jogo aberto.
- **Abertura rápida:** nada pesado antes do primeiro quadro (`deferredInit` roda depois de a janela pintar); rede e disco em segundo
  plano, com timeout curto.
