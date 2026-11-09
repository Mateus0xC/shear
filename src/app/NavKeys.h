#pragma once
#include <QWidget>
#include <functional>

namespace shear {

struct NavAction {
    enum class Kind { JumpSeconds, StepFrame } kind = Kind::JumpSeconds;
    double seconds = 0; // JumpSeconds: quanto pular (negativo = para trás)
    int dir = 0;        // StepFrame: -1 ou +1
};

// Quanto cada combinação pula (segundos). Shift = passo fino (menor que 1 s); Ctrl e Ctrl+Shift = passos grandes.
inline constexpr double kJumpPlain = 1.0, kJumpShift = 0.5, kJumpCtrl = 5.0, kJumpCtrlShift = 15.0;

// Atalhos de navegação (todos no `parent`, só valem com a janela ativa):
//   ← → ou A D          1 s            Shift  0,5 s          Ctrl  5 s           Ctrl+Shift  15 s
//   Alt + (← → ou A D)  1 quadro       , e .  1 quadro
// Cada atalho chama `onAction`; quem usa decide o que fazer (pular sem pausar, ou pausar e andar um quadro).
void registerNavShortcuts(QWidget *parent, const std::function<void(const NavAction &)> &onAction);

} // namespace shear
