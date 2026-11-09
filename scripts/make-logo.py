"""Gera a logo (resources/logo.svg, logo.png e shear.ico): a letra S, desenhada do zero (sem fonte), cortada por uma
diagonal em duas metades que deslizam um pouco. Uso: python scripts/make-logo.py [pasta de saída]  (precisa do Pillow)."""
from PIL import Image, ImageDraw, ImageChops
import math, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "D:/Coding/shear/resources"
COLOR = (0, 128, 250)

# --- cota do desenho (quadro 1024 x 1024) ---
ANGLE = 20.0           # inclinação do corte em relação à vertical (graus), no sentido "/"
GAP = 30               # vão entre as metades, medido na horizontal
SLIDE = 26             # quanto cada metade desliza (esquerda sobe, direita desce)

# --- o S, desenhado do zero: duas elipses empilhadas, percorridas como uma linha única e engrossadas ---
RX, RY, W = 205, 168, 156       # raios (horizontal/vertical) de cada "barriga" do S e a espessura do traço
START_TOP, END_BOTTOM = 8, -172  # onde o S começa (em cima à direita) e termina (embaixo à esquerda), em graus

def centerline():
    pts = []
    cx = 512
    top_cy, bot_cy = 512 - RY, 512 + RY
    for i in range(0, 121):                      # barriga de cima: de START_TOP até 270 (o ponto de ligação)
        th = math.radians(START_TOP + (270 - START_TOP) * i / 120)
        pts.append((cx + RX * math.cos(th), top_cy - RY * math.sin(th)))
    for i in range(1, 121):                      # barriga de baixo: de 90 (ligação) descendo até END_BOTTOM
        th = math.radians(90 + (END_BOTTOM - 90) * i / 120)
        pts.append((cx + RX * math.cos(th), bot_cy - RY * math.sin(th)))
    return pts

def stroke_polygon(pts, w):
    left, right = [], []
    for i, p in enumerate(pts):
        q0 = pts[max(i - 1, 0)]; q1 = pts[min(i + 1, len(pts) - 1)]
        dx, dy = q1[0] - q0[0], q1[1] - q0[1]
        L = math.hypot(dx, dy) or 1
        nx, ny = -dy / L, dx / L
        left.append((p[0] + nx * w / 2, p[1] + ny * w / 2))
        right.append((p[0] - nx * w / 2, p[1] - ny * w / 2))
    return left + right[::-1]

frame_contours = [stroke_polygon(centerline(), W)]

# --- a reta do corte: passa pelo centro, inclinada em "/" ---
t = math.tan(math.radians(ANGLE))
def line_x(y, shift):             # x da reta no nível y (shift desloca a reta na horizontal)
    return 512 + shift - (y - 512) * t

def piece_polygons(sign):
    """Meio-plano da esquerda (sign=-1) ou da direita (sign=+1), recuado GAP/2 do corte."""
    shift = sign * GAP / 2
    big = 2000
    a = (line_x(-big, shift), -big); b = (line_x(big, shift), big)
    far = -big * 2 if sign < 0 else big * 3
    return [a, b, (far, big), (far, -big)]

pieces = [(-1, (0, -SLIDE)), (+1, (0, +SLIDE))]   # (lado, deslocamento da metade)

def render(size):
    S = 4
    n = size * S
    f = n / 1024
    result = Image.new("L", (n, n), 0)
    for sign, (dx, dy) in pieces:
        glyph = Image.new("L", (n, n), 0)
        gd = ImageDraw.Draw(glyph)
        # o contorno do S desenhado já deslocado; furos (contra-formas) saem pelo preenchimento par-ímpar
        for c in frame_contours:
            gd.polygon([((x + dx) * f, (y + dy) * f) for x, y in c], fill=255)
        # S tem um contorno só; se houver mais de um (furo), subtrai do anterior
        clip = Image.new("L", (n, n), 0)
        ImageDraw.Draw(clip).polygon([(x * f, y * f) for x, y in piece_polygons(sign)], fill=255)
        result = ImageChops.lighter(result, ImageChops.multiply(glyph, clip))
    rgba = Image.new("RGBA", (n, n), COLOR + (0,))
    rgba.putalpha(result)
    return rgba.resize((size, size), Image.LANCZOS)

render(512).save(f"{OUT}/logo.png")
sizes = [16, 24, 32, 48, 64, 128, 256]
imgs = [render(s) for s in sizes]
imgs[-1].save(f"{OUT}/shear.ico", format="ICO", sizes=[(s, s) for s in sizes], append_images=imgs[:-1])

# --- SVG: o mesmo S duas vezes, cada cópia recortada pelo seu meio-plano ---
def d_path():
    parts = []
    for c in frame_contours:
        parts.append("M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in c) + " Z")
    return " ".join(parts)
clips = ""
body = ""
for idx, (sign, (dx, dy)) in enumerate(pieces):
    poly = " ".join(f"{x:.1f},{y:.1f}" for x, y in piece_polygons(sign))
    clips += f'    <clipPath id="c{idx}"><polygon points="{poly}"/></clipPath>\n'
    body += f'  <g clip-path="url(#c{idx})"><path transform="translate({dx},{dy})" d="{d_path()}"/></g>\n'
open(f"{OUT}/logo.svg", "w", encoding="utf-8").write(
    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="256" height="256">\n'
    f'  <defs>\n{clips}  </defs>\n  <g fill="rgb{COLOR}">\n{body}  </g>\n</svg>\n')
print("logo gerada em", OUT)
