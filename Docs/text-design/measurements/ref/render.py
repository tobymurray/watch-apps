"""Reference rasterisation of the string inventory, and the status quo beside it.

For each (string, face, em px): FreeType exact-coverage AA at 1x (an exact box filter of the
outline onto the panel grid), quantised to the panel's four levels; the same with FreeType's
light autohinter (vertical grid-fit, which is what TouchGFX's converter output matches within
2 %); hard-aliased (FT mono); and the status-quo supersample-and-shrink from u8g2 faces via
the `sq` binary. Writes one contact sheet per group, 4x nearest-neighbour, and a metrics CSV.
Host renderings only: nothing here is evidence about the glass.
"""
import freetype, numpy as np, subprocess, sys, os, csv
from PIL import Image, ImageDraw
S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = S + "/fonts/"
SQ = S + "/sq/target/release/sq"
OUT = S + "/ref/out/"; os.makedirs(OUT, exist_ok=True)

def quantise(cov, thresholds=(1/6, 1/2, 5/6)):
    lv = np.zeros_like(cov, dtype=np.uint8)
    for i, t in enumerate(thresholds): lv[cov >= t] = i + 1
    return lv

def ft_render(face_path, px, text, hint="none", mono=False):
    f = freetype.Face(face_path); f.set_pixel_sizes(0, px)
    flags = freetype.FT_LOAD_RENDER
    flags |= {"none": freetype.FT_LOAD_NO_HINTING, "light": freetype.FT_LOAD_TARGET_LIGHT, "normal": freetype.FT_LOAD_TARGET_NORMAL}[hint]
    if mono: flags |= freetype.FT_LOAD_TARGET_MONO | freetype.FT_LOAD_MONOCHROME
    asc = int(np.ceil(f.size.ascender / 64)); desc = int(np.ceil(-f.size.descender / 64))
    H = asc + desc + 2
    # pass 1: advance width
    pen = 0; prev = None; w = 0
    for ch in text:
        gi = f.get_char_index(ord(ch)); f.load_glyph(gi, flags)
        w = pen + max(f.glyph.advance.x // 64, f.glyph.bitmap_left + f.glyph.bitmap.width)
        pen += f.glyph.advance.x // 64
    W = max(w, 1) + 2
    cov = np.zeros((H, W), dtype=np.float32)
    pen = 1
    for ch in text:
        gi = f.get_char_index(ord(ch)); f.load_glyph(gi, flags)
        b = f.glyph.bitmap
        if b.width and b.rows:
            if mono:
                rows = np.frombuffer(bytes(b.buffer), dtype=np.uint8).reshape(b.rows, b.pitch)
                bits = np.unpackbits(rows, axis=1)[:, :b.width].astype(np.float32)
                g = bits
            else:
                g = np.frombuffer(bytes(b.buffer), dtype=np.uint8).reshape(b.rows, b.pitch)[:, :b.width].astype(np.float32) / 255.0
            x0 = pen + f.glyph.bitmap_left; y0 = asc - f.glyph.bitmap_top + 1
            y0c = max(y0, 0); x0c = max(x0, 0)
            gg = g[y0c - y0:, x0c - x0:]
            hh = min(gg.shape[0], H - y0c); ww = min(gg.shape[1], W - x0c)
            cov[y0c:y0c+hh, x0c:x0c+ww] = np.maximum(cov[y0c:y0c+hh, x0c:x0c+ww], gg[:hh, :ww])
        pen += f.glyph.advance.x // 64
    return cov, asc

def sq_render(face, dst_h, text):
    p = OUT + f"sq_{face}_{dst_h}_{abs(hash(text))}.pgm"
    r = subprocess.run([SQ, face, str(dst_h), text, p], capture_output=True, text=True, check=True)
    img = np.array(Image.open(p)); return (img // 85).astype(np.uint8), r.stdout.strip()

def trim(lv):
    ys, xs = np.nonzero(lv)
    if len(ys) == 0: return lv[:1, :1]
    return lv[ys.min():ys.max()+1, xs.min():xs.max()+1]

def stats(lv):
    t = trim(lv); n = t.size
    ink = (t > 0).sum()
    counts = [(t == k).sum() for k in range(4)]
    frac_partial = (counts[1] + counts[2]) / max(ink, 1)
    return dict(w=t.shape[1], h=t.shape[0], ink=int(ink), l1=int(counts[1]), l2=int(counts[2]), l3=int(counts[3]), partial=round(float(frac_partial), 3))

def to_img(lv, scale=4, color=(255,255,255)):
    rgb = np.zeros(lv.shape + (3,), dtype=np.uint8)
    for k in range(1,4):
        for c in range(3): rgb[..., c][lv == k] = color[c] * k // 3
    return Image.fromarray(rgb).resize((lv.shape[1]*scale, lv.shape[0]*scale), Image.NEAREST)

def sheet(name, rows, scale=4):
    # rows: list of (label, [(sublabel, lv), ...])
    pad = 12; labelw = 260
    cell_h = max(max(lv.shape[0] for _, lv in r) for _, r in rows) * scale + 22
    cell_w = max(max(lv.shape[1] for _, lv in r) for _, r in rows) * scale + pad
    ncol = max(len(r) for _, r in rows)
    img = Image.new("RGB", (labelw + ncol * cell_w, len(rows) * (cell_h + pad)), (24, 24, 24))
    d = ImageDraw.Draw(img)
    for i, (label, r) in enumerate(rows):
        y = i * (cell_h + pad)
        d.text((6, y + 4), label, fill=(200, 200, 200))
        for j, (sub, lv) in enumerate(r):
            x = labelw + j * cell_w
            d.text((x, y + 2), sub, fill=(140, 200, 255))
            img.paste(to_img(lv, scale), (x, y + 16))
    img.save(OUT + name + ".png"); print("wrote", OUT + name + ".png")

if __name__ == "__main__":
    metrics = []
    # --- Group 1: Barcode's two tiers, status quo vs reference at the TouchGFX sizes it replaced
    barcode = [("helvB24_tr", 24, "Poppins-SemiBold.ttf", 20, "0123456789ABCD"), ("helvB24_tr", 24, "Poppins-SemiBold.ttf", 20, "A1234"),
               ("helvR24_tr", 18, "Poppins-Regular.ttf", 18, "GYMWORLD12345678"), ("helvR24_tr", 18, "Poppins-Regular.ttf", 18, "Set it to Code128,"),
               ("helvR24_tr", 18, "Poppins-Regular.ttf", 18, "Toby Murray")]
    rows = []
    for face, dh, pf, em, text in barcode:
        sq, info = sq_render(face, dh, text)
        cov, _ = ft_render(FONTS + pf, em, text); ref = quantise(cov)
        covl, _ = ft_render(FONTS + pf, em, text, hint="light"); refl = quantise(covl)
        mono, _ = ft_render(FONTS + pf, em, text, mono=True); hard = quantise(mono, (0.5, 0.5, 0.5))
        rows.append((f"{text!r}\n{face}@{dh} vs {pf[8:-4]} {em}px", [("status quo (u8g2 shrink)", sq), ("Poppins AA->4 levels", ref), ("Poppins light-hint AA->4", refl), ("Poppins hard-aliased", hard)]))
        for lab, lv in [("sq", sq), ("ref", ref), ("ref_light", refl), ("hard", hard)]:
            metrics.append(dict(group="barcode", text=text, variant=lab, face=face if lab=="sq" else pf, px=dh if lab=="sq" else em, **stats(lv)))
    sheet("01_barcode_tiers", rows)
    # --- Group 2: Spin labels/headings/answers at the sizes it draws, vs Poppins at matching cap height
    spin = [("helvR24_tr", 16, "Poppins-Regular.ttf", 16, "NOTHING WAS SAVED"), ("helvR24_tr", 16, "Poppins-Regular.ttf", 16, "FINDING STRAP"),
            ("helvB24_tr", 18, "Poppins-SemiBold.ttf", 18, "NOT SAVED"), ("helvB24_tr", 24, "Poppins-SemiBold.ttf", 24, "YES"),
            ("helvB24_tr", 32, "Poppins-SemiBold.ttf", 32, "SPIN"), ("fub49_tn", 25, "Poppins-SemiBold.ttf", 27, "142"),
            ("fub49_tn", 54, "Poppins-SemiBold.ttf", 60, "12:34"), ("fub49_tn", 44, "Poppins-SemiBold.ttf", 49, "1:02:05")]
    rows = []
    for face, dh, pf, em, text in spin:
        sq, info = sq_render(face, dh, text)
        cov, _ = ft_render(FONTS + pf, em, text); ref = quantise(cov)
        covl, _ = ft_render(FONTS + pf, em, text, hint="light"); refl = quantise(covl)
        mono, _ = ft_render(FONTS + pf, em, text, mono=True); hard = quantise(mono, (0.5, 0.5, 0.5))
        rows.append((f"{text!r}\n{face}@{dh} vs {pf[8:-4]} {em}px", [("status quo (u8g2 shrink)", sq), ("Poppins AA->4 levels", ref), ("Poppins light-hint AA->4", refl), ("Poppins hard-aliased", hard)]))
        for lab, lv in [("sq", sq), ("ref", ref), ("ref_light", refl), ("hard", hard)]:
            metrics.append(dict(group="spin", text=text, variant=lab, face=face if lab=="sq" else pf, px=dh if lab=="sq" else em, **stats(lv)))
    sheet("02_spin_sizes", rows)
    # --- Group 3: the research's three conditions at 22 and 26 px, plus 16 and 18, bright on dark
    rows = []
    for em in [16, 18, 22, 26]:
        for pf in ["Poppins-Regular.ttf", "Poppins-SemiBold.ttf"]:
            text = "José Zoë 12:34 µg ±5°"
            mono, _ = ft_render(FONTS + pf, em, text, mono=True); hard = quantise(mono, (0.5,)*3)
            cov, _ = ft_render(FONTS + pf, em, text); ref = quantise(cov)
            # deliberate two-shade fringe: keep only 170 (level 2) as the single fringe shade -> thresholds so that
            # partial coverage >= 0.35 maps to level 2, >= 0.8 to full; nothing lands on level 1 (85, which washes out in daylight per Spin's DIM note)
            fringe = quantise(cov, (2.0, 0.35, 0.8))
            covl, _ = ft_render(FONTS + pf, em, text, hint="light"); refl = quantise(covl)
            rows.append((f"{pf[8:-4]} {em}px", [("hard-aliased", hard), ("AA->4 (1/6,1/2,5/6)", ref), ("AA, one fringe shade 170", fringe), ("light-hint AA->4", refl)]))
            for lab, lv in [("hard", hard), ("ref", ref), ("fringe170", fringe), ("ref_light", refl)]:
                metrics.append(dict(group="conditions", text=text, variant=lab, face=pf, px=em, **stats(lv)))
    sheet("03_three_conditions", rows, scale=4)
    # --- Group 4: stem widths — 'l' and 'H' at each em, no hint vs light hint, to show stem blur across columns
    rows = []
    for em in [16, 18, 20, 24]:
        for pf in ["Poppins-Regular.ttf", "Poppins-SemiBold.ttf"]:
            cov, _ = ft_render(FONTS + pf, em, "lHIl"); covl, _ = ft_render(FONTS + pf, em, "lHIl", hint="light")
            rows.append((f"{pf[8:-4]} {em}px 'lHIl'", [("no hint", quantise(cov)), ("light hint", quantise(covl))]))
    sheet("04_stems", rows, scale=8)
    with open(OUT + "metrics.csv", "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(metrics[0].keys())); w.writeheader(); w.writerows(metrics)
    print("metrics rows", len(metrics))
