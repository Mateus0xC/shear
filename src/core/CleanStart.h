#pragma once
#include <vector>

namespace shear {

// Vídeo com o começo corrompido (ex.: clipe do ShadowPlay gravado com o disco/encoder falhando): o decodificador só
// consegue entregar uma fração dos quadros. Um segundo é "ruim" se tiver menos que metade dos quadros esperados (fps);
// só vale uma sequência de 3 segundos ruins seguidos (quedas isoladas são normais e não contam).
//
// counts[s] = quadros bons no segundo s. Devolve o segundo em que o vídeo passa a ser sadio: o fim da ÚLTIMA sequência
// ruim (0 = nada para pular; também 0 se a sequência ruim vai até o fim da contagem: não há o que aproveitar).
// minRun = quantas entradas ruins seguidas contam como dano (3 quando cada entrada é 1 s).
double firstCleanSecond(const std::vector<int> &counts, double fps, int minRun = 3);

// O mesmo, a partir dos instantes dos quadros que decodificaram bem (duration em segundos).
double firstCleanTime(const std::vector<double> &okFrames, double fps, double duration);

} // namespace shear
