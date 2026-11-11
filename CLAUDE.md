# Shear

App desktop Windows (C++20, CMake + Ninja, Qt 6.8 Widgets, libmpv, ffmpeg.exe) para **cortar clipes** de vídeo. O shear base é só
o cortador; recursos de jogos ficam em **plugins** (hoje, o do Valorant) que não vêm instalados. O `README.md` é a porta de entrada; a documentação detalhada está em `docs/` (site via `mkdocs.yml`).

## Regras do projeto
- **Commits**: mensagem em português, no estilo dos existentes (curta, no indicativo, diz o que mudou para o usuário). **Sem
  `Co-Authored-By`** nem qualquer outra linha de coautoria nos commits e PRs. Não altere `user.name` / `user.email` do Git.
- **Só commitar/dar push/publicar quando o usuário pedir.** Release, upload de assets e `push --force` são ações dele; confirme antes.
- **Interface, mensagens e comentários em português do Brasil.** Textos novos de interface vão também para `HelpDialog.cpp` (F1) , `README.md` e `docs/`.
- `src/core/` é **puro** (sem GUI/rede/disco), com testes em `tests/`. Lógica nova que dê para ser pura vai para lá (ou para o
  código puro do plugin) e ganha teste.

## Mapa do código
- `src/core/`: planejamento de corte, comando do ffmpeg, estimativas, nomes de arquivo (`OutputNaming`). Testes: `tests/test_core.cpp`.
- `src/app/`: janela principal (`MainWindow`, grande: cuidado ao editar), diálogos, `PluginManager` / `PluginInstaller` /
  `PluginsDialog` (plugins), `Updater` (atualização do shear), `ClipMeta` (comentário do clipe, guardado em `clipmeta.json`).
- `src/player/` (libmpv), `src/timeline/` (timeline, keyframes, começo corrompido), `src/export/`, `src/platform/`, `src/util/`
  (`Metrics`, `CrashLog`, `Settings`).
- `src/plugin_api/ShearPlugin.h`: a interface pública dos plugins. **Versionada `MAIOR.MENOR`**: MAIOR muda só se algo existente
  quebra; recurso novo entra **no fim** das interfaces com `kApiMinor` maior (plugin e shear não precisam ser da mesma versão).
- `src/plugins/valorant/`: o plugin (`valorant.dll`) + `plugin.json` (`description` curta, `details` = "Saiba mais", `appliesTo`).
  A parte pura (`MatchEvents`, `ValorantParse`, `ValorantNaming`) é testada em `tests/test_valorant.cpp`.
- `scripts/`: `make-installer.ps1`, `make-plugin-package.ps1`, `make-update-manifest.ps1`, medições.

## Compilar e testar (Windows)
```powershell
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vc`" >nul 2>&1 && set PATH=D:\Qt\6.8.3\msvc2022_64\bin;%PATH% && cmake --build build > %TEMP%\build.txt 2>&1"
# conferir o CÓDIGO DE SAÍDA e o arquivo inteiro (não só o fim da saída), depois:
cmd /c "set PATH=D:\Qt\6.8.3\msvc2022_64\bin;%PATH% && ctest --test-dir build --output-on-failure"
```
- Pré-configuração: `cmake --preset release` (Ninja, Qt em `D:/Qt/6.8.3/msvc2022_64`). Dependências em `third_party/` (`scripts/setup-deps.ps1`).
- Testes: `core`, `valorant`, `plugins`, `updater`, `navkeys`. **Um build que falhou deixa os executáveis antigos**: o `ctest` "passa"
  mesmo assim. Confirme que o `.exe` mudou de horário. Testes com rede/arquivos usam `file://` e pastas temporárias.
