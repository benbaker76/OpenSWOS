#!/usr/bin/env python3
"""Builds every platform's application icon from one SVG.

    python3 scripts/generate_icons.py

The source of truth is SWOS.svg at the repository root. Everything this writes is
generated and committed, so a normal build needs neither this script nor a rasteriser --
only someone changing the artwork runs it:

    SWOS.ico                          Windows: SWOS.rc links it into SWOS.exe (Explorer, the
                                      taskbar, and SDL's window icon); the installer's
                                      SetupIconFile
    SWOS.icns                         macOS: `make macos-app` copies it into the bundle
    packaging/macos/SWOS.iconset/     the same images unpacked -- what iconutil takes
    packaging/linux/hicolor/          the freedesktop icon theme's sizes
    android/app/src/main/res/drawable-nodpi/openswos_mark.png
                                     Android adaptive launcher's foreground artwork
    packaging/switch/swos_icon.bmp    Switch: the 1024x1024 24-bit BMP AuthoringTool wants
    include/swos_window_icon.h        the window icon on Linux desktops, as RGBA pixels
    wasm/shell.html                   the web build's favicon, inlined as a data: URI

Each size is rasterised from the vector rather than resampled down from one big bitmap. At
16 and 24 pixels that is the difference between a readable mark and a smudge, because the
renderer gets to antialias against the real geometry instead of an already-flattened one.

Below about 20 pixels even that is not enough: detail lands under a pixel wide and averages
into its background whatever the rasteriser does. The fix is not a better filter but a
hand-tuned bitmap, so any PNG dropped into packaging/icons/overrides/ named for its size --
overrides/16.png -- is used verbatim for that size in every output that needs it.

The ICO and ICNS containers are written here rather than shelled out to ImageMagick or
iconutil: both formats are a header and a list of images, and iconutil only exists on macOS,
which would mean the icons could only be regenerated on a Mac. (Pillow writes both too, but
its ICO stores every size as PNG, which some Windows shell paths draw blank below 256, and
its ICNS leaves out the 16 and 32 pixel images.)

Needs cairosvg and Pillow:

    pip install cairosvg pillow
"""

import base64
import io
import struct
import sys
from pathlib import Path

try:
    import cairosvg
    from PIL import Image
except ImportError as exc:  # pragma: no cover - developer tooling
    sys.exit(f"{exc}. See the module docstring for what to install.")

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "SWOS.svg"

# Hand-tuned bitmaps that win over the vector at the size they are named for.
OVERRIDES = ROOT / "packaging" / "icons" / "overrides"

# Windows shows a different size in almost every context - the taskbar, alt-tab, the title
# bar, large icons in Explorer - and picks the nearest available, scaling if it has to.
# Supplying the ones it actually asks for avoids that.
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]

# The names are fixed: iconutil and the icns type codes below both key off them.
ICONSET = [
    ("icon_16x16.png", 16, b"icp4"),
    ("icon_16x16@2x.png", 32, b"ic11"),
    ("icon_32x32.png", 32, b"icp5"),
    ("icon_32x32@2x.png", 64, b"ic12"),
    ("icon_128x128.png", 128, b"ic07"),
    ("icon_128x128@2x.png", 256, b"ic13"),
    ("icon_256x256.png", 256, b"ic08"),
    ("icon_256x256@2x.png", 512, b"ic14"),
    ("icon_512x512.png", 512, b"ic09"),
    ("icon_512x512@2x.png", 1024, b"ic10"),
]

# The sizes the freedesktop icon theme spec expects to find under hicolor.
HICOLOR_SIZES = [16, 22, 24, 32, 48, 64, 128, 256, 512]
LINUX_ICON_NAME = "openswos"

# The running window's icon on desktops that take it from the program. It is compiled into
# the binary as raw pixels, so it has to stay small: 64x64 is 16 KB.
WINDOW_ICON_SIZE = 64

# Switch: AuthoringTool accepts only a 1024x1024 24-bit uncompressed BMP, and rejects
# anything else with a confusing message about baseline JPEG (docs/SWITCH.md). The artwork
# sits on black, 32 pixels in from each edge.
SWITCH_ICON_SIZE = 1024
SWITCH_ART_SIZE = 960

FAVICON_SIZE = 32


def override_for(size: int) -> Path | None:
    """The hand-tuned bitmap for `size`, if one was dropped in.

    Both `16.png` and `SWOS-16.png` are accepted, because either is the obvious thing to
    call it and nothing is ever written into this directory for the two to collide with.
    """
    found = [path for path in (OVERRIDES / f"{size}.png",
                               OVERRIDES / f"SWOS-{size}.png") if path.exists()]
    if len(found) > 1:
        names = " and ".join(path.name for path in found)
        sys.exit(f"{names} both claim {size}x{size} - keep one")
    return found[0] if found else None


