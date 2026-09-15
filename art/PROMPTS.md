# The artwork: how it was made and how to make more

Everything was generated with Magnific (formerly Freepik) on 14 September 2026,
on a paid plan.

The files in this folder:

| File | Model | Credits |
|---|---|---|
| `bg_winter_overcast.png` | auto | 75 |
| `bg_winter_morning.png` | auto, with `bg_winter_overcast.png` as reference | 75 |
| `bg_autumn.png` | auto, with `bg_winter_overcast.png` as reference | 75 |
| `feeder_cutout.png` | background removal on `bg_winter_overcast.png` | 3 |
| `sheet_small.png` | Nano Banana Pro, 2k | 75 |
| `sheet_large.png` | Nano Banana Pro, 2k | 75 |
| `bg_winter_overcast_empty.png` | retouch, erase with a mask | 30 |
| `bg_winter_morning_empty.png` | retouch, replace with a mask and a prompt | 10 |
| `bg_autumn_empty.png` | retouch, erase with a mask | 30 |
| `sheet_wings.png` | Nano Banana Pro, 2k, 3:4; 11 species × (landing, crouched) | 75 |
| `sheet_nuthatch.png` | Nano Banana Pro, 2k, 21:9; nuthatch in 5 poses on a plank | 75 |
| `cell_greattit_lookback.png` | Nano Banana Pro, 1k, 1:1; great tit looking back | 75 |
| `feeder_mask.png` | made by a script from the cut-out's alpha | – |
| `depth-map.png` | a relative depth estimate of the original photo, brighter is nearer | – |

All are full-resolution PNGs, the backgrounds 4:3. `tools/make_assets.py`
turns the backgrounds into the JPEGs baked into flash; the sheets and the
cut-out stay PNG, because `tools/sheet_cut.py` needs clean grey edges.

## Backgrounds

### The base scene (`bg_winter_overcast.png`)

Text to image, model `auto`, 4:3, three variants in one call, the second one
chosen. Prompt:

```
Photograph taken through a window with a telephoto lens: an empty wooden
bird feeder with a small roof, hanging from a bare apple-tree branch in
a Central European garden in winter. A thin layer of snow on the feeder
roof and on the branch, sunflower seeds scattered on the feeder tray,
wooden perches clearly visible on both sides, a second bare branch nearby
as a perch, snowy ground visible below the feeder. No birds. Overcast soft
daylight, muted natural colours, shallow depth of field, background of dark
blurred spruce trees and an old garden fence fading to near black.
Realistic photo, slightly grainy, no text, no people.
```

The same prompt also went to Nano Banana Pro (2 variants, 75 credits each)
and GPT 2.5 at high quality (2 variants, 325 credits each); the pick stayed
the one from `auto`. `auto` is enough for further backgrounds.

### Variants of the same scene

Always image generation with `bg_winter_overcast.png` as the reference image,
model `auto`, 4:3, one image. The feeder and the branch must stay exactly where
they are in the base scene; a variant where they moved is thrown away.

`bg_winter_morning.png`:

```
Same scene, same wooden bird feeder, same branch, same camera position and
framing as the reference photo, but on a bright winter morning: low warm
sunlight from the left casting a soft shadow of the feeder on the snow,
a slightly blue sky behind the bare branches, thin frost on the feeder
roof. Still no birds, still empty feeder with sunflower seeds on the tray.
Realistic photo, slightly grainy, no text, no people.
```

`bg_autumn.png`:

```
Same scene, same wooden bird feeder, same branch, same camera position and
framing as the reference photo, but in late autumn with no snow: wet dark
bark, a few remaining brown leaves on the branch, damp brown leaves on the
ground below, overcast grey light, background of dark blurred spruce trees
fading to near black. Still no birds, still empty feeder with sunflower
seeds on the tray. Realistic photo, slightly grainy, no text, no people.
```

### Not generated yet (templates)

Spring:

