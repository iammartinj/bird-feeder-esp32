#!/usr/bin/env python3
"""sheet_cut.py - cuts a sheet of sprites on flat grey into PNGs with alpha.

Usage:
  python3 sheet_cut.py SHEET.png --rows vrabec,konadra,... --poses sedi,klove,... \
      --px-per-cm 2.4 --lengths lengths.json --out sprites/
  python3 sheet_cut.py CELL.png --rows konadra --poses ohlizi_se ...   (a file without a grid = one cell)

The calls that make the feeder's sprites, in this order, run from the repository
root with C = "--lengths tools/lengths.json --px-per-cm 2.4 --out tools/sprites":
  sheet_cut.py art/sheet_small.png --rows vrabec,konadra,modrinka,zvonek,stehlik,brhlik \
      --poses sedi,klove,ohlizi_se,x4,x5 --flip $C
  sheet_cut.py art/sheet_large.png --rows kos,hrdlicka,strakapoud,sojka,veverka \
      --poses sedi,klove,ohlizi_se,x4,x5 --flip --skip-first-col $C
  sheet_cut.py art/sheet_large.png --rows hrdlicka --poses x1,x2,x3,x4,sedi --use-rows 1 \
      --flip --skip-first-col --scale-col 0 $C
      (the collared dove's sitting pose comes from the fifth column: in the first its
       neck is exactly the grey of the sheet and cannot be cut out; --scale-col 0 keeps
       the size of the other poses)
  sheet_cut.py art/sheet_wings.png --rows vrabec,konadra,modrinka,zvonek,stehlik,brhlik,kos,hrdlicka,strakapoud,sojka,veverka \
      --poses pristava,prikrceny --flip --scale-col 1 $C
  sheet_cut.py art/cell_greattit_lookback.png --rows konadra --poses ohlizi_se $C      (no flip)
  sheet_cut.py art/sheet_nuthatch.png --rows brhlik --poses hlavou_dolu,x2,x3,x4,x5 \
      --use-rows 0 --exclude wood --scale-col 0 --cell-scale 0.9 $C   (only cell 1 is used)
The helper poses x1..x5 stay in the sprites folder and do not go into the firmware.
Baking:
  sheet_cut.py --bake tools/sprites main/scene        -> bird_art.h, bird_art.cpp

Species and pose names are the Czech ones the firmware uses: vrabec house sparrow,
konadra great tit, modrinka blue tit, zvonek greenfinch, stehlik goldfinch, brhlik
nuthatch, kos blackbird, hrdlicka collared dove, strakapoud great spotted
woodpecker, sojka jay, veverka red squirrel; sedi sits, klove pecks, ohlizi_se
looks back, pristava lands, prikrceny crouches, hlavou_dolu head down.

Per cell: the background colour guessed from the edge, alpha from the colour
distance (soft threshold), un-premultiplied against the background, the largest
connected component kept (drops labels and specks), cropped, anchored, and box
filtered down to the species' length in scene pixels. Deterministic, no hand work.
"""
import argparse, json, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

