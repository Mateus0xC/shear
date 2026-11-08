// Verifica o mapa de teclas de navegação simulando as teclas (e os modificadores) dentro do Qt, numa janela fora da
// tela: não usa o teclado nem as janelas do usuário.
#include "NavKeys.h"
#include <QtTest>
#include <vector>

using namespace shear;
using Kind = NavAction::Kind;

class NavKeysTest : public QObject {
    Q_OBJECT
private slots:
    void keys_map_to_the_expected_actions()
    {
        QWidget w;
        std::vector<NavAction> got;
        registerNavShortcuts(&w, [&](const NavAction &a) { got.push_back(a); });
        w.show();
        QVERIFY(QTest::qWaitForWindowActive(&w));

        struct Case { Qt::Key key; Qt::KeyboardModifiers mods; Kind kind; double seconds; int dir; };
        const Qt::KeyboardModifiers none, sh = Qt::ShiftModifier, ct = Qt::ControlModifier,
                                    cs = Qt::ControlModifier | Qt::ShiftModifier, al = Qt::AltModifier;
        const Case cases[] = {
            // 1 s
            {Qt::Key_Right, none, Kind::JumpSeconds, +1, 0}, {Qt::Key_Left, none, Kind::JumpSeconds, -1, 0},
            {Qt::Key_D, none, Kind::JumpSeconds, +1, 0},     {Qt::Key_A, none, Kind::JumpSeconds, -1, 0},
            // Shift = 5 s
            {Qt::Key_Right, sh, Kind::JumpSeconds, +5, 0},   {Qt::Key_Left, sh, Kind::JumpSeconds, -5, 0},
            {Qt::Key_D, sh, Kind::JumpSeconds, +5, 0},       {Qt::Key_A, sh, Kind::JumpSeconds, -5, 0},
            // Ctrl = 15 s
            {Qt::Key_Right, ct, Kind::JumpSeconds, +15, 0},  {Qt::Key_Left, ct, Kind::JumpSeconds, -15, 0},
            {Qt::Key_D, ct, Kind::JumpSeconds, +15, 0},      {Qt::Key_A, ct, Kind::JumpSeconds, -15, 0},
            // Ctrl+Shift = 1 min
            {Qt::Key_Right, cs, Kind::JumpSeconds, +60, 0},  {Qt::Key_Left, cs, Kind::JumpSeconds, -60, 0},
            {Qt::Key_D, cs, Kind::JumpSeconds, +60, 0},      {Qt::Key_A, cs, Kind::JumpSeconds, -60, 0},
            // Alt = 1 quadro
            {Qt::Key_Right, al, Kind::StepFrame, 0, +1},     {Qt::Key_Left, al, Kind::StepFrame, 0, -1},
            {Qt::Key_D, al, Kind::StepFrame, 0, +1},         {Qt::Key_A, al, Kind::StepFrame, 0, -1},
            // , e . = 1 quadro
            {Qt::Key_Period, none, Kind::StepFrame, 0, +1},  {Qt::Key_Comma, none, Kind::StepFrame, 0, -1},
        };
        for (const Case &c : cases) {
            got.clear();
            QTest::keyClick(&w, c.key, c.mods);
            QVERIFY2(got.size() == 1, qPrintable(QString("tecla %1 com modificadores %2: %3 ações")
                                                     .arg(int(c.key)).arg(int(c.mods)).arg(got.size())));
            QCOMPARE(int(got[0].kind), int(c.kind));
            QCOMPARE(got[0].seconds, c.seconds);
            QCOMPARE(got[0].dir, c.dir);
        }
    }
};

QTEST_MAIN(NavKeysTest)
#include "test_navkeys.moc"