```
Same scene, same wooden bird feeder, same branch, same camera position and
framing as the reference photo, but in early spring: the apple branch has
fresh green buds and a few white blossoms, wet dark soil and first grass
on the ground below, soft overcast light, background of dark blurred spruce
trees fading to near black. Still no birds, still empty feeder with
sunflower seeds on the tray. Realistic photo, slightly grainy, no text,
no people.
```

Summer:

```
Same scene, same wooden bird feeder, same branch, same camera position and
framing as the reference photo, but in summer: the apple branch in full
green leaf, green grass below, warm evening light from the left, background
of dark blurred spruce trees fading to near black. Still no birds, still
empty feeder with sunflower seeds on the tray. Realistic photo, slightly
grainy, no text, no people.
```

A wider shot, should the birds ever need to be drawn smaller:

```
Same scene, same wooden bird feeder, same branch, same lighting as the
reference photo, but seen from farther away: the feeder occupies about one
quarter of the frame width, more of the garden and the dark spruce
background visible around it, snowy ground below. Still no birds, still
empty feeder. Realistic photo, slightly grainy, no text, no people.
```

## The feeder cut-out (`feeder_cutout.png`)

Background removal on `bg_winter_overcast.png`, no parameters. The cut-out holds
the feeder and the branch; `make_assets.py` keeps only what hangs below the knot,
and the branch stays in the background. It does not have to be made again for a
new background as long as the feeder does not change: the colours of each
background's own feeder are taken from that photo.

## Backgrounds without the feeder

The firmware always draws the feeder as a layer of its own (it swings), so it
must not be in the background, or it would show through behind the swinging one.
The three `*_empty.png` were made by retouching one area, which leaves the rest
of the photo untouched pixel for pixel; the branch and the knot stay, only the
string below the knot and the feeder with its perches go.

The mask: from the cut-out's alpha only the string below the knot and the feeder
(a polygon that leaves the branches out), widened by about 30 px, saved as a
**strictly black and white RGB PNG** (Magnific rejects masks with any grey pixel,
and a greyscale PNG gave trouble after uploading). For a new background with the
same feeder the mask stays the same: all the backgrounds made from the reference
have the feeder on exactly the same pixels (checked by correlating edges, offset
0,0).

Method: retouch in `erase` mode (30 credits). On the morning variant erase kept
returning the mask filled with red instead of a result, whatever the mask format;
there `replace` mode (10 credits) worked, with the prompt:

```
Empty space: the snowy garden background continues seamlessly, the old
wooden fence and dark blurred spruce trees behind, sunlit snow on the
ground below, the thick bare branch continuing naturally through the
area, bright winter morning light from the left. Nothing hanging from the
branch, no bird feeder, no rope, no objects.
```

For another background try erase first, and replace with a prompt describing
that variant's surroundings if it comes back red. The feeder's shadow on the
snow in the morning variant stays in the background on purpose: a 3° swing
three metres above would not visibly move it.

## Bird sheets

Nano Banana Pro, 4:3, 2k, one image. Nano Banana 2 might do the same for less,
not tried. What `sheet_cut.py` relies on: a flat grey of RGB 200,200,200, thin
dark grid lines, one bird per cell, profile facing left, row = species,
column = pose, a species label in the left column (thrown away).

Poses by column: 1 perched upright, 2 pecking head down, 3 looking back over the
shoulder, 4 landing with wings spread and tail fanned, 5 crouched before take-off.
For the squirrel: 4 mid-jump, 5 sitting with a seed.

`sheet_small.png` (rows: male house sparrow, great tit, blue tit, European
greenfinch, European goldfinch, Eurasian nuthatch):

```
Sprite reference sheet of Central European garden birds, realistic
photographic quality, on a flat uniform light grey background (RGB
200,200,200) with no ground, no shadows, no props. A grid of 5 columns and
6 rows separated by thin dark grey lines, each cell containing exactly one
bird, centred, large in the cell. Every bird is shown in strict side view
facing LEFT, lit by soft overcast daylight from above. All birds in the
same row are at the same scale and are the same individual. The five
columns are five poses: column 1 perched upright and alert; column 2
pecking with head down; column 3 perched with head turned looking back
over its shoulder; column 4 wings fully spread in landing position, tail
fanned; column 5 crouched low, about to take off. The six rows are six
species: row 1 male house sparrow; row 2 great tit; row 3 blue tit; row 4
European greenfinch; row 5 European goldfinch; row 6 Eurasian nuthatch.
Small plain label with the species name at the left edge of each row.
No other text, no watermark.
```

