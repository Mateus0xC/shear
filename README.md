# Shear

Cortador de clipes de vídeo para Windows, feito para ser rápido: abrir o clipe do ShadowPlay,
marcar início e fim, exportar e colar no Discord.

## Uso

1. Abra um vídeo: argumento de linha de comando, arrastar e soltar na janela, botão **Abrir vídeo…**
   ou a lista de clipes recentes da pasta do ShadowPlay.
2. Marque o trecho e exporte com `Enter`. O arquivo exportado já fica na área de transferência
   (Ctrl+V no Discord).
3. **Os clipes exportados são temporários**: ficam em `%TEMP%\Shear-exports` e são apagados ao fechar o Shear
   (e as sobras de uma sessão que travou são apagadas na próxima abertura). Para guardar um clipe, clique em
   **Manter**: ele é movido para a pasta configurada (ou a do vídeo original). **Apagar** remove na hora.

| Tecla | Ação |
|---|---|
| `Espaço` | play/pause |
| `I` / `O` | marca início / fim |
| `←` / `→` (ou `,` / `.`) | frame anterior / seguinte |
| `J` / `L` | −5 s / +5 s |
| `↑` / `↓` ou roda do mouse sobre o vídeo | volume do preview (estilo VLC; não muda o arquivo) |
| `K` | encaixa o início no keyframe mais próximo |
| `Enter` | exporta |
| `Esc` | volta à lista de clipes |

### Modos de corte do início
- **Rápido**: `-c copy`. O clipe começa no keyframe anterior. Um marcador laranja mostra o ponto real e um aviso diz quanto antes ele começa.
- **Exato**: corta no frame certo recodificando o vídeo com NVENC (ou `libx264` sem NVENC). O smart cut chega na Fase 2.
- **Automático** (padrão): usa Rápido se a distância ao keyframe anterior for menor que o limite (0,5 s por padrão); senão, Exato.

### Áudio
Um cartão compacto no canto superior direito tem um botão por faixa (aceso = ligada) e um fader de volume só para o **Mic**
(−30 a +30 dB, com escala; duplo clique ou o botão com o valor zera). O desktop não tem ajuste de volume.

- **Ligada** = você ouve a faixa no preview **e** ela vai no arquivo exportado (o que você ouve é o que sai).
- **Padrão:** só o **Desktop** (faixa 1) fica ligado; o **Mic** começa desligado.
- **Ligando o Mic**, as faixas são **juntadas em uma só**: o desktop mantém o nível original e o mic passa por
  highpass/lowpass/gate e ganha +20 dB (mais o que você ajustar no fader). A mixagem usa `amix` sem normalização (o desktop
  não muda de volume ao ligar o mic) e um limitador contra clipping. Saída em AAC 320k.
- Com uma só faixa ligada, o áudio é **copiado sem recodificar**.
- Nenhuma faixa ligada: o clipe sai sem áudio.

O volume do preview (roda do mouse / setas) é independente e só serve para você ouvir.

### Precisão na timeline
Ao arrastar o cursor ou os marcadores de início/fim, **puxe o mouse para baixo**: aparece uma régua ampliada abaixo da timeline,
centrada na posição atual. Quanto mais para baixo, maior o zoom (janela de ~12 s até ~0,4 s) e mais fino fica o movimento
horizontal. Soltar o botão fecha a régua.

### Overlay da NVIDIA
O Shear pinta o preview por software (sem OpenGL/D3D) e decodifica com `nvdec-copy`. Em testes nesta máquina, isso não
aciona o aviso "pressione ... para abrir" do overlay da NVIDIA, enquanto `d3d11va`/`dxva2` acionam. Em Configurações dá para
desligar o NVDEC e usar só a CPU.

O ffmpeg roda com prioridade abaixo do normal para não derrubar o FPS do jogo.

## Compilar

Requisitos: Windows 11 x64, Visual Studio 2022 Build Tools (carga de trabalho C++), CMake, Ninja, Python (para o aqtinstall).

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

Se o Qt estiver em outro lugar, ajuste `CMAKE_PREFIX_PATH` em `CMakePresets.json`.
O build já copia Qt, `libmpv-2.dll`, as DLLs do ffmpeg e o `ffmpeg.exe` para `build\src\app\`;
essa pasta é portátil (copie-a inteira ou compacte em .zip).

### Dependências
| Dependência | Origem |
|---|---|
| Qt 6.8 (Widgets) | binários oficiais via `aqtinstall` |
| libmpv | [zhongfly/mpv-winbuild](https://github.com/zhongfly/mpv-winbuild/releases) (`mpv-dev-lgpl-x86_64`, build não oficial) |
| libavformat e `ffmpeg.exe` | [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) (GPL, shared; o GPL inclui `libx264` para o fallback) |

## Medições

O app grava em `%LOCALAPPDATA%\Shear\Shear\metrics.log` o tempo até a janela aparecer, até o primeiro frame,
até os keyframes ficarem prontos e o tempo de cada exportação. Para medir as exportações:

```
build\bench_export.exe third_party\ffmpeg\bin\ffmpeg.exe clipe.mp4 <início> <fim> <keyframe_anterior>
```

## Estrutura
`src/core` (funções puras: comandos do ffmpeg, bitrate, planejamento do corte, nomes) · `src/player` (libmpv) ·
`src/timeline` (widget e leitura de keyframes via libavformat) · `src/export` (QProcess) · `src/platform` (registro, área de transferência) · `src/app` (janelas) · `tests`.
