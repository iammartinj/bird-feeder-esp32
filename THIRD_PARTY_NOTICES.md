# Third-party notices

## Aquarium_Espresso engine

`main/engine/` (`gfx`, `fastmath`, `stretch`, `Arduino.h`, `fish_art.h`) comes
from **Aquarium_Espresso** by mochimochi-man, adapted for this board.
MIT License, full text in [LICENSES/Aquarium_Espresso-MIT.txt](LICENSES/Aquarium_Espresso-MIT.txt).

## stb_image

`main/stb_image.h` is **stb_image v2.30** by Sean Barrett
(<https://github.com/nothings/stb>), public domain or MIT at your choice; the
licence text is at the end of the file. One local change frees a JPEG
component's coefficients early, marked in the source.

## Espressif components

Fetched at build time by the ESP-IDF component manager, not stored here:
`espressif/esp_lcd_co5300` (Apache-2.0) and ESP-IDF itself (Apache-2.0).

## Artwork

The garden photographs, the feeder cut-out, the bird and squirrel sheets and the
depth map in `art/` (and baked from them into `main/scene/`) were generated with
Magnific (formerly Freepik) on a paid plan, with the Nano Banana Pro and `auto`
models, and retouched there. The prompts are in [art/PROMPTS.md](art/PROMPTS.md).
Magnific's terms apply to these outputs; they are not covered by the MIT licence
of the code.
