"""
Converte um PNG para C array no formato LVGL 8.x (CF_TRUE_COLOR_ALPHA, RGB565 + alpha).
Uso: python png_to_lvgl.py <input.png> <output.c> <var_name> [max_width]
"""
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow nao encontrado. Instale com: pip install Pillow")
    sys.exit(1)


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def convert(input_path: str, output_path: str, var_name: str, max_width: int = 0):
    img = Image.open(input_path).convert("RGBA")
    w, h = img.size
    print(f"Imagem original: {w}x{h}")

    if max_width > 0 and w > max_width:
        ratio = max_width / w
        new_h = int(h * ratio)
        img = img.resize((max_width, new_h), Image.LANCZOS)
        w, h = img.size
        print(f"Redimensionada para: {w}x{h}")

    pixels = img.load()
    data_size = w * h * 3  # 2 bytes cor + 1 byte alpha
    print(f"Tamanho dos dados: {data_size} bytes ({data_size / 1024:.1f} KB)")

    upper = var_name.upper()

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(f"/**\n")
        f.write(f" * @file {Path(output_path).name}\n")
        f.write(f" * @brief Logo AFR Solucoes Inteligentes — C array LVGL 8.x (gerado por png_to_lvgl.py).\n")
        f.write(f" * Formato: CF_TRUE_COLOR_ALPHA (RGB565 little-endian + alpha), {w}x{h} px.\n")
        f.write(f" */\n\n")
        f.write(f"#ifdef __has_include\n")
        f.write(f"#if __has_include(\"lvgl.h\")\n")
        f.write(f"#include \"lvgl.h\"\n")
        f.write(f"#elif __has_include(\"lvgl/lvgl.h\")\n")
        f.write(f"#include \"lvgl/lvgl.h\"\n")
        f.write(f"#endif\n")
        f.write(f"#else\n")
        f.write(f"#include \"lvgl.h\"\n")
        f.write(f"#endif\n\n")
        f.write(f"#ifndef LV_ATTRIBUTE_MEM_ALIGN\n")
        f.write(f"#define LV_ATTRIBUTE_MEM_ALIGN\n")
        f.write(f"#endif\n\n")
        f.write(f"#ifndef LV_ATTRIBUTE_IMG_{upper}\n")
        f.write(f"#define LV_ATTRIBUTE_IMG_{upper}\n")
        f.write(f"#endif\n\n")
        f.write(
            f"const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST "
            f"LV_ATTRIBUTE_IMG_{upper} uint8_t {var_name}_map[] = {{\n"
        )

        line_buf = []
        col = 0
        for y in range(h):
            for x in range(w):
                r, g, b, a = pixels[x, y]
                c565 = rgb888_to_rgb565(r, g, b)
                lo = c565 & 0xFF
                hi = (c565 >> 8) & 0xFF
                line_buf.append(f"0x{lo:02x},0x{hi:02x},0x{a:02x},")
                col += 1
                if col >= 12:
                    f.write("  " + "".join(line_buf) + "\n")
                    line_buf.clear()
                    col = 0
        if line_buf:
            f.write("  " + "".join(line_buf) + "\n")

        f.write(f"}};\n\n")
        f.write(f"const lv_img_dsc_t {var_name} = {{\n")
        f.write(f"  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,\n")
        f.write(f"  .header.always_zero = 0,\n")
        f.write(f"  .header.reserved = 0,\n")
        f.write(f"  .header.w = {w},\n")
        f.write(f"  .header.h = {h},\n")
        f.write(f"  .data_size = {data_size},\n")
        f.write(f"  .data = {var_name}_map,\n")
        f.write(f"}};\n")

    print(f"Gerado: {output_path}")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(f"Uso: python {sys.argv[0]} <input.png> <output.c> <var_name> [max_width]")
        sys.exit(1)
    mw = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    convert(sys.argv[1], sys.argv[2], sys.argv[3], mw)
