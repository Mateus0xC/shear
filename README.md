# shear

Cortador de clipes de vídeo para Windows, feito para ser rápido: abrir o clipe do ShadowPlay,
marcar início e fim, exportar e colar no Discord.

## Uso

1. Abra um vídeo: argumento de linha de comando, arrastar e soltar na janela, botão **Abrir vídeo…**
   ou a lista de clipes recentes da pasta do ShadowPlay.
2. Marque o trecho e exporte com `Enter`. O arquivo exportado já fica na área de transferência
   (Ctrl+V no Discord).
3. **Os clipes exportados são temporários**: ficam em `%TEMP%\Shear-exports` e são apagados ao fechar o shear
   (e as sobras de uma sessão que travou são apagadas na próxima abertura). Para guardar um clipe, clique em
   **Manter clipe**: ele é movido para a pasta configurada (ou a do vídeo original) e aparece o botão **Apagar original** (manda só o vídeo de origem para a Lixeira, com confirmação). **Apagar original e clipe** apaga os dois (confirma antes). Segurando **Shift**, o Manter vira "Manter e apagar original" (mantém o clipe e manda o original para a Lixeira, com confirmação). O ícone de cópia ao lado copia o clipe de novo para a área de transferência; clicar no nome do arquivo abre a pasta. Ao voltar para a lista de clipes ou fechar, o clipe não mantido é apagado.

| Tecla | Ação |
|---|---|
| `Espaço` ou clique no vídeo | play/pause (com uma animação rápida sobre o vídeo; pular também mostra "+5 s", "−0,5 s"…). Ao chegar na marca de fim (`O`) a reprodução **para**, como se o vídeo tivesse acabado; play de novo (no `O` ou no fim do vídeo) **recomeça do `I`** |
| `Ctrl` + roda do mouse sobre o vídeo | zoom no vídeo (até 8×); com zoom, arraste com o botão esquerdo para mover a imagem; `Ctrl` + `0` volta a 1× |
| `-` / `+` (ou o botão **1×** no topo) | velocidade do preview: 0,25× a 3× (não muda o arquivo exportado); `Backspace` volta a 1× |
| `I` / `O` ou `Z` / `C` | marca início / fim (no modo Rápido o `I`/`Z` encaixa no keyframe e o playback vai para lá) |
| `←` `→` ou `A` `D` | pula 1 s para trás / frente, sem pausar o vídeo (segurando, repete) |
| `Shift` + `←` `→` / `A` `D` | 0,3 s (passo fino) |
| `Ctrl` + `←` `→` / `A` `D` | 5 s |
| `Ctrl` + `Shift` + `←` `→` / `A` `D` | 15 s |
| `Alt` + `←` `→` / `A` `D` (ou `,` `.`) | 1 frame (pausa o vídeo para mostrar o quadro) |
| `Home` / `End` | início / fim do vídeo |
| `J` / `L` | −5 s / +5 s |
| `X` | apaga um trecho do **meio**: o 1º `X` marca onde ele começa, o 2º onde termina (`Esc` cancela). O trecho fica hachurado em laranja na timeline, o preview pula por cima dele, e na exportação o que sobra é emendado (com trechos apagados o modo **Exato** é selecionado à força e o **Rápido** fica desabilitado, porque o vídeo é recodificado; quando todos os trechos somem, volta ao modo de antes). Botão direito num trecho apagado traz ele de volta; `Ctrl+Z` desfaz um passo de cada vez (o 2º `X` sai, o início continua marcado) |
| **Configurações → Emendas** | "Suavizar as emendas" (ligado por padrão): nas emendas dos trechos apagados o vídeo esmaece rapidinho (0,2 s) e o áudio faz **crossfade**; o começo e o fim do vídeo ficam como estão |
| `Q` / `E` | vai para o ponto de início (`I`) / de fim (`O`) |
| `0` a `9` | pula para 0% a 90% do vídeo (como no YouTube) |
| `↑` / `↓` ou `W` / `S` ou roda do mouse sobre o vídeo | volume do preview (estilo VLC; não muda o arquivo) |
| `Ctrl` + `Z` | desfaz: marcadores de início/fim e volume do mic |
| `Ctrl` + `Y` (ou `Ctrl` + `Shift` + `Z`) | refaz o que foi desfeito |
| `Enter` | exporta |
| `Esc` | cancela o trecho do `X` que está sendo marcado (para voltar à lista de clipes use o botão **← Clipes**) |
| puxar o mouse para baixo ao arrastar na timeline | régua ampliada (zoom de precisão); em tela cheia, quando falta espaço até a borda da tela, as faixas de zoom ficam mais estreitas e a régua aparece acima da timeline |
| roda do mouse na timeline | pula como as setas: 1 s por clique (Shift 0,3 s, Ctrl 5 s, Ctrl+Shift 15 s, Alt 1 quadro); para cima avança |

### Menu de contexto do Explorer
Em **Configurações → Menu do Windows** (caixa de seleção) o item “Cortar com shear” passa a aparecer ao clicar com o botão direito em
vídeos (mp4, mkv, mov, webm, avi). Não precisa de administrador. No Windows 11 o item fica em **Mostrar mais opções** (Shift+F10),
a menos que você use o menu clássico do Windows. Quem instalou o menu numa versão antiga é migrado sozinho ao abrir o app.

