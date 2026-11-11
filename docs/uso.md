# Guia de uso

← [Voltar ao início](index.md)

## Abrir, exportar e guardar

1. Abra um vídeo: argumento de linha de comando, arrastar e soltar na janela, botão **Abrir vídeo…** ou a lista de clipes
   recentes da pasta de clipes (a do ShadowPlay, por exemplo; muda em Configurações).
2. Marque o trecho e exporte com `Enter`. O arquivo exportado já fica na área de transferência (`Ctrl+V` cola).
3. **Os clipes exportados são temporários**: ficam em `%TEMP%\Shear-exports` e são apagados ao fechar o shear (e as sobras de
   uma sessão que travou, na próxima abertura). Para guardar, clique em **Manter clipe**: ele é movido para a pasta configurada
   (ou a do vídeo original).
   - Depois de **Manter** aparece **Apagar original**: manda só o vídeo de origem para a Lixeira, com confirmação.
   - **Apagar original e clipe** (enquanto o clipe é temporário) apaga os dois, com confirmação.
   - Segurando **Shift**, o botão vira **Manter e apagar original**.
   - O ícone de cópia copia o clipe de novo para a área de transferência.
   - Clicar no nome do arquivo exportado abre o clipe no shear; **botão direito**: *Abrir local do arquivo*. Dá para
     **arrastar** o nome para outro programa ou para uma pasta: vai o arquivo.
4. **Nome do arquivo exportado:** `<nome do vídeo>_shear.mp4`; se já existir, vira `_2`, `_3`… (um plugin pode dar outro nome).

## Atalhos

`F1` (ou **Configurações → Atalhos e controles…**) abre a ajuda com todas as teclas, gestos do mouse, painéis e Configurações.

| Tecla | Ação |
|---|---|
| `F5` ou `Ctrl+R` (na lista de clipes) | relê a pasta e atualiza a lista |
| `F` (ou duplo clique no vídeo) | tela cheia: ficam só o vídeo e a timeline, sem o zoom de precisão; `F` ou `Esc` voltam |
| `Espaço` ou clique no vídeo | play / pause. Ao chegar na marca de fim (`O`) a reprodução **para**; play de novo recomeça do `I` |
| `Ctrl` + roda do mouse sobre o vídeo | zoom no vídeo (até 8×); com zoom, arraste para mover a imagem; `Ctrl+0` volta a 1× |
| `[` `]`, `-` `+` (ou o botão **1×**) | velocidade do preview: 0,25× a 3× (não muda o arquivo); `Backspace` volta a 1× |
| `I` / `O` ou `Z` / `C` | marca início / fim (no modo Rápido o `I` encaixa no keyframe) |
| `←` `→` ou `A` `D` | pula 1 s sem pausar |
| `Shift` + `←` `→` | 0,3 s (passo fino) |
| `Ctrl` + `←` `→` | 5 s |
| `Ctrl` + `Shift` + `←` `→` | 15 s |
| `Alt` + `←` `→` (ou `,` `.`) | 1 quadro (pausa o vídeo) |
| `Home` / `End` | início / fim do vídeo |
| `J` / `L` | −5 s / +5 s |
| `Shift+Q` / `Shift+E` | vai para o ponto `I` / `O` (o `E` com o vídeo rodando pausa em cima do `O`) |
| `Q` / `E` | vai para a parada anterior / seguinte: marcas da timeline (se houver plugin) e os pontos `I` e `O` |
| `0` a `9` | pula para 0% a 90% do vídeo |
| `↑` `↓` ou `W` `S` ou roda sobre o vídeo | volume do preview (não muda o arquivo) |
| `X` | apaga um trecho do **meio** (veja abaixo) |
| `Ctrl+Z` / `Ctrl+Y` (ou `Ctrl+Shift+Z`) | desfaz / refaz: marcadores, trechos apagados e volume do mic |
| `Enter` | exporta |
| `Esc` | sai da tela cheia; senão, cancela o trecho do `X` em marcação |

### Apagar um trecho do meio (`X`)
O 1º `X` marca onde o trecho começa e o 2º onde termina (`Esc` cancela). O trecho fica hachurado em laranja, o preview pula por
cima dele e, na exportação, o que sobra é emendado. Com trechos apagados o modo **Exato** é selecionado à força (o vídeo é
recodificado) e o **Rápido** fica desabilitado; quando todos somem, volta ao modo de antes. Botão direito num trecho apagado
traz ele de volta. Em **Configurações → Emendas**, "Suavizar as emendas" (ligado por padrão) faz o vídeo esmaecer 0,2 s e o áudio
fazer crossfade nas emendas.

## Modo de corte (Rápido | Exato)

- **Rápido**: copia o vídeo sem recodificar (instantâneo). O corte só pode começar num keyframe, então o marcador de **início
  encaixa nos keyframes** (as marcas na timeline). O fim pode ficar em qualquer frame.
- **Exato**: corta no frame certo recodificando o vídeo (NVENC, ou `libx264` sem NVENC). É mais lento.

