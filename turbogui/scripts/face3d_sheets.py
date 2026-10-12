#!/usr/bin/env python3
"""FC 27 LE Turbo GUI - contact sheets of the game's 3D head renders, for classifying the 3D looks by eye.

Reads the renders Turbo caches (turbo_output/cache/faces3d/p_<playerid>.dds, 256x256 DXT5 with a transparent background)
and writes PNG sheets: a grid of heads on a neutral teal background, each tile labelled "#<n> <playerid>" (no names), plus a
<sheet>.txt listing "n,playerid" so a classification can be keyed back to the ids. Sheets are working files: write them to
a scratch folder, never into the repository.

usage: face3d_sheets.py --out DIR [--ids FILE | --all] [--cols 5] [--rows 4] [--tile 224] [--prefix sheet]
                        [--renders DIR] [--crop top|face]
  --ids FILE   one playerid per line (or the first CSV column; a header line is skipped), in sheet order
  --all        every p_<id>.dds of the render folder, sorted by id
  --crop face  zoom on the lower face (beard / stubble check) instead of the whole head
"""
import argparse, os, sys

from PIL import Image, ImageDraw, ImageFont

DEFAULT_RENDERS = r"C:\FC 27 Live Editor\turbo_output\cache\faces3d"
BG = (58, 96, 104)        # teal: not a hair or skin colour, so blonde, grey and black hair all stand out
LABEL_BG = (16, 16, 16)
LABEL_FG = (255, 255, 120)


def read_ids(path):
    ids = []
    with open(path, encoding="utf-8-sig") as fh:
        for line in fh:
            cell = line.strip().split(",")[0].strip()
            if cell.isdigit():
                ids.append(int(cell))
    return ids


def all_ids(renders):
    out = []
    for f in os.listdir(renders):
        if f.startswith("p_") and f.endswith(".dds") and f[2:-4].isdigit():
            out.append(int(f[2:-4]))
    return sorted(out)


def load_head(renders, pid, crop):
    im = Image.open(os.path.join(renders, "p_%d.dds" % pid)).convert("RGBA")
    if crop == "face":
        im = im.crop((48, 96, 208, 256))  # mouth, chin and jaw at a larger scale
    bg = Image.new("RGBA", im.size, BG + (255,))
    bg.alpha_composite(im)
    return bg.convert("RGB")


def font(size):
    for f in (r"C:\Windows\Fonts\arialbd.ttf", r"C:\Windows\Fonts\arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
        if os.path.isfile(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def make_sheets(ids, out, prefix, cols, rows, tile, renders, crop):
    os.makedirs(out, exist_ok=True)
    per = cols * rows
    lab_h = max(16, tile // 9)
    fnt = font(lab_h - 3)
    written = []
    missing = [p for p in ids if not os.path.isfile(os.path.join(renders, "p_%d.dds" % p))]
    ids = [p for p in ids if p not in set(missing)]
    for s in range(0, len(ids), per):
        chunk = ids[s:s + per]
        sheet = Image.new("RGB", (cols * tile, rows * (tile + lab_h)), (0, 0, 0))
        draw = ImageDraw.Draw(sheet)
        for i, pid in enumerate(chunk):
            x, y = (i % cols) * tile, (i // cols) * (tile + lab_h)
            sheet.paste(load_head(renders, pid, crop).resize((tile, tile), Image.LANCZOS), (x, y))
            draw.rectangle((x, y + tile, x + tile - 1, y + tile + lab_h - 1), fill=LABEL_BG)
            draw.text((x + 4, y + tile + 1), "#%d  %d" % (i + 1, pid), fill=LABEL_FG, font=fnt)
        used_rows = (len(chunk) + cols - 1) // cols
        sheet = sheet.crop((0, 0, cols * tile, used_rows * (tile + lab_h)))
        name = "%s_%03d" % (prefix, s // per + 1)
        sheet.save(os.path.join(out, name + ".png"))
        with open(os.path.join(out, name + ".txt"), "w", encoding="utf-8", newline="\n") as fh:
            for i, pid in enumerate(chunk):
                fh.write("%d,%d\n" % (i + 1, pid))
        written.append(name)
    return written, missing


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--ids")
    g.add_argument("--all", action="store_true")
    ap.add_argument("--cols", type=int, default=5)
    ap.add_argument("--rows", type=int, default=4)
    ap.add_argument("--tile", type=int, default=224)
    ap.add_argument("--prefix", default="sheet")
    ap.add_argument("--renders", default=DEFAULT_RENDERS)
    ap.add_argument("--crop", choices=["top", "face"], default="top")
    a = ap.parse_args(argv[1:])
    ids = all_ids(a.renders) if a.all else read_ids(a.ids)
    written, missing = make_sheets(ids, a.out, a.prefix, a.cols, a.rows, a.tile, a.renders, a.crop)
    print("%d sheet(s) in %s (%d heads, %d without a render)" % (len(written), a.out, len(ids) - len(missing), len(missing)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
