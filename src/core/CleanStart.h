#pragma once
#include <vector>

namespace shear {

// Vídeo com o começo corrompido (ex.: clipe do ShadowPlay gravado com o disco/encoder falhando): o decodificador só
// consegue entregar uma fração dos quadros. Dados os instantes dos quadros que decodificaram BEM, devolve a partir de
// quando o vídeo está sadio. Um segundo é "ruim" se tiver menos que metade dos quadros esperados (fps); só vale uma
// sequência de 3 segundos ruins seguidos (quedas isoladas são normais e não contam). Devolve o fim da ÚLTIMA sequência
// ruim, em segundos (0 = nada para pular; também 0 se a sequência ruim vai até o fim: não há o que aproveitar).
double firstCleanTime(const std::vector<double> &okFrames, double fps, double duration);

} // namespace shear
