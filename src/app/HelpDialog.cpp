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
        {"Reproduzir", {
            {"Espaço · clique no vídeo", "play / pause (uma animação rápida aparece sobre o vídeo)"},
            {"Espaço no fim do vídeo ou no O", "recomeça do I (ou do começo do vídeo, se não houver I)"},
            {"[ · -", "velocidade do preview mais lenta (0,25× a 3×; não muda o arquivo)"},
            {"] · +", "velocidade do preview mais rápida"},
            {"Backspace", "volta a 1×"},
            {"botão 1× (barra de cima)", "escolhe a velocidade num menu"},
        }},
        {"Pular no tempo (sem pausar)", {
            {"← → · A D", "1 s para trás / frente"},
            {"Shift + ← → · A D", "0,3 s (passo fino)"},
            {"Ctrl + ← → · A D", "5 s"},
            {"Ctrl + Shift + ← → · A D", "15 s"},
            {"Alt + ← → · , .", "1 quadro (pausa para mostrar o quadro)"},
            {"J · L", "5 s para trás / frente"},
            {"Home · End", "início / fim do vídeo"},
            {"0 a 9", "pula para 0% a 90% do vídeo (como no YouTube)"},
            {"Q · E", "vai para o ponto I / O (o E, com o vídeo rodando, pausa em cima do O)"},
        }},
        {"Volume e imagem", {
            {"↑ ↓ · W S · roda sobre o vídeo", "volume do preview (não muda o arquivo)"},
            {"Ctrl + roda sobre o vídeo", "zoom no vídeo, até 8×"},
            {"arrastar (com zoom)", "move a imagem"},
            {"Ctrl + 0", "tira o zoom (1×)"},
        }},
    };

    const std::vector<Section> cutting = {
        {"Marcar o trecho que fica", {
            {"I · Z", "marca o início (no modo Rápido encaixa no keyframe e o vídeo vai para lá)"},
            {"O · C", "marca o fim; a reprodução para ao chegar nele"},
            {"arrastar as marcas azul / vermelha", "ajusta início / fim direto na timeline"},
            {"Ctrl + Z", "desfaz (marcas, trechos apagados e volume do mic, um passo de cada vez)"},
            {"Ctrl + Y · Ctrl + Shift + Z", "refaz"},
        }},
        {"Apagar um trecho do meio", {
            {"X", "1º X = onde o trecho a apagar começa; 2º X = onde termina"},
            {"Esc", "cancela o trecho que estava sendo marcado"},
            {"botão direito no trecho", "traz o trecho de volta"},
            {"(automático)", "o preview pula por cima dele; na exportação o que sobra é emendado e o modo vira Exato"},
        }},
        {"Modo de corte e exportação", {
            {"Rápido", "corte instantâneo, sem recodificar; o início encaixa nos keyframes"},
            {"Exato", "corta no quadro escolhido (recodifica o vídeo, demora um pouco mais)"},
            {"Enter", "exporta"},
            {"Copiar (ícone)", "copia o clipe para a área de transferência (Ctrl+V onde quiser colar)"},
            {"Manter clipe", "guarda o clipe; com Shift o botão vira “Manter e apagar original”"},
            {"Apagar original", "manda só o vídeo original para a Lixeira (aparece depois de Manter)"},
            {"Apagar original e clipe", "apaga os dois (confirma antes)"},
            {"nome do arquivo (embaixo)", "abre a pasta do clipe"},
        }},
    };

    const std::vector<Section> mouse = {
        {"Timeline", {
            {"clicar / arrastar", "vai para aquele ponto do vídeo"},
            {"arrastar e puxar o mouse para baixo", "régua ampliada para ajustar com precisão (4 níveis; quanto mais baixo, mais zoom)"},
            {"roda do mouse", "pula como as setas: 1 s por clique (Shift 0,3 s, Ctrl 5 s, Ctrl+Shift 15 s, Alt 1 quadro)"},
            {"botão direito num trecho apagado", "traz o trecho de volta"},
        }},
        {"Vídeo", {
            {"clique", "play / pause"},
            {"roda", "volume"},
            {"Ctrl + roda", "zoom; depois arraste para mover a imagem"},
        }},
        {"Áudio (painel à direita)", {
            {"Desktop / Mic", "liga ou desliga cada faixa: o que está ligado você ouve e vai no arquivo"},
            {"fader do Mic", "volume do microfone (−30 a +30 dB); clique no valor para zerar"},
            {"alto-falante (1 faixa só)", "liga ou desliga o som do vídeo"},
        }},
    };

    const std::vector<Section> general = {
        {"Janela e arquivos", {
            {"F1", "abre esta ajuda"},
            {"← Clipes", "volta à lista de clipes (o clipe não mantido é apagado)"},
            {"engrenagem", "Configurações"},
            {"arrastar um vídeo para a janela", "abre o vídeo"},
            {"Cortar com shear (botão direito num vídeo)", "abre o vídeo no shear; ative em Configurações → Menu do Windows"},
        }},
        {"Configurações", {
            {"Modo de corte padrão", "Rápido ou Exato"},
            {"Pasta dos clipes mantidos / do ShadowPlay", "onde guardar e onde procurar os clipes"},
            {"Placa de vídeo no preview", "deixa o preview mais leve para o processador"},
            {"Som ao arrastar", "ouvir o áudio enquanto arrasta na timeline"},
            {"Suavizar as emendas", "fade rápido no vídeo e crossfade no áudio nos cortes do meio (ligado por padrão)"},
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
