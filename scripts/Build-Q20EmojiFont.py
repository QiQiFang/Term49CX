"""Build the BMP-mapped monochrome emoji font used by Term49CX on BB10."""

from pathlib import Path
import sys

from fontTools.ttLib import TTFont


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "external/fonts/NotoEmoji-Regular.ttf"
OUTPUT = ROOT / "external/fonts/NotoEmoji-Q20.ttf"


def main() -> int:
    font = TTFont(SOURCE)
    source_cmap = font.getBestCmap()
    bmp_tables = [
        table
        for table in font["cmap"].tables
        if table.format == 4 and table.isUnicode()
    ]
    if not bmp_tables:
        raise RuntimeError("font has no Unicode cmap format 4 table")

    mirrored = 0
    for codepoint in range(0x1F000, 0x1FB00):
        glyph = source_cmap.get(codepoint)
        if glyph is None:
            continue
        private_use = 0xE000 + (codepoint - 0x1F000)
        for table in bmp_tables:
            table.cmap[private_use] = glyph
        mirrored += 1

    font.save(OUTPUT)
    print(f"Wrote {OUTPUT} with {mirrored} emoji mirrored to the BMP PUA")
    return 0


if __name__ == "__main__":
    sys.exit(main())
