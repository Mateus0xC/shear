#pragma once
#include <QString>

namespace shear {

// Copia o arquivo para a área de transferência como CF_HDROP (Ctrl+V no Discord/Explorer).
void copyFileToClipboard(const QString &path);
// Abre o Explorer com o arquivo selecionado.
void revealInExplorer(const QString &path);

// Menu de contexto "Cortar com Shear" em HKCU (sem administrador), registrado POR EXTENSÃO
// (SystemFileAssociations\.mp4, .mkv, ...): o Explorer reconhece esse local; o do tipo "video" não aparecia.
bool contextMenuInstalled();
bool installContextMenu(const QString &exePath);
bool removeContextMenu();
// Quem instalou o menu na versão antiga (local que não funcionava) é migrado para o local novo.
void migrateLegacyContextMenu(const QString &exePath);

} // namespace shear
