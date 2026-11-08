#pragma once
#include <QString>

namespace shear {

// Copia o arquivo para a área de transferência como CF_HDROP (Ctrl+V no Discord/Explorer).
void copyFileToClipboard(const QString &path);
// Abre o Explorer com o arquivo selecionado.
void revealInExplorer(const QString &path);

// Menu de contexto "Cortar com Shear" em HKCU (sem administrador).
bool contextMenuInstalled();
bool installContextMenu(const QString &exePath);
bool removeContextMenu();

} // namespace shear
