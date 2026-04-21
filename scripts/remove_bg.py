"""
Remove fundo de um PNG usando flood-fill a partir das bordas + suavizacao de anti-aliasing.
Uso: python remove_bg.py <input.png> <output.png> [threshold=240]

threshold: tolerancia de "branco" para o flood-fill (pixels com R,G,B >= threshold
sao tratados como fundo se ligados as bordas).
Anti-aliasing: pixels na fronteira com alpha parcial sao suavizados.
"""
import sys
from pathlib import Path
from collections import deque

try:
    from PIL import Image
except ImportError:
    print("Pillow nao encontrado. Instale com: pip install Pillow")
    sys.exit(1)


def is_bg(r: int, g: int, b: int, threshold: int) -> bool:
    return r >= threshold and g >= threshold and b >= threshold


def remove_bg(input_path: str, output_path: str, threshold: int = 230) -> None:
    img = Image.open(input_path).convert("RGBA")
    pixels = img.load()
    w, h = img.size

    visited = [[False] * h for _ in range(w)]
    queue = deque()

    # Semear flood-fill a partir de todas as bordas
    for x in range(w):
        for y in [0, h - 1]:
            r, g, b, a = pixels[x, y]
            if not visited[x][y] and is_bg(r, g, b, threshold):
                visited[x][y] = True
                queue.append((x, y))
    for y in range(h):
        for x in [0, w - 1]:
            r, g, b, a = pixels[x, y]
            if not visited[x][y] and is_bg(r, g, b, threshold):
                visited[x][y] = True
                queue.append((x, y))

    removed = 0
    while queue:
        cx, cy = queue.popleft()
        pixels[cx, cy] = (pixels[cx, cy][0], pixels[cx, cy][1], pixels[cx, cy][2], 0)
        removed += 1
        for nx, ny in [(cx-1,cy),(cx+1,cy),(cx,cy-1),(cx,cy+1)]:
            if 0 <= nx < w and 0 <= ny < h and not visited[nx][ny]:
                r, g, b, a = pixels[nx, ny]
                if is_bg(r, g, b, threshold):
                    visited[nx][ny] = True
                    queue.append((nx, ny))

    # Segunda passagem: ilhas brancas interiores nao alcancadas pelo flood-fill
    island_thr = max(threshold, 245)
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            if a > 0 and r >= island_thr and g >= island_thr and b >= island_thr:
                pixels[x, y] = (r, g, b, 0)
                removed += 1

    # Suavizar anti-aliasing: vizinhos de pixels transparentes com luminosidade alta
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            r, g, b, a = pixels[x, y]
            if a == 0:
                continue
            has_transparent_neighbor = any(
                pixels[nx, ny][3] == 0
                for nx, ny in [(x-1,y),(x+1,y),(x,y-1),(x,y+1)]
                if 0 <= nx < w and 0 <= ny < h
            )
            if has_transparent_neighbor:
                lum = (r * 299 + g * 587 + b * 114) // 1000
                if lum > 180:
                    new_a = int(a * (255 - lum) / 75)
                    pixels[x, y] = (r, g, b, max(0, new_a))

    img.save(output_path)
    total = w * h
    print(f"{Path(input_path).name} -> {Path(output_path).name}")
    print(f"Dimensoes: {w}x{h}  |  Pixels removidos: {removed}/{total} ({100*removed//total}%)")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"Uso: python {sys.argv[0]} <input.png> <output.png> [threshold=230]")
        sys.exit(1)
    thr = int(sys.argv[3]) if len(sys.argv) > 3 else 230
    remove_bg(sys.argv[1], sys.argv[2], thr)
