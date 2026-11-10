#include "HelpDialog.h"
#include <QDialogButtonBox>
#include <QPalette>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace shear {

namespace {

struct Row {
    const char *keys; // teclas / gesto (vários separados por " · ")
    const char *what;
};
struct Section {
    const char *title; // vazio = sem título
    std::vector<Row> rows;
};

// Uma aba = uma lista de seções; cada seção = linhas "teclas → o que faz".
QString html(const std::vector<Section> &sections, const QPalette &pal)
{
    // Teclas em "fichas": fundo e texto do próprio botão do tema (legível em tema claro ou escuro).
    const QString chipBg = pal.color(QPalette::Button).lighter(118).name();
    const QString chipFg = pal.color(QPalette::ButtonText).name();
    const QString muted = pal.color(QPalette::Disabled, QPalette::WindowText).name();
    QString h = "<style>td { padding: 4px 6px; } h3 { margin: 14px 0 4px 0; }</style>";
    for (const Section &s : sections) {
        if (*s.title)
            h += QString("<h3>%1</h3>").arg(QString::fromUtf8(s.title));
        h += "<table cellspacing='0' width='100%'>";
        for (const Row &r : s.rows) {
            QString chips;
            for (const QString &k : QString::fromUtf8(r.keys).split(" · ")) {
                if (!chips.isEmpty())
                    chips += QString(" <span style='color:%1'>ou</span> ").arg(muted);
                chips += QString("<span style='background-color:%1; color:%2; font-family:Consolas,monospace;'>&nbsp;<b>%3</b>&nbsp;</span>")
                             .arg(chipBg, chipFg, k.toHtmlEscaped());
            }
            h += QString("<tr><td width='38%'>%1</td><td>%2</td></tr>").arg(chips, QString::fromUtf8(r.what).toHtmlEscaped());
        }
        h += "</table>";
    }
    return h;
}

} // namespace