def find_lines(profile, thresh, min_gap):
    """indices (centres) of the dark lines in a 1D brightness profile"""
    dark = profile < thresh
    lines, i = [], 0
    while i < len(dark):
        if dark[i]:
            j = i
            while j < len(dark) and dark[j]: j += 1
            if j - i <= 14: lines.append((i + j) // 2)
            i = j
        else:
            i += 1
    # merge the close ones
    merged = []
    for l in lines:
        if merged and l - merged[-1] < min_gap: merged[-1] = (merged[-1] + l) // 2
        else: merged.append(l)
    return merged

def detect_grid(img):
    g = np.asarray(img.convert('L'), dtype=float)
    H, W = g.shape
    # grid lines: rows and columns where at least 60 % of the pixels are dark
    rowdark = (g < 130).mean(axis=1); coldark = (g < 130).mean(axis=0)
    rows = find_lines(1 - rowdark, 0.4, 20)
    cols = find_lines(1 - coldark, 0.4, 20)
    ys = [0] + rows + [H]; xs = [0] + cols + [W]
    cells = []
    for r in range(len(ys) - 1):
        row = []
        for c in range(len(xs) - 1):
            x0, x1, y0, y1 = xs[c], xs[c + 1], ys[r], ys[r + 1]
            if x1 - x0 < 40 or y1 - y0 < 40: continue   # slivers at the edges
            row.append((x0 + 6, y0 + 6, x1 - 6, y1 - 6))
        if row: cells.append(row)
    return cells

FILL_R = 16  # sheet px: a notch narrower than 2 x FILL_R belongs to the outline

def cut_cell(cell_img, t0=10.0, t1=48.0, exclude=None):
    """exclude: polygons in cell coordinates (a perch, a plank) to mask out"""
    a = np.asarray(cell_img.convert('RGB'), dtype=float)
    H, W, _ = a.shape
    excl = np.zeros((H, W), bool)
    if exclude == 'wood':
        r, g, b = a[..., 0], a[..., 1], a[..., 2]
        wood = (r > g) & (g > b) & ((r - b) > 28) & ((r - b) < 90) & (r < 225) & (r > 80)
        wood = ndimage.binary_opening(wood, iterations=3)
        lab, n = ndimage.label(wood)
        if n:
            sizes = ndimage.sum(wood, lab, range(1, n + 1))
            big = lab == (1 + int(np.argmax(sizes)))
            excl = ndimage.binary_dilation(ndimage.binary_fill_holes(big), iterations=4)
        exclude = None
    if exclude:
        from PIL import ImageDraw
        m = Image.new('L', (W, H), 0); d = ImageDraw.Draw(m)
        for poly in exclude: d.polygon([tuple(p) for p in poly], fill=255)
        excl = np.asarray(m) > 0
    border = np.concatenate([a[:8].reshape(-1, 3), a[-8:].reshape(-1, 3), a[:, :8].reshape(-1, 3), a[:, -8:].reshape(-1, 3)])
    bg = np.median(border, axis=0)
    d = np.abs(a - bg).max(axis=2)
    alpha = np.clip((d - t0) / (t1 - t0), 0, 1)
    alpha[excl] = 0
    # the largest component (after a dilation, so legs and beak stay attached)
    solid = ndimage.binary_dilation(alpha > 0.5, iterations=6)
    lab, n = ndimage.label(solid)
    if n == 0: return None
    sizes = ndimage.sum(solid, lab, range(1, n + 1))
    keep = lab == (1 + int(np.argmax(sizes)))
    alpha = alpha * keep
    # Feathers almost the grey of the sheet (the collared dove's neck and back)
    # come out transparent. Inside the outline, closed over its notches, a more
    # sensitive threshold just above the background noise applies: the feathers
    # differ from the grey by a few levels, while the gap between a blackbird's
    # legs (also inside) is exactly the grey and stays transparent.
    noise = float(np.percentile(np.abs(border - bg).max(axis=1), 95))
    closed = np.pad(alpha > 0.5, FILL_R)
    closed = ndimage.binary_closing(closed, iterations=FILL_R)[FILL_R:-FILL_R, FILL_R:-FILL_R]
    inside = ndimage.binary_fill_holes(closed) & ~excl
    soft = np.clip((d - (noise + 2.0)) / 8.0, 0, 1)
    alpha = np.where(inside, np.maximum(alpha, soft), alpha)
    # un-premultiply against the background
    eps = 1e-3
    col = (a - (1 - alpha[..., None]) * bg) / np.maximum(alpha[..., None], eps)
    col = np.clip(col, 0, 255)
    col[alpha < eps] = 0
    ys, xs = np.nonzero(alpha > 0.02)
    if len(ys) == 0: return None
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    rgba = np.dstack([col, alpha * 255]).astype(np.uint8)[y0:y1, x0:x1]
    im = Image.fromarray(rgba, 'RGBA')
    im.info['contact'] = None
    if exclude:
        # anchor = centroid of the bird's pixels next to the masked-out perch
        near = ndimage.binary_dilation(excl, iterations=6) & (alpha > 0.3)
        ys2, xs2 = np.nonzero(near)
        if len(ys2): im.info['contact'] = (float(xs2.mean() - x0), float(ys2.mean() - y0))
    return im

def anchor_of(im, pose):
    a = np.asarray(im)[..., 3] > 64
    H, W = a.shape
    if pose in ('pristava', 'skok'):
        ys, xs = np.nonzero(a); return (float(xs.mean()), float(ys.mean()))
    third = a[:, W // 3: 2 * W // 3]
    rows = np.nonzero(third.any(axis=1))[0]
    y = rows.max() if len(rows) else H - 1
    cols = np.nonzero(a[y - 3:y + 1].any(axis=0))[0]
    x = cols.mean() if len(cols) else W / 2
    return (float(x), float(y))

def box_resize(im, scale):
    w, h = im.size
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    # premultiply before shrinking, so no black bleeds into the edges
    a = np.asarray(im, dtype=float); al = a[..., 3:4] / 255
    pm = Image.fromarray(np.dstack([a[..., :3] * al, a[..., 3:4]]).astype(np.uint8), 'RGBA')
    small = np.asarray(pm.resize((nw, nh), Image.BOX), dtype=float)
    sal = small[..., 3:4] / 255
    col = np.where(sal > 0.004, small[..., :3] / np.maximum(sal, 0.004), 0)
    return Image.fromarray(np.dstack([np.clip(col, 0, 255), small[..., 3:4]]).astype(np.uint8), 'RGBA')

def bake(sprites_dir, out_dir):
    """The sprites from sprites.json baked into bird_art.h/.cpp: RGBA8 arrays
    (straight alpha) with size and anchor. The helper poses x1..x5 are left out."""
    meta = json.load(open(os.path.join(sprites_dir, 'sprites.json'), encoding='utf-8'))
    names = sorted(k for k in meta if not k.split('_', 1)[1].startswith('x'))
    os.makedirs(out_dir, exist_ok=True)
    h = ['#ifndef FEEDER_BIRD_ART_H', '#define FEEDER_BIRD_ART_H',
         '// Generated by tools/sheet_cut.py --bake - do not edit.',
         '// Straight (not premultiplied) RGBA, profile facing left; the engine mirrors.',
         '#include "fish_art.h"', '',
         'struct BirdSprite {',
         '  const char*  name;     // species_pose, as in sprites.json',
         '  uint8_t      w, h;',
         '  float        ax, ay;   // anchor: feet on a perch, body centre in flight',
         '  const RGBA8* px;',
         '};', '',
         f'static const int N_BIRD_SPRITES = {len(names)};',
         'extern const BirdSprite BIRD_SPRITES[N_BIRD_SPRITES];',
         'const BirdSprite* birdSprite(const char* name);   // nullptr if there is none', '',
         '#endif // FEEDER_BIRD_ART_H', '']
    c = ['// Generated by tools/sheet_cut.py --bake - do not edit.', '#include "bird_art.h"',
         '#include <string.h>', '']
    total = 0
    for k in names:
        m = meta[k]
        im = Image.open(os.path.join(sprites_dir, k + '.png')).convert('RGBA')
        w, hh = im.size
        if (w, hh) != (m['w'], m['h']):
            sys.exit(f'{k}: {w}x{hh} in the PNG, {m["w"]}x{m["h"]} in sprites.json')
        px = np.asarray(im)
        c.append(f'// {k}  ({w}x{hh})')
        c.append(f'static const RGBA8 {k.upper()}_ART[{w * hh}] PROGMEM = {{')
        for y in range(hh):
            c.append('  ' + ', '.join('{%3d,%3d,%3d,%3d}' % tuple(int(v) for v in px[y, x]) for x in range(w)) + ',')
        c.append('};')
        c.append('')
        total += w * hh * 4
    c.append('const BirdSprite BIRD_SPRITES[N_BIRD_SPRITES] = {')
    for k in names:
        m = meta[k]
        c.append(f'  {{ "{k}", {m["w"]}, {m["h"]}, {m["anchor"][0]:.1f}f, {m["anchor"][1]:.1f}f, {k.upper()}_ART }},')
    c.append('};')
    c.append('')
    c.append('const BirdSprite* birdSprite(const char* name) {')
    c.append('  for (int i = 0; i < N_BIRD_SPRITES; i++)')
    c.append('    if (strcmp(BIRD_SPRITES[i].name, name) == 0) return &BIRD_SPRITES[i];')
    c.append('  return nullptr;')
    c.append('}')
    open(os.path.join(out_dir, 'bird_art.h'), 'w', encoding='utf-8', newline='\n').write('\n'.join(h))
    open(os.path.join(out_dir, 'bird_art.cpp'), 'w', encoding='utf-8', newline='\n').write('\n'.join(c) + '\n')
    print(f'{len(names)} sprites, {total / 1024:.1f} kB of pixels -> {out_dir}')

def main():
    if len(sys.argv) >= 2 and sys.argv[1] == '--bake':
        if len(sys.argv) != 4:
            sys.exit('usage: sheet_cut.py --bake SPRITES_DIR OUT_DIR')
        bake(sys.argv[2], sys.argv[3])
        return
    ap = argparse.ArgumentParser()
    ap.add_argument('sheet'); ap.add_argument('--rows', required=True); ap.add_argument('--poses', required=True)
    ap.add_argument('--lengths', required=True, help='JSON {species: cm}'); ap.add_argument('--px-per-cm', type=float, required=True)
    ap.add_argument('--out', default='sprites'); ap.add_argument('--skip-first-col', action='store_true', help='the sheet has a column of labels')
    ap.add_argument('--flip', action='store_true', help='mirror, so they face left')
    ap.add_argument('--exclude', help='JSON {"r,c": [[[x,y],...], ...]} perch polygons in cell coordinates, or "wood" = the largest wooden area in the cell')
    ap.add_argument('--scale-col', type=int, default=0, help='the column whose larger dimension is the species length (default 0)')
    ap.add_argument('--use-rows', help='grid rows to use, e.g. 0 (a sheet with an empty row)')
    ap.add_argument('--scale-from', help='take the scale of this species\' first pose, already in sprites.json')
    ap.add_argument('--cell-scale', help='per-column scale factors (the model did not keep the scale along a row), e.g. 1,0.57,0.77')
    args = ap.parse_args()
    rows = args.rows.split(','); poses = args.poses.split(',')
    lengths = json.load(open(args.lengths))
    img = Image.open(args.sheet).convert('RGB')
    cells = detect_grid(img)
    if args.skip_first_col: cells = [r[1:] for r in cells]
    if args.use_rows: cells = [cells[int(i)] for i in args.use_rows.split(',')]
    exclude = ('wood' if args.exclude == 'wood' else json.load(open(args.exclude))) if args.exclude else {}
    if len(cells) != len(rows):
        sys.exit(f'found {len(cells)} rows, expected {len(rows)}: {[len(r) for r in cells]}')
    os.makedirs(args.out, exist_ok=True)
    mp = os.path.join(args.out, 'sprites.json')
    meta = json.load(open(mp)) if os.path.exists(mp) else {}   # adds to what is there, does not overwrite
    for r, species in enumerate(rows):
        if len(cells[r]) != len(poses):
            sys.exit(f'row {species}: {len(cells[r])} cells, expected {len(poses)}')
        # the reference sprite for the row's scale
        rx0, ry0, rx1, ry1 = cells[r][args.scale_col]
        ref_spr = cut_cell(img.crop((rx0, ry0, rx1, ry1)), exclude=(exclude if exclude == 'wood' else exclude.get(f'{r},{args.scale_col}')))
        row_scale = lengths[species] * args.px_per_cm / max(ref_spr.size)
        if args.scale_from:
            ref = meta.get(f'{species}_sedi')
            if ref and 'scale' in ref: row_scale = ref['scale']
        for c, pose in enumerate(poses):
            x0, y0, x1, y1 = cells[r][c]
            spr = cut_cell(img.crop((x0, y0, x1, y1)), exclude=(exclude if exclude == 'wood' else exclude.get(f'{r},{c}')))
            if spr is None: print('empty cell', species, pose); continue
            contact = spr.info.get('contact')
            if args.flip:
                spr = spr.transpose(Image.FLIP_LEFT_RIGHT)
                if contact: contact = (spr.size[0] - contact[0], contact[1])
            ax, ay = contact if contact else anchor_of(spr, pose)
            # the row's scale: species length / larger dimension of pose 1, or taken from an earlier run
            cs = [float(v) for v in args.cell_scale.split(',')] if args.cell_scale else None
            eff = row_scale * (cs[c] if cs and c < len(cs) else 1.0)
            small = box_resize(spr, eff)
            ax, ay = ax * eff / row_scale, ay * eff / row_scale
            name = f'{species}_{pose}'
            small.save(os.path.join(args.out, name + '.png'))
            spr.save(os.path.join(args.out, name + '_full.png'))
            meta[name] = {'w': small.size[0], 'h': small.size[1], 'anchor': [round(ax * row_scale, 1), round(ay * row_scale, 1)],
                          'scale': eff, 'blend_px': int((np.asarray(small)[..., 3] % 255 > 0).sum())}
            print(f'{name:28s} {spr.size[0]:4d}x{spr.size[1]:<4d} -> {small.size[0]:3d}x{small.size[1]:<3d} anchor {meta[name]["anchor"]}')
    json.dump(meta, open(os.path.join(args.out, 'sprites.json'), 'w'), indent=1, ensure_ascii=False)

if __name__ == '__main__':
    main()