### Até 20 MB
A caixa **Até 20 MB** comprime o clipe para ficar com até esse tamanho: o bitrate é calculado pela duração (vídeo + áudio em
AAC), a resolução cai (720p, 540p ou 360p) e o vídeo vai para 30 quadros/s quando o bitrate é baixo. Se o resultado passar do
limite, o shear tenta de novo (até 2 vezes) com bitrate menor. Recodifica, então o modo fica Exato. Em **Configurações → Tamanho**
dá para fazer cada vídeo já começar com ela ligada.

## Som

Com **uma única faixa** não há painel lateral: só um alto-falante na barra de cima, ao lado da engrenagem, que liga e desliga o
som (preview e arquivo exportado). Com **duas ou mais**, um painel à direita tem um botão por faixa (aceso = ligada) e um fader de
volume só para o **Mic** (−30 a +30 dB; duplo clique ou o botão com o valor zera). O desktop não tem ajuste de volume.

- **Ligada** = você ouve no preview **e** vai no arquivo exportado.
- **Padrão:** só o **Desktop** (faixa 1) fica ligado; o **Mic** começa desligado.
- **Ligando o Mic**, as faixas são **juntadas em uma só**: o desktop mantém o nível original e o mic passa por filtros (corta
  graves e agudos, gate) e ganha +20 dB, mais o que você ajustar no fader. Há um limitador contra clipping. Saída em AAC 320k.
- Com uma só faixa ligada, o áudio é **copiado sem recodificar**. Nenhuma faixa ligada: o clipe sai sem som.

O volume do preview (roda do mouse e setas) é independente e só serve para você ouvir.

## Timeline

### Zoom de precisão
Ao arrastar o cursor ou os marcadores de início e fim, **puxe o mouse para baixo**: abre uma régua ampliada abaixo da timeline,
e uma moldura mostra na timeline qual trecho está ampliado.

- **4 níveis** (janelas de 20 s, 5 s, 1,5 s e 0,4 s): quanto mais para baixo, mais zoom e mais fino o movimento horizontal. Nos
  dois níveis mais finos o marcador anda de quadro em quadro.
- O ponto sob o mouse fica na mesma coluna da régua; entrar no zoom ou trocar de nível não faz o marcador pular. Voltar com o
  mouse para dentro da timeline sai do zoom, e soltar o botão fecha a régua.
- **Enquanto o botão está pressionado na timeline, o vídeo fica pausado**; ao soltar, volta a tocar se estava tocando.
- **Som ao arrastar:** a cada posição nova toca um trecho curto de áudio (mais curto quanto maior o zoom). Dá para desligar em
  **Configurações → Som ao arrastar**.
- Na tela cheia o zoom de precisão fica desligado.

### Roda do mouse na timeline
Pula como as setas: 1 s por clique (`Shift` 0,3 s, `Ctrl` 5 s, `Ctrl+Shift` 15 s, `Alt` 1 quadro); para cima avança.

## Lista de clipes

- A lista mostra os clipes mais recentes da pasta configurada: nome, data e comentário. Plugins podem acrescentar ícone, tag e imagem.
- **Botão direito** numa linha: *Abrir local do arquivo* e *Excluir* (vai para a Lixeira, com confirmação). No editor, o botão
  direito no nome do arquivo tem *Excluir* (apaga o original e, se o clipe ainda é temporário, o clipe também).
- **Arrastar** uma linha leva o arquivo para outro programa ou pasta.
- **Clipe recém-salvo:** se você salvou um clipe há **até 15 s** (conta desde a criação do arquivo) e abre o shear sem indicar
  um arquivo, ele **já abre nesse clipe**. O clipe mais recente, se criado há **até 5 minutos**, fica no topo, destacado
  ("NOVO · há N min").

## Comentário

Ao lado do nome do arquivo há um ícone de editar (✎). Clique, digite e aperte `Enter` (`Esc` descarta). O comentário vale para
qualquer clipe, aparece na lista de clipes e é gravado também ao voltar para a lista, ao abrir outro clipe e ao sair do
programa. Enquanto você digita, os atalhos do editor ficam desligados. Fica guardado em `clipmeta.json`.

## Começo corrompido

Se o início do vídeo está quebrado (quadros que não decodificam, como em alguns clipes do ShadowPlay), o shear detecta ao abrir,
pula esse trecho (hachurado em vermelho na timeline) e começa no primeiro keyframe sadio. O trecho não pode ser marcado nem
exportado. A checagem olha só os primeiros 0,5 s; só se houver problema ele amostra o vídeo, o que leva uns 2 a 3 s, com tela preta e
"Verificando o início do vídeo…". O resultado fica guardado por arquivo, então da próxima vez é imediato.

## Menu de contexto do Explorer

Em **Configurações → Menu do Windows** o item “Cortar com shear” passa a aparecer ao clicar com o botão direito em vídeos (mp4, mkv,
mov, webm, avi). Não precisa de administrador. No Windows 11 o item fica em **Mostrar mais opções** (`Shift+F10`), a menos que
você use o menu clássico. Quem instalou o menu numa versão antiga é migrado sozinho.