HelpDialog::HelpDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Atalhos e controles");
    resize(720, 600);
    const QPalette pal = palette();

    const std::vector<Section> playback = {
        {"Tocar", {
            {"Espaço · clique no vídeo", "Toca ou pausa"},
            {"[ · -", "Deixa o vídeo mais lento"},
            {"] · +", "Deixa o vídeo mais rápido"},
            {"Backspace", "Volta à velocidade normal"},
        }},
        {"Avançar e voltar", {
            {"← → · A D", "Volta ou avança 1 segundo"},
            {"Shift + ← → · A D", "Passo curto: 0,3 segundo"},
            {"Ctrl + ← → · A D", "Passo médio: 5 segundos"},
            {"Ctrl + Shift + ← → · A D", "Passo longo: 15 segundos"},
            {"Alt + ← → · , .", "Anda um quadro de cada vez"},
            {"Home · End", "Vai para o começo ou para o fim do vídeo"},
            {"0 a 9", "Vai direto para uma parte do vídeo: 0 é o começo, 5 é a metade, 9 é quase o fim"},
            {"Shift + Q · Shift + E", "Vai para o ponto I ou para o ponto O"},
            {"K", "Clipe do Valorant: pause no quadro de uma kill/morte e aperte K — as marcas desta partida se acertam para cair ali (fica guardado)"},
            {"Q · E", "Pula para a parada anterior ou seguinte: kills/mortes do Valorant e os pontos I e O (o que já está em cima do ponto atual é ignorado)"},
        }},
        {"Volume e zoom", {
            {"↑ ↓ · W S · roda do mouse sobre o vídeo", "Aumenta ou diminui o volume"},
            {"Ctrl + roda do mouse sobre o vídeo", "Aproxima ou afasta a imagem"},
            {"Arrastar o vídeo (com zoom)", "Move a imagem"},
            {"Ctrl + 0", "Volta ao tamanho normal"},
        }},
    };

    const std::vector<Section> cutting = {
        {"Escolher o pedaço que fica", {
            {"I · Z", "Marca onde o clipe começa"},
            {"O · C", "Marca onde o clipe termina"},
            {"Ctrl + Z", "Desfaz a última ação"},
            {"Ctrl + Y · Ctrl + Shift + Z", "Refaz o que foi desfeito"},
        }},
        {"Tirar um pedaço do meio", {
            {"X", "Aperte uma vez onde o pedaço a tirar começa e outra vez onde ele termina"},
            {"Esc", "Cancela a marcação"},
            {"Botão direito no pedaço tirado", "Devolve o pedaço ao vídeo"},
        }},
        {"Salvar o clipe", {
            {"Enter", "Exporta o clipe"},
            {"Rápido", "Corta na hora; o começo se ajusta ao ponto mais próximo"},
            {"Exato", "Corta exatamente onde você marcou (demora um pouco mais)"},
            {"Até 20 MB", "Comprime o vídeo para o arquivo ficar com até 20 MB (a qualidade baixa um pouco, e em trechos longos a resolução também)"},
            {"Ícone de copiar", "Copia o clipe para você colar em outro lugar (Ctrl + V)"},
            {"Manter clipe", "Guarda o clipe. Segure Shift para guardar e já apagar o vídeo original"},
            {"Apagar original", "Manda o vídeo original para a Lixeira"},
            {"Apagar original e clipe", "Apaga os dois"},
            {"Nome do arquivo (embaixo)", "Abre a pasta onde o clipe está"},
        }},
    };

    const std::vector<Section> mouse = {
        {"Barra de tempo (embaixo do vídeo)", {
            {"Clicar ou arrastar", "Vai para aquele ponto do vídeo"},
            {"Arrastar e puxar o mouse para baixo", "Mostra a barra ampliada, para escolher o ponto com mais precisão"},
            {"Roda do mouse", "Volta ou avança, como as setas"},
            {"Botão direito num pedaço tirado", "Devolve o pedaço ao vídeo"},
        }},
        {"Vídeo", {
            {"Clique", "Toca ou pausa"},
            {"Roda do mouse", "Volume"},
            {"Ctrl + roda do mouse", "Aproxima ou afasta a imagem; depois é só arrastar para mover"},
        }},
        {"Som (painel à direita)", {
            {"Desktop · Mic", "Liga ou desliga cada som. O que está ligado você ouve e vai para o clipe"},
            {"Barra do Mic", "Volume do microfone; clique no número para voltar ao normal"},
            {"Alto-falante (quando o vídeo tem um som só)", "Liga ou desliga o som"},
        }},
    };

    const std::vector<Section> general = {
        {"Janela e arquivos", {
            {"F1", "Abre esta ajuda"},
            {"← Clipes", "Volta para a lista de clipes (o clipe que você não guardou é apagado)"},
            {"Engrenagem", "Abre as Configurações"},
            {"Mapa · Agente · kills (ao lado do título)", "O que o jogo não identificou fica clicável: clique para escolher à mão"},
            {"comentário (canto direito)", "Texto livre opcional do clipe: clique, digite e aperte Enter (Esc descarta)"},
            {"Mapa · Agente (ao lado do título)", "Clipes do Valorant: aparece sozinho se o jogo estiver aberto (ou se o clipe já foi identificado antes)"},
            {"Arrastar um vídeo para a janela", "Abre o vídeo"},
            {"Cortar com shear (botão direito num vídeo)", "Abre o vídeo no shear; para ativar, vá em Configurações → Menu do Windows"},
        }},
        {"Configurações", {
            {"Modo de corte padrão", "Escolhe se o corte começa em Rápido ou em Exato"},
            {"Pasta dos clipes mantidos", "Onde os clipes guardados são salvos"},
            {"Pasta dos vídeos", "Onde o shear procura seus clipes"},
            {"Placa de vídeo no preview", "Deixa a reprodução mais leve para o computador"},
            {"Som ao arrastar", "Você ouve o som enquanto arrasta na barra de tempo"},
            {"Sempre comprimir para até 20 MB", "Cada vídeo já começa com a opção “Até 20 MB” ligada"},
            {"Suavizar as emendas", "Quando você tira um pedaço do meio, a junção fica suave (ligado por padrão)"},
        }},
    };

    auto *tabs = new QTabWidget;
    auto addTab = [&](const QString &title, const std::vector<Section> &s) {
        auto *b = new QTextBrowser;
        b->setFrameShape(QFrame::NoFrame);
        b->setHtml(html(s, pal));
        tabs->addTab(b, title);
    };
    addTab("Reprodução", playback);
    addTab("Cortar e exportar", cutting);
    addTab("Mouse e painéis", mouse);
    addTab("Janela e arquivos", general);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    bb->button(QDialogButtonBox::Close)->setText("Fechar");
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *root = new QVBoxLayout(this);
    root->addWidget(tabs, 1);
    root->addWidget(bb);
}

} // namespace shear
