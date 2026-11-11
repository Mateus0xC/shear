# Plugins e atualizações

← [Voltar ao início](index.md)

O shear base é só um cortador de clipes. Recursos de jogos ficam em **plugins**, que **não vêm instalados**. O primeiro é o
[plugin do Valorant](valorant.md).

## Plugins

### Instalar, atualizar e remover
- **Sugestão:** ao abrir um clipe cujo nome ou pasta combina com um plugin ainda não instalado, aparece um aviso discreto no topo
  do editor: **Instalar**, **Saiba mais**, **Agora não** e **Não perguntar de novo**. O texto curto e o "Saiba mais" vêm do próprio
  plugin (`plugin.json`).
- **Configurações → Plugins…** lista os plugins e permite instalar, atualizar e remover. A instalação é por usuário (sem
  administrador) e o plugin liga na hora, sem reiniciar. Remover um plugin em uso vale na próxima abertura.
- Onde ficam: `%LOCALAPPDATA%\Shear\plugins\<id>\`. Para desenvolvimento, também vale `plugins\<id>\` ao lado do `shear.exe`.

### Segurança: fingerprint
O catálogo (`plugins/catalog.json` no repositório) traz o SHA-256 e o tamanho de cada arquivo do plugin.
- Ao instalar, o shear confere cada arquivo baixado e **recusa** a instalação se algum não bater.
- Ao terminar, grava um `installed.json` com os hashes e os **reconfere a cada abertura**, antes de carregar a DLL: arquivo
  trocado ou alterado = o plugin não carrega.
- Caminhos fora da pasta do plugin (`..`, absolutos) são recusados.

Plugins são código nativo (DLL): instale só os que vêm do catálogo oficial.

### Compatibilidade (não precisa ser a mesma versão do shear)
A API de plugins (`src/plugin_api/ShearPlugin.h`) tem versão `MAIOR.MENOR`:
- Plugin e shear se entendem se a **MAIOR** for igual e o shear tiver a **MENOR** que o plugin pede (`minHostApiMinor`).
- Recursos novos da API entram só como MENOR, no fim das interfaces. Por isso um plugin antigo continua funcionando num shear mais
  novo, e o catálogo oferece a **versão mais nova do plugin que o seu shear consegue usar**.
- Só uma mudança que quebra algo existente muda a MAIOR.

### Para quem desenvolve plugins
Um plugin é uma DLL de plugin do Qt mais um `plugin.json` (`id`, `name`, `version`, `description` curta, `details` = "Saiba mais",
`library`, `apiMajor`, `apiMinor`, `minHostApiMinor`, `appliesTo`). O exemplo é `src/plugins/valorant/`.
Publicar: `powershell -File scripts\make-plugin-package.ps1 -Id <id>` gera `dist\plugins\<id>\` e atualiza `plugins/catalog.json`;
os arquivos vão como assets da Release `plugin-<id>-<versão>` e o catálogo é commitado.

Para testar sem rede: `SHEAR_PLUGINS_DIR` troca a pasta de plugins e `SHEAR_PLUGIN_CATALOG` troca o catálogo (aceita `file:///...`).

## Atualizações (sem reinstalar)

O shear e os plugins só se atualizam **quando você clica**. Uma vez por dia, em segundo plano e depois de a janela aparecer, o
shear consulta um manifesto pequeno (poucos KB). Se houver versão nova, aparece uma faixa verde no topo: **Atualizar** ou
**Depois**. Também dá para pedir em **Configurações → Plugins… → Verificar atualizações agora**.

- **Só baixa o que mudou:** o manifesto lista todos os arquivos com SHA-256 e tamanho; o shear compara com os seus e baixa apenas
  os diferentes. O que pesa (player e FFmpeg, ~300 MB) quase nunca muda; uma atualização típica é o `shear.exe` (~1 MB).
- **Fingerprint:** cada arquivo baixado é conferido (tamanho e SHA-256) antes de ser usado. Se algum não bater, nada é trocado.
- **Troca com o shear aberto:** o Windows não deixa sobrescrever um `.exe` em uso, mas deixa renomear. O arquivo atual vira
  `<nome>.old`, o novo entra no lugar e, se algo falhar no meio, tudo volta ao que era. Depois é só **Reiniciar agora** (o novo
  shear espera o antigo fechar); os `.old` somem na abertura.
- **Plugins** são consultados junto (a versão mais nova que o seu shear consegue usar) e atualizados no mesmo clique.
- Se o shear estiver numa pasta sem permissão de escrita (instalado para todos os usuários), ele avisa para usar o instalador.

### Publicar uma versão do shear
1. Suba `version` em `project(Shear VERSION ...)` no `CMakeLists.txt` e compile.
2. `powershell -File scripts\make-update-manifest.ps1 -Notes "o que mudou"` gera `updates/manifest.json` e, em
   `dist\update\v<versão>\`, **só os arquivos alterados** (os iguais reaproveitam a URL antiga).
3. Publique esses arquivos como assets da Release `v<versão>` e faça commit do manifesto.

`SHEAR_UPDATE_MANIFEST` (aceita `file:///...`) troca o manifesto, para testar sem rede.
