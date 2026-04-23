"""
Rasteriza SVG -> PNG (transparencia preservada via tecnica white-to-alpha) e
gera C array LVGL 8.x (CF_TRUE_COLOR_ALPHA) via png_to_lvgl.py.

Uso: python svg_to_lvgl.py <input.svg> <output.c> <var_name> <target_width>

Requisitos: svglib, reportlab, Pillow. NAO usar cairocffi/cairosvg (necessitam DLLs).
Tecnica: oversample 4x, render com fundo branco, mapear branco -> alpha 0 preservando
intensidade da cor nos pixeis antialiased, downsample LANCZOS para target.
"""
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image
from reportlab.graphics import renderPM
from svglib.svglib import svg2rlg


def rasterize_svg_to_png(svg_path: str, png_path: str, target_w: int) -> None:
    over = 4
    big_w = target_w * over
    drawing = svg2rlg(svg_path)
    orig_w, orig_h = drawing.width, drawing.height
    aspect = orig_h / orig_w
    target_h = int(round(target_w * aspect))
    scale = big_w / orig_w
    drawing.width *= scale
    drawing.height *= scale
    drawing.scale(scale, scale)

    tmp = png_path + ".big.png"
    renderPM.drawToFile(drawing, tmp, fmt="PNG", bg=0xFFFFFF)

    im = Image.open(tmp).convert("RGBA")
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, _a = px[x, y]
            m = min(r, g, b)
            if m == 255:
                px[x, y] = (r, g, b, 0)
            else:
                new_a = 255 - m
                rr = max(0, int((r - (255 - new_a)) * 255 / new_a))
                gg = max(0, int((g - (255 - new_a)) * 255 / new_a))
                bb = max(0, int((b - (255 - new_a)) * 255 / new_a))
                px[x, y] = (min(255, rr), min(255, gg), min(255, bb), new_a)

    im_final = im.resize((target_w, target_h), Image.LANCZOS)
    im_final.save(png_path)
    try:
        os.remove(tmp)
    except OSError:
        pass
    print(f"Rasterizado {svg_path} -> {png_path} ({target_w}x{target_h})")


def main():
    if len(sys.argv) < 5:
        print(f"Uso: python {sys.argv[0]} <input.svg> <output.c> <var_name> <target_width>")
        sys.exit(1)
    svg = sys.argv[1]
    out_c = sys.argv[2]
    var = sys.argv[3]
    tw = int(sys.argv[4])

    tmp_png = Path(out_c).with_suffix(".tmp.png").as_posix()
    rasterize_svg_to_png(svg, tmp_png, tw)

    here = Path(__file__).resolve().parent
    subprocess.check_call([sys.executable, str(here / "png_to_lvgl.py"), tmp_png, out_c, var])

    try:
        os.remove(tmp_png)
    except OSError:
        pass
    print(f"Gerado: {out_c}")


if __name__ == "__main__":
    main()
