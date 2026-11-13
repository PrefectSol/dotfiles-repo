#!/usr/bin/env python3
import os, struct, zlib

repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
pal = {}
with open(os.path.join(repo, "theme/palette")) as f:
    for line in f:
        if line.strip():
            k, v = line.split()
            pal[k] = v

c = lambda k: "#" + pal[str(k)]
rgba = lambda k, a=255: tuple(int(pal[str(k)][i:i + 2], 16) for i in (0, 2, 4)) + (a,)
CLEAR = (0, 0, 0, 0)


def png(path, w, h, px):
    raw = b"".join(b"\0" + b"".join(bytes(px(x, y)) for x in range(w)) for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def vtrgb():
    rows = [",".join(str(int(pal[str(i)][j:j + 2], 16)) for i in range(16)) for j in (0, 2, 4)]
    with open(os.path.join(repo, "system/etc/vtrgb"), "w") as f:
        f.write("\n".join(rows) + "\n")


FONT, FONT_SRC, FONT_SIZE, CHAR = "Mono Regular 32", "JetBrainsMonoNerdFont-SemiBold.ttf", 32, 19
ITEMS, ITEM_H, ITEM_GAP = range(2, 13), 48, 12
LINE, PAD, BAR, INNER = 2, 16, 4, 48
BOX_W = 36 * CHAR + 2 * INNER
FRAME = 2 * (INNER - ITEM_GAP) - ITEM_GAP


def grub():
    d = os.path.join(repo, "theme/grub")
    for n in ITEMS:
        h = n * (ITEM_H + ITEM_GAP) + FRAME
        border = lambda x, y: x < LINE or y < LINE or x >= BOX_W - LINE or y >= h - LINE
        png(os.path.join(d, f"frame-{n}.png"), BOX_W, h, lambda x, y: rgba(8) if border(x, y) else CLEAR)
    png(os.path.join(d, "sel_w.png"), PAD, 1, lambda x, y: rgba("acc") if x < BAR else rgba("sel"))
    png(os.path.join(d, "sel_c.png"), 1, 1, lambda x, y: rgba("sel"))
    png(os.path.join(d, "sel_e.png"), PAD, 1, lambda x, y: rgba("sel"))

    l = -BOX_W // 2
    pos = lambda v: "50%%%+d" % v

    parts = [("boot", 7), ("enter", "acc"), ("edit", 7), ("e", "acc"), ("console", 7), ("c", "acc")]
    footer, col = [], 0
    for i, (text, color) in enumerate(parts):
        footer.append((text, color, col))
        col += (len(text) + (1 if i % 2 == 0 else 2)) * CHAR
    right = (col - 2 * CHAR) + 2 * CHAR

    out = f'''title-text: ""
desktop-color: "{c(0)}"
terminal-font: "{FONT}"
terminal-left: "0"
terminal-top: "0"
terminal-width: "100%"
terminal-height: "100%"
terminal-border: "0"

+ image {{
  left = {pos(l)}
  top = 50%-@HALF@
  width = {BOX_W}
  height = @BOX_H@
  file = "frame.png"
}}

+ boot_menu {{
  left = {pos(l + INNER - PAD)}
  top = 50%-@MENU_TOP@
  width = {BOX_W - 2 * (INNER - PAD)}
  height = @MENU_H@
  item_font = "{FONT}"
  item_color = "{c(7)}"
  selected_item_color = "{c(15)}"
  item_height = {ITEM_H}
  item_spacing = {ITEM_GAP}
  item_padding = 0
  icon_width = 0
  icon_height = 0
  item_icon_space = 0
  scrollbar = false
  selected_item_pixmap_style = "sel_*.png"
}}

+ label {{
  left = {pos(l)}
  top = 50%+@LABEL_TOP@
  width = {BOX_W}
  align = "center"
  id = "__timeout__"
  text = "booting in %d seconds"
  color = "{c(8)}"
  font = "{FONT}"
}}
'''
    for text, color, x in footer:
        out += f'''
+ label {{
  left = 100%-{right - x}
  top = 100%-72
  width = {len(text) * CHAR}
  text = "{text}"
  color = "{c(color)}"
  font = "{FONT}"
}}
'''
    with open(os.path.join(d, "theme.txt"), "w") as f:
        f.write(out)
    with open(os.path.join(d, "layout"), "w") as f:
        f.write(f"{ITEM_H + ITEM_GAP} {FRAME} {INNER - ITEM_GAP} {ITEM_GAP} {2 * CHAR} {ITEMS[0]} {ITEMS[-1]}\n")
    with open(os.path.join(d, "font"), "w") as f:
        f.write(f"{FONT_SRC} {FONT_SIZE}\n")


def dunst():
    body = f'''[global]
    monitor = 0
    follow = keyboard
    width = (240, 420)
    height = (0, 300)
    origin = top-right
    offset = (5, 42)
    notification_limit = 5
    indicate_hidden = yes
    progress_bar = true
    progress_bar_frame_width = 0
    progress_bar_corner_radius = 0
    progress_bar_min_width = 200
    progress_bar_max_width = 380
    highlight = "{c("acc")}"
    separator_height = 2
    separator_color = frame
    padding = 12
    horizontal_padding = 14
    text_icon_padding = 12
    frame_width = 2
    gap_size = 6
    corner_radius = 0
    sort = yes
    font = monospace 12
    markup = full
    format = "<b>%s</b>\\n%b"
    alignment = left
    vertical_alignment = center
    show_age_threshold = 60
    ellipsize = middle
    stack_duplicates = true
    show_indicators = no
    icon_position = left
    max_icon_size = 48
    sticky_history = yes
    history_length = 30
    browser = /usr/bin/xdg-open
    mouse_left_click = close_current
    mouse_middle_click = do_action, close_current
    mouse_right_click = close_all

[urgency_low]
    background = "{c(0)}"
    foreground = "{c(7)}"
    frame_color = "{c(8)}"
    timeout = 4

[urgency_normal]
    background = "{c(0)}"
    foreground = "{c(15)}"
    frame_color = "{c("acc")}"
    timeout = 6

[urgency_critical]
    background = "{c(0)}"
    foreground = "{c(15)}"
    frame_color = "{c(1)}"
    timeout = 0
'''
    with open(os.path.join(repo, "theme/dunstrc"), "w") as f:
        f.write(body)


def zathura():
    body = f'''set font "monospace 11"
set selection-clipboard clipboard
set guioptions ""
set default-bg "{c(0)}"
set default-fg "{c(7)}"
set statusbar-bg "{c(0)}"
set statusbar-fg "{c(7)}"
set inputbar-bg "{c(0)}"
set inputbar-fg "{c(15)}"
set notification-bg "{c(0)}"
set notification-fg "{c(7)}"
set notification-error-bg "{c(0)}"
set notification-error-fg "{c(1)}"
set notification-warning-bg "{c(0)}"
set notification-warning-fg "{c(3)}"
set completion-bg "{c(0)}"
set completion-fg "{c(7)}"
set completion-highlight-bg "{c("sel")}"
set completion-highlight-fg "{c(15)}"
set highlight-color "{c("acc")}"
set highlight-active-color "{c("acchl")}"
'''
    with open(os.path.join(repo, "theme/zathurarc"), "w") as f:
        f.write(body)


def panel():
    order = [0, "sel", 7, 15, 8, "acc", 15, 2, 1, 3, 5]
    body = "static const char *colors[] = {\n" + "".join(f'\t"{c(k)}",\n' for k in order) + "};\n"
    with open(os.path.join(repo, "pkgs/distro-panel/panel/colors.h"), "w") as f:
        f.write(body)


def hdr(path, body):
    with open(os.path.join(repo, path), "w") as f:
        f.write(body)


def suckless():
    names = [("bg", 0), ("sel", "sel"), ("fg", 7), ("fgsel", 15), ("dim", 8), ("accent", "acc"), ("urg", 1), ("line", "line")]
    w = max(len(n) for n, _ in names)
    hdr("pkgs/dwm/dwm/colors.h", "".join(f'static const char col_{n}[]{" " * (w - len(n))} = "{c(k)}";\n' for n, k in names))
    hdr("pkgs/dmenu/dmenu/colors.h", f"""static const char *colors[SchemeLast][2] = {{
	[SchemeNorm]   = {{ "{c(7)}", "{c(0)}" }},
	[SchemeSel]    = {{ "{c(15)}", "{c("sel")}" }},
	[SchemeOut]    = {{ "{c(0)}", "{c("acc")}" }},
	[SchemeNormHl] = {{ "{c("acc")}", "{c(0)}" }},
	[SchemeSelHl]  = {{ "{c("acchl")}", "{c("sel")}" }},
	[SchemeSep]    = {{ "{c(7)}", "{c(0)}" }},
}};
static const char *bordercolor = "{c("acc")}";
""")
    hdr("pkgs/st/st/colors.h", "static const char *colorname[] = {\n"
        + "".join(f'\t"{c(i)}",\n' for i in range(16))
        + f'\n\t[255] = 0,\n\n\t"{c(15)}",\n\t"{c(8)}",\n\t"{c(7)}",\n\t"{c(0)}",\n}};\n')


vtrgb()
grub()
dunst()
zathura()
panel()
suckless()
png(os.path.join(repo, "theme/solid.png"), 1, 1, lambda x, y: rgba(0))
