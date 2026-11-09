#include "NavKeys.h"
#include <QKeyCombination>
#include <QKeySequence>
#include <QShortcut>
#include <utility>

namespace shear {

static void add(QWidget *parent, const QKeySequence &seq, const NavAction &a,
                const std::function<void(const NavAction &)> &onAction)
{
    auto *s = new QShortcut(seq, parent);
    QObject::connect(s, &QShortcut::activated, parent, [a, onAction] { onAction(a); });
}

void registerNavShortcuts(QWidget *parent, const std::function<void(const NavAction &)> &onAction)
{
    using Kind = NavAction::Kind;
    const auto frame = [](int dir) { return NavAction{Kind::StepFrame, 0.0, dir}; };
    const auto jump = [](double sec) { return NavAction{Kind::JumpSeconds, sec, 0}; };

    add(parent, QKeySequence(Qt::Key_Comma), frame(-1), onAction);
    add(parent, QKeySequence(Qt::Key_Period), frame(+1), onAction);

    for (const auto &kd : {std::pair<Qt::Key, int>{Qt::Key_Left, -1}, {Qt::Key_Right, +1}, {Qt::Key_A, -1}, {Qt::Key_D, +1}}) {
        const Qt::Key key = kd.first;
        const int dir = kd.second;
        const auto seq = [key](Qt::KeyboardModifiers m) { return QKeySequence(QKeyCombination(m, key)); };
        add(parent, seq(Qt::NoModifier), jump(dir * kJumpPlain), onAction);
        add(parent, seq(Qt::ShiftModifier), jump(dir * kJumpShift), onAction);
        add(parent, seq(Qt::ControlModifier), jump(dir * kJumpCtrl), onAction);
        add(parent, seq(Qt::ControlModifier | Qt::ShiftModifier), jump(dir * kJumpCtrlShift), onAction);
        add(parent, seq(Qt::AltModifier), frame(dir), onAction);
    }
}

} // namespace shear