def render(size: int) -> Image.Image:
    """The artwork at `size` x `size`: the override if there is one, else the vector."""
    override = override_for(size)
    if override is not None:
        image = Image.open(override).convert("RGBA")
        if image.size != (size, size):
            sys.exit(
                f"{override.relative_to(ROOT)} is {image.width}x{image.height}, "
                f"but it can only stand in for {size}x{size}"
            )
        return image
    png = cairosvg.svg2png(url=str(SOURCE), output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def write_png(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, "PNG", optimize=True)


def dib_bytes(image: Image.Image) -> bytes:
    """One ICO entry in the old BITMAPINFOHEADER form.

    Windows has read PNG-compressed entries since Vista, but only at 256; some shell paths
    still render the small sizes through the DIB reader, and a PNG there comes out blank.
    So: DIB below 256, PNG at 256.

    Two things about the format catch people out. The header's height is twice the image's,
    because a DIB icon is a colour bitmap with a 1-bit AND mask stacked underneath it. And
    the rows run bottom-up.
    """
    width, height = image.size
    pixels = image.load()

    colour = bytearray()
    for y in range(height - 1, -1, -1):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            colour += bytes((b, g, r, a))

    # The mask is what pre-alpha Windows used to cut the icon out of its background. A 32-bit
    # icon carries its own alpha, so every bit is 0 for "opaque" and the real transparency
    # comes from the colour data. It is still structurally required, and each row still
    # pads out to four bytes.
    mask_stride = ((width + 31) // 32) * 4
    mask = bytes(mask_stride * height)

    header = struct.pack(
        "<IiiHHIIiiII",
        40,             # header size
        width,
        height * 2,     # colour rows plus mask rows
        1,              # planes
        32,             # bits per pixel
        0,              # BI_RGB
        len(colour) + len(mask),
        0, 0, 0, 0,     # resolution and palette, all unused here
    )
    return header + bytes(colour) + mask


def write_ico(path: Path, images: dict[int, Image.Image]) -> None:
    entries = []
    for size in ICO_SIZES:
        image = images[size]
        if size >= 256:
            buffer = io.BytesIO()
            image.save(buffer, "PNG", optimize=True)
            entries.append((size, buffer.getvalue()))
        else:
            entries.append((size, dib_bytes(image)))

    # 6-byte ICONDIR, then a 16-byte ICONDIRENTRY each, then the images.
    offset = 6 + 16 * len(entries)
    directory = struct.pack("<HHH", 0, 1, len(entries))
    for size, data in entries:
        directory += struct.pack(
            "<BBBBHHII",
            # 256 does not fit in a byte and is written as 0; the format has no way to
            # describe anything larger.
            0 if size >= 256 else size,
            0 if size >= 256 else size,
            0,          # palette colours
            0,          # reserved
            1,          # planes
            32,         # bits per pixel
            len(data),
            offset,
        )
        offset += len(data)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(directory + b"".join(data for _, data in entries))


def write_icns(path: Path, images: dict[int, Image.Image]) -> None:
    """An icns file: the magic, the total length, then typed PNG chunks."""
    chunks = bytearray()
    for _, size, code in ICONSET:
        buffer = io.BytesIO()
        images[size].save(buffer, "PNG", optimize=True)
        data = buffer.getvalue()
        # The length in a chunk header counts the header itself.
        chunks += code + struct.pack(">I", len(data) + 8) + data

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"icns" + struct.pack(">I", len(chunks) + 8) + bytes(chunks))


def write_switch_bmp(path: Path) -> None:
    art = render(SWITCH_ART_SIZE)
    icon = Image.new("RGB", (SWITCH_ICON_SIZE, SWITCH_ICON_SIZE), (0, 0, 0))
    margin = (SWITCH_ICON_SIZE - SWITCH_ART_SIZE) // 2
    icon.paste(art, (margin, margin), art)
    path.parent.mkdir(parents=True, exist_ok=True)
    icon.save(path, "BMP")          # Pillow writes RGB as 24-bit uncompressed
    if path.stat().st_size < SWITCH_ICON_SIZE * SWITCH_ICON_SIZE * 3:
        sys.exit(f"{path.relative_to(ROOT)} is smaller than a 24-bit uncompressed BMP would be")


def write_window_icon_header(path: Path, image: Image.Image) -> None:
    """RGBA pixels, top row first, for SDL_CreateRGBSurfaceWithFormatFrom: SDL has no PNG
    decoder of its own, and at this size the pixels are small enough to carry as an array."""
    size = image.width
    data = image.tobytes()
    rows = "\n".join(
        "    " + ", ".join(f"0x{byte:02x}" for byte in data[i:i + 16]) + ","
        for i in range(0, len(data), 16)
    )
    path.write_text(
        "/* Generated by scripts/generate_icons.py from SWOS.svg -- do not edit. */\n"
        "#ifndef SWOS_WINDOW_ICON_H\n"
        "#define SWOS_WINDOW_ICON_H\n\n"
        "#include <stdint.h>\n\n"
        f"#define SWOS_WINDOW_ICON_SIZE {size}\n\n"
        f"/* {size}x{size} pixels, RGBA (SDL_PIXELFORMAT_RGBA32), top row first. */\n"
        f"static const uint8_t k_swosWindowIcon[{size} * {size} * 4] = {{\n"
        f"{rows}\n"
        "};\n\n"
        "#endif /* SWOS_WINDOW_ICON_H */\n"
    )


def write_favicon(path: Path, image: Image.Image) -> None:
    """The favicon, inlined into the shell page, so the web build needs no file of its own
    beside SWOS.html. Replaces the page's icon link if it has one, else follows <title>."""
    buffer = io.BytesIO()
    image.save(buffer, "PNG", optimize=True)
    tag = ('  <link rel="icon" type="image/png" href="data:image/png;base64,'
           + base64.b64encode(buffer.getvalue()).decode("ascii") + '">')

    text = path.read_bytes().decode("utf-8")
    crlf = "\r\n" in text
    lines = text.replace("\r\n", "\n").split("\n")
    icons = [i for i, line in enumerate(lines) if 'rel="icon"' in line]
    if len(icons) > 1:
        sys.exit(f"{path.relative_to(ROOT)} has {len(icons)} icon links - keep one")
    if icons:
        lines[icons[0]] = tag
    else:
        titles = [i for i, line in enumerate(lines) if "<title>" in line]
        if len(titles) != 1:
            sys.exit(f"{path.relative_to(ROOT)}: no single <title> line to put the favicon after")
        lines.insert(titles[0] + 1, tag)
    text = "\n".join(lines)
    path.write_bytes((text.replace("\n", "\r\n") if crlf else text).encode("utf-8"))


def main() -> int:
    if not SOURCE.exists():
        sys.exit(f"no source artwork at {SOURCE}")

    wanted = sorted(
        {WINDOW_ICON_SIZE, FAVICON_SIZE}
        | set(ICO_SIZES)
        | {size for _, size, _ in ICONSET}
        | set(HICOLOR_SIZES)
    )
    images = {size: render(size) for size in wanted}
    overridden = [size for size in wanted if override_for(size) is not None]
    print(f"rendered {len(images)} sizes from {SOURCE.relative_to(ROOT)}")
    if overridden:
        sizes = ", ".join(str(size) for size in overridden)
        print(f"  {sizes} taken from {OVERRIDES.relative_to(ROOT)}/ instead")

    ico = ROOT / "SWOS.ico"
    write_ico(ico, images)

    icns = ROOT / "SWOS.icns"
    write_icns(icns, images)

    # The unpacked form as well: it is what iconutil takes, so a Mac can rebuild the icns
    # without this script, and it is far easier to eyeball.
    iconset = ROOT / "packaging" / "macos" / "SWOS.iconset"
    for name, size, _ in ICONSET:
        write_png(images[size], iconset / name)

    hicolor = ROOT / "packaging" / "linux" / "hicolor"
    for size in HICOLOR_SIZES:
        write_png(images[size], hicolor / f"{size}x{size}" / "apps" / f"{LINUX_ICON_NAME}.png")

    # Android's adaptive-icon XML supplies its background and safe-area sizing.
    write_png(images[512], ROOT / "android" / "app" / "src" / "main" / "res" /
              "drawable-nodpi" / "openswos_mark.png")

    switch = ROOT / "packaging" / "switch" / "swos_icon.bmp"
    write_switch_bmp(switch)

    window = ROOT / "include" / "swos_window_icon.h"
    write_window_icon_header(window, images[WINDOW_ICON_SIZE])

    shell = ROOT / "wasm" / "shell.html"
    write_favicon(shell, images[FAVICON_SIZE])

    for path in (ico, icns, switch, window, shell):
        print(f"  {path.relative_to(ROOT)}  {path.stat().st_size:,} bytes")
    print(f"  {iconset.relative_to(ROOT)}/  {len(ICONSET)} files")
    print(f"  {hicolor.relative_to(ROOT)}/  {len(HICOLOR_SIZES)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
