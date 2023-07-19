# Graphics output support

Graphics modes in UEFI are driven via the EFI\_GRAPHICS\_OUTPUT\_PROTOCOL 
or "GOP" interface.

**NOTE**: They are not guaranteed to be available, so we may need to
fall back to text console support. The only thing UEFI guarantees is
an 80x25 text console.

## Graphical Capabilities

We can:

 - query a mode (by its id, a uint starting at 0)
   - height & width
   - bpp
   - RGBx/BGRx pixel format
   - pixel R/G/B/x channel bitmasks
 - set a mode (by its id)
 - blit a rectangle to or from the active framebuffer
 
No other primitives are available to us.

## What we need to build

 - process a font into bitmaps (identified by glyph)
 - process a unicode string into a glpyh list
 - render a glpyh to a buffer using the graphics mode pixel format
   - track space used as we do so
 - render a glyph array, character by character

