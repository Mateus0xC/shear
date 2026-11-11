# Plugin do Valorant

← [Voltar ao início](index.md) · [Plugins e atualizações](plugins.md)

Plugin opcional do shear para clipes do Valorant: mostra o **mapa**, o **agente** e as **kills e mortes**, marca tudo na timeline,
ajusta o início e o fim para o último round e dá nome ao clipe exportado. Funciona sozinho, sem configurar nada. Se o jogo estiver
fechado ou algo falhar, o shear se comporta como sem o plugin, sem aviso na tela.

## Instalar
Ao abrir um clipe do Valorant (nome ou pasta com "valorant") o shear sugere instalar o plugin. Também dá para instalar em
**Configurações → Plugins…**. Veja [Plugins e atualizações](plugins.md).

## O que aparece

### Mapa, agente e kills
Ao lado do nome do arquivo aparece `· Mapa · Agente · 3k`, com o ícone do agente e a imagem do mapa ao fundo da barra de cima
(bem apagada). Os dados vêm do próprio jogo; o que o jogo não identificou fica como "mapa", "agente" ou "kills" em cinza e itálico, e
é **clicável** para você escolher à mão:
- Mapas e agentes vêm da lista do valorant-api.com; **Outro…** aceita qualquer nome. No menu de mapas cada item é a imagem do
  mapa com o nome por cima; ficam só os do modo bomba, e os demais (treino, mata-mata etc.) ficam em **Outros modos**.
- **Kills** de 1k a 10k; só aparece quando o jogo não achou kills no clipe.
- O que o jogo identificou aparece como texto e não dá para mudar.

### Kills e mortes na timeline
Depois que a partida acaba, o plugin baixa os dados dela (em segundo plano, uma partida por vez) e marca o clipe: **losangos verdes =
kills, roxos = mortes**. As tags de kills são `2k`, `3k`, `4k`, `5k`… conforme o maior número de kills **num mesmo round** dentro
do clipe. Morte não vira tag, aparece só na timeline.

`Q` e `E` pulam entre as marcas e os pontos `I` e `O`; `Shift+Q` e `Shift+E` vão direto para `I` e `O`.

### I e O automáticos
Se o clipe tem rounds conhecidos, o `I` vai para o começo do **último round capturado** e o `O` para o fim dele (o fim de um round
é o começo do seguinte), desde que o trecho dentro do clipe tenha 10 s ou mais; senão, vale o round anterior. No modo Rápido o `I`
encaixa no keyframe mais próximo e nunca cai no começo corrompido. Só acontece se você ainda não mexeu em `I`, `O` ou nos
trechos apagados, e `Ctrl+Z` desfaz. Ao abrir, o vídeo já começa a tocar no `I`.

### Lista de clipes
Cada linha mostra o ícone do agente, o nome, a data, a tag (`5k`; de 5k para cima em amarelo), o comentário e, à direita, o nome do
mapa em cima da imagem dele. Um clipe só ganha esses dados depois de ser aberto uma vez com o Riot Client logado.

### Nome do clipe exportado
`Valorant aaaa.mm.dd - Mapa_Agente_Nk.mp4`, por exemplo `Valorant 2026.11.08 - Ascent_Jett_4k.mp4`:
- o `Nk` é o maior número de kills num mesmo round **no trecho exportado** e some se não houver;
- a data é a do clipe original; se só o mapa ou só o agente for conhecido, só ele entra;
- se já existir um arquivo com esse nome na pasta, acrescenta `_2`, `_3`, `_4`…

Ao clicar em **Manter**, o clipe novo guarda mapa, agente, tag, o que você escolheu à mão e as kills e mortes **já no tempo dele**: ao
reabri-lo, tudo aparece de novo (inclusive as marcas) sem consultar a Riot.

## Como funciona

1. **Quem é o clipe:** lê o `lockfile` do Riot Client, pede os tokens ao servidor local e procura a partida: a em andamento (para
   clipe recente) ou, senão, no histórico, comparando o horário do clipe com o da partida. Os IDs viram nomes pelo valorant-api.com
   (cache local; só baixa de novo se aparecer um ID desconhecido).
2. **Quando:** tudo roda depois do primeiro quadro do vídeo, em segundo plano, com limite de 2 s por requisição.
3. **Só leitura:** só requisições GET. A senha do lockfile e os tokens ficam só na memória e nunca vão para disco nem para log.
   O certificado autoassinado é aceito apenas para `127.0.0.1`.
4. **Imagens:** o ícone do agente e a faixa do mapa são baixados uma vez, reduzidos e guardados em `valorant\img\` (só os que você
   usa; ao abrir um menu, os que faltam são baixados para a próxima vez).
5. **Dados guardados:** `%LOCALAPPDATA%\Shear\Shear\valorant\` (`clips.json`, `matches\<id>.json`, `img\`). Clipes já identificados
   continuam completos com o jogo fechado.

### Alinhamento das marcas com o vídeo
O clipe é alinhado à partida supondo que cobre `[data de criação − duração, data de criação]`, mais um deslocamento. Os horários da
API ficam de 60 a 110 s antes do vídeo, e esse tanto muda de partida para partida (a API marca o início da seleção de agentes). O jogo
grava no `ShooterGame.log` o instante real em que o round 0 passa a valer (`Gameplay started at ... (server time 45.x)`, logo depois
de `[Match Load Times] MatchId: <id>`). O plugin lê esse log (o atual e os backups em `%LOCALAPPDATA%\VALORANT\Saved\Logs`, uma vez
por partida), calcula o deslocamento exato (±0,1 s) e guarda em `matches\<id>.json`. Se o log daquela partida já não existir,
vale o padrão de 104 s.

Para acertar à mão: pause no quadro de uma kill e aperte **`K`**: a marca mais próxima passa a cair ali, para esse clipe e os outros
da mesma partida (esse ajuste tem prioridade sobre o automático). **`Shift+K`** desfaz a calibração e `Ctrl+Z` também desfaz o `K`.
