#pragma once

namespace shear::crashlog {

// Se o app quebrar (acesso inválido etc.), grava em %LOCALAPPDATA%\Shear\Shear\crash.log o tipo da falha e a pilha de
// chamadas (com nomes de função e linha quando o .pdb está ao lado do exe). Não interfere no uso normal.
void install();

} // namespace shear::crashlog