### Clipes recém-salvos
- **Começo corrompido:** se o vídeo tem o início quebrado (quadros que não decodificam, como em alguns clipes do ShadowPlay), o shear detecta ao abrir, pula esse trecho (hachurado em vermelho na timeline) e começa no primeiro keyframe sadio. O trecho não pode ser marcado nem exportado. A checagem olha só os primeiros 0,5 s (rápido, não atrasa o primeiro quadro); só se houver problema ele amostra o vídeo de 3 em 3 s, em paralelo, com um decodificador novo por amostra (um quadro quebrado "envenena" o decodificador e faz parecer que o resto todo está ruim). Isso leva uns 2 a 3 s, com tela preta e "Verificando o início do vídeo…" para não mostrar quadros quebrados; o resultado fica guardado por arquivo, e na próxima abertura é imediato.
- Se você salvou um clipe há **até 10 s** (conta desde a **criação do arquivo**, não a duração do vídeo, na pasta do ShadowPlay) e abre o shear sem
  indicar um arquivo, ele **já abre direto nesse clipe**. Se o shear já estiver aberto e for chamado de novo, também.
- O **clipe mais recente**, se o arquivo foi criado há **até 5 minutos**, fica no topo da lista de recentes, destacado (cor de fundo, negrito e
  "NOVO · há N min"). Só ele é destacado, não todos os dos últimos 5 minutos.

### Modo de corte (botões Rápido | Exato)
- **Rápido**: copia o vídeo sem recodificar (instantâneo). O corte só pode começar num keyframe, então o marcador de
  **início encaixa nos keyframes** (as marcas na timeline) ao arrastar, ao apertar `I` e ao trocar para este modo.
  O fim pode ficar em qualquer frame.
- **Exato**: corta no frame certo recodificando o vídeo (NVENC, ou `libx264` sem NVENC). É mais lento.
  O smart cut, que só recodifica o começo, está planejado para a Fase 2.

### Áudio
Com **uma única faixa** não há painel lateral: só um alto-falante na barra de cima, ao lado da engrenagem, que liga/desliga o som (preview e vídeo exportado).
Com **duas ou mais**, um painel à direita tem um botão por faixa (aceso = ligada) e um fader de volume só para o **Mic**
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

### Controle preciso (puxar o mouse para baixo)
Ao arrastar o cursor, o marcador de início ou o de fim, **puxe o mouse para baixo**:
- Abre uma régua ampliada logo abaixo da timeline, e uma moldura mostra na timeline qual trecho está ampliado.
- O zoom tem **4 níveis fixos** (janelas de 20 s, 5 s, 1,5 s e 0,4 s); quanto mais para baixo, mais zoom. O nível atual fica
  destacado na régua e há uma pequena folga para ele não ficar trocando na borda.
- O ponto sob o mouse fica na mesma coluna da régua, e a linha branca mostra o tempo exato. Entrar no zoom ou trocar de nível
  não faz o marcador pular. Nos dois níveis mais finos o marcador anda de frame em frame (taxa de quadros do vídeo).
- Voltar com o mouse para dentro da timeline sai do zoom. Uma etiqueta abaixo da timeline lembra do gesto durante o arrasto.
- **Enquanto o botão do mouse está pressionado na timeline, o vídeo fica pausado e parado**; ao soltar, volta a tocar se estava
  tocando.
- **Som ao arrastar:** a cada posição nova toca um trecho curto de áudio (o vídeo fica parado). Quanto maior o zoom, mais curto o
  trecho (de ~0,16 s sem zoom até ~0,05 s no zoom mais fino). Toca quando o **frame muda**: mexer o mouse dentro do mesmo frame,
  ou subir e descer para trocar o nível de zoom, não dispara som. Dá para desligar em **Configurações → Som ao arrastar**.

### Overlay da NVIDIA
O shear pinta o preview por software (sem OpenGL/D3D) e decodifica com `nvdec-copy`. Em testes nesta máquina, isso não
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

### Instalador

`winget install JRSoftware.InnoSetup` (uma vez) e depois, com o projeto compilado, `scripts\make-installer.ps1` gera `dist\shear-setup.exe` (~140 MB, já inclui o runtime do Visual C++). Instala só para o usuário (sem administrador) em `%LOCALAPPDATA%\Programs\shear`, com atalho no menu Iniciar (e na área de trabalho, se marcado) e desinstalador, que também remove o item do botão direito.

### Dependências
| Dependência | Origem |
|---|---|
| Qt 6.8 (Widgets) | binários oficiais via `aqtinstall` |
| libmpv | [zhongfly/mpv-winbuild](https://github.com/zhongfly/mpv-winbuild/releases) (`mpv-dev-lgpl-x86_64`, build não oficial) |
| libavformat e `ffmpeg.exe` | [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) (GPL, shared; o GPL inclui `libx264` para o fallback) |

## Medições

O app grava em `%LOCALAPPDATA%\shear\shear\metrics.log` o tempo até a janela aparecer, até o primeiro frame,
até os keyframes ficarem prontos e o tempo de cada exportação. Para medir as exportações:

```
build\bench_export.exe third_party\ffmpeg\bin\ffmpeg.exe clipe.mp4 <início> <fim> <keyframe_anterior>
```

## Estrutura
`src/core` (funções puras: comandos do ffmpeg, bitrate, planejamento do corte, nomes) · `src/player` (libmpv) ·
`src/timeline` (widget e leitura de keyframes via libavformat) · `src/export` (QProcess) · `src/platform` (registro, área de transferência) · `src/app` (janelas) · `tests`.
