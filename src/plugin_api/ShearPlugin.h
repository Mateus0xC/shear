#pragma once
// Interface pública dos plugins do shear (DLL de plugin do Qt, carregada em processo).
//
// Versão da API = MAIOR.MENOR.
//  - MAIOR muda quando algo existente deixa de funcionar: plugin e shear só se carregam com a MAIOR igual.
//  - MENOR sobe quando se ACRESCENTA algo (sempre no fim das interfaces, nunca no meio). O plugin declara em plugin.json
//    "apiMinor" (o que ele implementa) e "minHostApiMinor" (o que ele exige do shear): o shear só carrega se tiver essa menor e só
//    chama os métodos mais novos se o plugin os implementa. Assim plugin e shear não precisam ser da mesma versão.
#include <QDateTime>
#include <QKeySequence>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace shear::plugin {

inline constexpr int kApiMajor = 1;
inline constexpr int kApiMinor = 0;

// Clipe aberto no editor.
struct ClipContext {
    QString path;
    QDateTime created; // quando o arquivo foi criado
    qint64 size = 0;
};

// Texto clicável (ou fixo) ao lado do título: "· Ascent · Jett · 3k".
struct InfoChip {
    QString id;          // volta nas chamadas chipMenu/chipChosen
    QString text;        // vazio = mostra `placeholder` em itálico cinza
    QString placeholder;
    QString iconPath;
    bool editable = false; // clicável (abre o menu do plugin)
    QString tooltip;
};

// Opção de um menu de escolha. Com `bannerPath` o item é a imagem como fundo, com o nome por cima.
struct Choice {
    QString value;        // devolvido em chipChosen
    QString label;        // texto do item (vazio = usa `value`)
    QString iconPath;
    QString bannerPath;
    bool checked = false;
};
struct ChoiceMenu {
    QVector<Choice> items;
    QString moreTitle;            // título de um submenu com `moreItems` (ex.: "Outros modos")
    QVector<Choice> moreItems;
    QString clearTitle;           // "Limpar o mapa" (vazio = sem esse item)
    QString customTitle;          // "Outro…": pede um texto ao usuário (vazio = sem esse item)
    QString customPrompt;
    QString customCurrent;
};

// Marca na timeline (segundos do clipe aberto). `primary` = cor principal (kill); senão, a secundária (morte).
struct Marker {
    double t = 0;
    bool primary = true;
};

// Dados de uma linha da lista de clipes (devolvidos por rowInfos, de uma thread de fundo: tem de ser thread-safe).
struct RowQuery {
    QString path;
    qint64 size = 0;
};
struct RowInfo {
    QString iconPath;   // ícone à esquerda (ex.: agente)
    QString bannerPath; // faixa à direita (ex.: mapa)
    QString bannerText; // texto em cima da faixa
    QString tag;        // ex.: "5k"
    bool tagHot = false; // destaque (ex.: 5k ou mais)
};

// O que vai ser exportado (para o plugin dar nome e guardar dados).
struct ExportContext {
    QString clipPath;
    QDateTime clipCreated;
    double start = 0; // onde o arquivo exportado realmente começa (no Rápido, o keyframe) e termina, em segundos do clipe
    double end = 0;
    QVector<QPair<double, double>> gaps; // trechos do meio apagados
    double duration = 0;
};

struct Action {
    QString id;
    QString text;
    QKeySequence keys;
};

// O que o shear oferece ao plugin. Só pode ser chamado da thread da interface.
class IShearHost {
public:
    virtual ~IShearHost() = default;
    virtual int apiMinor() const = 0;                  // versão MENOR da API que este shear implementa
    virtual double position() const = 0;               // ponto de reprodução (s)
    virtual double duration() const = 0;               // duração do clipe (s; 0 até saber)
    virtual QString dataDir() const = 0;               // pasta de dados do shear (%LOCALAPPDATA%/Shear/Shear)
    virtual void log(const QString &line) = 0;         // metrics.log
    virtual void showStatus(const QString &text) = 0;  // mensagem na barra de status
    virtual void pushUndo() = 0;                       // chamar ANTES de mudar o estado que entra em saveState()
    virtual void setInfoChips(const QVector<InfoChip> &chips) = 0;
    virtual void setBackdrop(const QString &imagePath) = 0; // imagem de fundo (apagada) da barra de cima; "" tira
    virtual void setMarkers(const QVector<Marker> &markers) = 0;
    // Sugere I/O (segundos do clipe). O shear só aplica se você ainda não mexeu em I/O/trechos; encaixa no keyframe no modo
    // Rápido, evita o começo corrompido, começa a tocar no I e entra no desfazer. Pode ser chamado de novo (ex.: ao recalibrar).
    virtual void suggestRange(double start, double end) = 0;
    virtual void refreshList() = 0;                    // a lista de clipes precisa ser redesenhada
};

class IShearPlugin {
public:
    virtual ~IShearPlugin() = default;
    // --- API 1.0 ---
    virtual void init(IShearHost *host) = 0;
    virtual QVector<Action> actions() const = 0;            // atalhos do editor
    virtual void triggerAction(const QString &id) = 0;
    virtual void clipClosed() = 0;                          // vai abrir outro clipe (ou voltou à lista)
    virtual void clipOpened(const ClipContext &clip) = 0;   // depois do primeiro quadro
    virtual void durationChanged(double seconds) = 0;
    virtual ChoiceMenu chipMenu(const QString &chipId) = 0;
    virtual void chipChosen(const QString &chipId, const QString &value) = 0; // value vazio = limpar
    virtual QVector<RowInfo> rowInfos(const QVector<RowQuery> &rows) const = 0; // thread-safe
    virtual QString exportStem(const ExportContext &ctx) = 0;               // nome-base sem extensão; "" = deixa o shear nomear
    virtual void exportKept(const QString &finalPath, qint64 size) = 0;     // o clipe exportado foi mantido
    virtual QVariant saveState() const = 0;                 // estado que o Ctrl+Z desfaz (invalid = nenhum)
    virtual void restoreState(const QVariant &state) = 0;
    // --- métodos novos entram AQUI (sempre no fim), junto com um aumento de kApiMinor ---
};

} // namespace shear::plugin

Q_DECLARE_INTERFACE(shear::plugin::IShearPlugin, "br.shear.Plugin/1")
