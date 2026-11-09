"""Gera a logo (resources/logo.svg, logo.png e shear.ico): um quadro de vídeo dividido em duas metades por um corte
inclinado e reto (bordas paralelas, cantos do corte vivos); só os cantos de fora são arredondados. Uso: python scripts/make-logo.py [pasta de saída]  (precisa do Pillow)."""
from PIL import Image, ImageDraw
import math, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "D:/Coding/shear/resources"
COLOR = (0, 128, 250)

# Geometria (quadro 1024 x 1024). Cada peça é um trapézio com topo e base HORIZONTAIS: a de cima tem o lado esquerdo
# VERTICAL e a de baixo o lado direito VERTICAL; o lado que dá para o corte é inclinado, com o MESMO ângulo nas duas
# (as bordas do corte ficam paralelas, então o vão é igual de ponta a ponta). As peças não são idênticas: tamanhos um
# pouco diferentes deixam o conjunto mais orgânico.
ANGLE = 26.0          # inclinação do corte em relação à vertical (graus)
K = math.tan(math.radians(ANGLE))
GAP = 108             # vão entre as peças, medido na horizontal
OUTER = 40            # raio dos cantos de fora (os do corte ficam vivos)

# peça de cima (esquerda): borda esquerda x=TOP_X0, topo y=TOP_Y0, altura TOP_H, largura do topo TOP_W
TOP_X0, TOP_Y0, TOP_H, TOP_W = 80, 206, 470, 560
# peça de baixo (direita): borda direita x=BOT_X1, topo y=BOT_Y0, base y=BOT_Y1
BOT_X1, BOT_Y0, BOT_Y1 = 944, 350, 818

tr = (TOP_X0 + TOP_W, TOP_Y0)                                  # canto de cima à direita da peça de cima
br = (tr[0] - K * TOP_H, TOP_Y0 + TOP_H)                       # canto de baixo à direita (fim do corte)
x_cut = lambda y: tr[0] - K * (y - TOP_Y0)                     # x da borda do corte da peça de cima no nível y
tl2 = (x_cut(BOT_Y0) + GAP, BOT_Y0)                            # canto de cima à esquerda da peça de baixo
bl2 = (tl2[0] - K * (BOT_Y1 - BOT_Y0), BOT_Y1)                 # canto de baixo à esquerda da peça de baixo

LEFT = [(TOP_X0, TOP_Y0, OUTER), (tr[0], tr[1], 0), (br[0], br[1], 0), (TOP_X0, TOP_Y0 + TOP_H, OUTER)]
RIGHT = [(tl2[0], tl2[1], 0), (BOT_X1, BOT_Y0, OUTER), (BOT_X1, BOT_Y1, OUTER), (bl2[0], bl2[1], 0)]
CX, CY, SCALE = 512, 512, 1.0

def xf(v):
    return (CX + (v[0] - CX) * SCALE, CY + (v[1] - CY) * SCALE, v[2] * SCALE)

def rounded(vs):
    """Contorno com raio por vértice (0 = canto vivo). Devolve os pontos (PNG) e o path do SVG."""
    n = len(vs)
    pts, d = [], ""
    for i in range(n):
        p0, p1, p2 = vs[i - 1], vs[i], vs[(i + 1) % n]
        r = p1[2]
        def toward(a, b, dist):
            L = math.hypot(b[0] - a[0], b[1] - a[1])
            k = min(dist, L / 2) / L
            return (a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k)
        if r <= 0:
            pts.append((p1[0], p1[1]))
            d += ("M" if i == 0 else "L") + f"{p1[0]:.1f},{p1[1]:.1f} "
            continue
        a, b = toward(p1, p0, r), toward(p1, p2, r)
        d += ("M" if i == 0 else "L") + f"{a[0]:.1f},{a[1]:.1f} Q{p1[0]:.1f},{p1[1]:.1f} {b[0]:.1f},{b[1]:.1f} "
        for s in range(13):
            t = s / 12
            pts.append(((1 - t) ** 2 * a[0] + 2 * (1 - t) * t * p1[0] + t * t * b[0],
                        (1 - t) ** 2 * a[1] + 2 * (1 - t) * t * p1[1] + t * t * b[1]))
    return pts, d + "Z"

shapes = [rounded([xf(v) for v in poly]) for poly in (LEFT, RIGHT)]

def render(size):
    S = 4
    n = size * S
    k = n / 1024
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for pts, _ in shapes:
        d.polygon([(x * k, y * k) for x, y in pts], fill=COLOR + (255,))
    return im.resize((size, size), Image.LANCZOS)

render(512).save(f"{OUT}/logo.png")
sizes = [16, 24, 32, 48, 64, 128, 256]
imgs = [render(s) for s in sizes]
imgs[-1].save(f"{OUT}/shear.ico", format="ICO", sizes=[(s, s) for s in sizes], append_images=imgs[:-1])
paths = "\n".join(f'  <path d="{d}" fill="rgb{COLOR}"/>' for _, d in shapes)
open(f"{OUT}/logo.svg", "w", encoding="utf-8").write(
    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="256" height="256">\n{paths}\n</svg>\n')
print("logo gerada em", OUT)
