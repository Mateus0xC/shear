<p align="center">
  <img src="resources/logo.png" alt="shear" width="128">
</p>

<h1 align="center">shear</h1>

<p align="center">Cortador de clipes de vídeo para Windows: rápido, leve e feito para clipes de jogos.</p>

---

## O que faz

- Abre o clipe na hora, você marca o início e o fim e exporta com **Enter**.
- **Corte rápido** (copia o vídeo, sem recodificar) ou **exato** (no quadro certo).
- Apaga trechos do meio, liga e desliga o som do desktop e do microfone, e comprime para **até 20 MB**.
- Timeline com **zoom de precisão**, tela cheia, desfazer e refazer.
- **Plugins** opcionais para recursos de jogos. Nenhum vem instalado.

## Instalar

Baixe o **`shear-setup.exe`** na página de [Releases](https://github.com/Mateus0xC/shear/releases) (na versão `v0.x`).
Funciona no Windows 10 e 11 (64 bits), não precisa de administrador e se atualiza sem reinstalar.

## Como usar

1. **Abra um vídeo**: arraste para a janela, use **Abrir vídeo…**, escolha um clipe da lista ou clique com o botão direito num
   vídeo e escolha **Cortar com shear** (ative em Configurações → Menu do Windows).
2. **Marque o trecho** com `I` (início) e `O` (fim), ou arraste os marcadores na timeline.
3. **Exporte** com `Enter`. O arquivo exportado já fica na área de transferência (`Ctrl+V` cola).
4. **Guarde** o clipe com **Manter clipe**. Clipes que você não guardar são apagados ao fechar.

### Atalhos principais

| Tecla | Ação |
|---|---|
| `Espaço` ou clique no vídeo | play / pause |
| `I` / `O` | marca início / fim |
| `←` `→` | pula 1 s (com `Shift` 0,3 s, `Ctrl` 5 s, `Alt` 1 quadro) |
| `X` | apaga um trecho do meio (1º `X` marca onde começa, 2º onde termina) |
| `F` ou duplo clique no vídeo | tela cheia (`F` ou `Esc` voltam) |
| `Ctrl+Z` / `Ctrl+Y` | desfaz / refaz |
| `Enter` | exporta |
| `F1` | abre a ajuda com **todas** as teclas e gestos |

## Plugins e atualizações

- **Plugins** acrescentam recursos ao shear (o primeiro é o do Valorant). Ao abrir um clipe que combina com um plugin, o shear
  sugere instalar; também dá para gerenciar em **Configurações → Plugins…**. O plugin é conferido por fingerprint antes de ligar.
- **Atualizações:** uma vez por dia o shear verifica se há versão nova e **só avisa**; nada é instalado sem você clicar em
  **Atualizar**. Baixa apenas o que mudou (em geral ~1 MB).

## Documentação

| | |
|---|---|
| [Guia de uso](docs/uso.md) | todos os atalhos, modos de corte, som, timeline, lista de clipes e comentários |
| [Plugins e atualizações](docs/plugins.md) | como funcionam, segurança, compatibilidade e como publicar |
| [Plugin do Valorant](docs/valorant.md) | mapa, agente, kills, marcas na timeline e nome do clipe |
| [Desenvolvimento](docs/desenvolvimento.md) | compilar, instalador, dependências, medições e estrutura do código |