`sheet_large.png` (rows: male common blackbird, Eurasian collared dove, great
spotted woodpecker, Eurasian jay, red squirrel):

```
Sprite reference sheet of Central European garden animals, realistic
photographic quality, on a flat uniform light grey background (RGB
200,200,200) with no ground, no shadows, no props. A grid of 5 columns and
5 rows separated by thin dark grey lines, each cell containing exactly one
animal, centred, large in the cell. Every animal is shown in strict side
view facing LEFT, lit by soft overcast daylight from above. All animals in
the same row are at the same scale and are the same individual. The five
columns are five poses: column 1 standing upright and alert; column 2 head
down feeding; column 3 head turned looking back over its shoulder; column
4 wings fully spread in landing position (for the squirrel: mid-jump,
stretched); column 5 crouched low, about to take off (for the squirrel:
sitting up holding a seed). The five rows are five species: row 1 male
common blackbird; row 2 Eurasian collared dove; row 3 great spotted
woodpecker; row 4 Eurasian jay; row 5 red squirrel with winter ear tufts.
Small plain label with the species name at the left edge of each row.
No other text, no watermark.
```

### What went wrong in the first sheets, and the fixes

- The birds face **right**, not left as the prompt says. `sheet_cut.py --flip`
  handles it; the single great tit cell and the nuthatch row came out facing
  left and are not flipped.
- Column 4 (wings) came out from the front or the back, and column 5 almost the
  same as column 1. Both are replaced by `sheet_wings.png`, whose prompt says
  "camera level with the bird, only one eye visible, near wing overlaps far
  wing"; that keeps the model in profile.
- The great tit in column 3 turned out to be a sparrow, hence
  `cell_greattit_lookback.png`.
- The nuthatch had five nearly identical poses, hence a row of its own with a
  plank to hold on to; without something to cling to the model will not draw it
  head down. Only cell 1 (head down) is used: in cells 2–5 the plank has grown
  into the body and removing the wood automatically (`--exclude wood`) damages
  them. The nuthatch's other poses come from the first sheet and from
  `sheet_wings.png`.
- Labels: the large sheet has them in a column of their own (`--skip-first-col`),
  the small one inside the first cell; the script drops them by keeping only the
  largest connected shape in a cell.
- The model keeps the scale neither across a row (the nuthatch) nor between
  sheets. The script therefore takes a row's scale from one reference column
  (`--scale-col`) and allows per-column factors by hand (`--cell-scale`).

### Which pose comes from where (exact calls in the header of `tools/sheet_cut.py`)

| pose | source |
|---|---|
| sitting, pecking, looking back | `sheet_small.png`, `sheet_large.png`, columns 1–3 |
| looking back, great tit | `cell_greattit_lookback.png` |
| landing, crouched | `sheet_wings.png` |
| head down, nuthatch | `sheet_nuthatch.png`, cell 1 |
| sitting, collared dove | `sheet_large.png`, column 5 (in column 1 its neck is the exact grey of the sheet) |

### Adding one pose or one species

Not a new sheet, but a single cell with the matching row as reference (crop the
row from the sheet and upload it as the reference). Template:

```
Single [species], realistic photographic quality, same individual and same
scale as the reference image, on a flat uniform light grey background (RGB
200,200,200), no ground, no shadows, no props. Strict side view facing
LEFT, soft overcast daylight from above. Pose: [description of the pose].
Centred, large in the frame, no text, no watermark.
```

1:1, the same model. `sheet_cut.py` also takes single cells (a file without a
grid is one cell).

A new species: the sheet template above with a single row (`A grid of 5 columns
and 1 row`), the same poses, the same background.
