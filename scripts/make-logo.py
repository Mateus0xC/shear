"""Gera a logo (resources/logo.svg, logo.png e shear.ico): um quadro de vídeo dividido em duas metades por um corte
inclinado e reto (bordas paralelas, cantos do corte vivos); só os cantos de fora são arredondados. Uso: python scripts/make-logo.py [pasta de saída]  (precisa do Pillow)."""
from PIL import Image, ImageDraw
import math, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "D:/Coding/shear/resources"
COLOR = (0, 128, 250)

# Vértices (x, y, raio) no quadro de 1024 x 1024. Raio 0 = canto vivo.
#  - cantos de fora: levemente arredondados
#  - todos os cantos do corte (no centro): vivos; as duas bordas do corte são paralelas, com um vão fino
#  - a metade de baixo desliza para baixo/direita em relação à de cima
OUTER = 24
LEFT = [(85, 210, OUTER), (611, 210, 0), (389, 668, 0), (85, 670, OUTER)]
RIGHT = [(639, 354, 0), (939, 354, OUTER), (939, 814, OUTER), (416, 814, 0)]
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