- Variáveis para teste sem rede: `SHEAR_PLUGINS_DIR` (pasta de plugins), `SHEAR_PLUGIN_CATALOG` e `SHEAR_UPDATE_MANIFEST` (aceitam `file:///...`).
- O shear aberto **trava** o `shear.exe` (o link falha com LNK1104). **O shear só tem uma instância**: abrir outro com um arquivo manda o
  arquivo para o que já está aberto. **Nunca feche todos os `shear.exe`** (é o programa do usuário); feche só o PID que você abriu.

## Armadilhas conhecidas
- Arquivos do repositório usam **CRLF**. Edições por script: normalize `\r\n`→`\n`, edite e volte (os scripts Python de apoio fazem isso).
  No Bash, heredoc com acentos/aspas pode corromper o texto: para scripts com acentos use a ferramenta de escrever arquivo.
- `slots`, `signals`, `emit` são macros do Qt: não use como nome de variável.
- Os executáveis do build ficam em `build/src/app/` (com `plugins/valorant/` ao lado, copiado a cada build).
- `crash.log` costuma ter uma falha 0xC0000005 **ao fechar** o programa (já existia antes dos plugins); falha nova durante o uso é problema.
- Para ver a janela: `PrintWindow` do próprio processo (não capture a tela toda: pega Discord, jogo etc.).
- Dados do usuário em `%LOCALAPPDATA%\Shear\Shear\`: `metrics.log` (tempos de abertura), `crash.log`, `clipmeta.json`, `valorant/`.
  Plugins instalados em `%LOCALAPPDATA%\Shear\plugins\<id>\` (com `installed.json`: SHA-256 conferido a cada abertura).

## Prioridades de desempenho
Abrir rápido e **não pesar com o jogo aberto**: nada de trabalho pesado antes do primeiro quadro (`deferredInit` roda depois de a janela
pintar), tudo de rede/disco em segundo plano com timeout curto, sem polling nem thread permanente. Compare o `metrics.log`
("janela pintada" ≈ 230 ms, "primeiro frame" ≈ 210 ms) antes e depois de mexer na abertura.

## Segurança e privacidade
- Plugin do Valorant: **só GET, só leitura**; certificado autoassinado aceito apenas para `127.0.0.1`. Senha do lockfile e tokens
  ficam só na memória: **nunca** em disco, log, `metrics.log` ou `crash.log`.
- Plugins e atualizações: cada arquivo baixado é conferido por **SHA-256** (e tamanho) antes de ser usado; falhou = nada é instalado.
  Caminhos do catálogo/manifesto nunca podem sair da pasta (`..`, absolutos, `plugins/`, ocultos, `.old` são recusados).

## Publicar (só a pedido)
O repositório e a branch de publicação estão nas constantes `kDefaultCatalogUrl` (`src/app/PluginInstaller.cpp`) e `kDefaultManifestUrl`
(`src/app/Updater.cpp`). O app lê `plugins/catalog.json` e `updates/manifest.json` direto de lá (raw) e baixa os arquivos das Releases
(`v<versão>`, `plugin-<id>-<versão>`).
1. `cmake --build build` (suba `project(Shear VERSION ...)` no `CMakeLists.txt` para uma versão nova do shear).
2. `scripts\make-installer.ps1` → `dist\shear-setup.exe` (sem plugins; leva `tls\`, necessário para HTTPS).
3. `scripts\make-update-manifest.ps1 -Notes "..."` → `updates\manifest.json` e, em `dist\update\v<versão>\`, só os arquivos alterados.
4. `scripts\make-plugin-package.ps1 -Id valorant` → `dist\plugins\valorant\` e `plugins\catalog.json` (copia descrição e "Saiba mais" do `plugin.json`).
5. Enviar os arquivos como assets da Release, commitar manifesto/catálogo e dar push.

## Informações pessoais ficam fora do repositório
Este arquivo é público. Dados do dono (identidade do Git, caminhos e clipes de teste desta máquina, contas) vão em `CLAUDE.local.md`,
que está no `.gitignore` e o Claude Code lê automaticamente. Não coloque e-mails, usuários, tokens ou caminhos pessoais aqui.
