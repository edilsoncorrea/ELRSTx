"""Renders the ELRSTx menu screens as they appear on the ST7789 240x240.

Reproduces the drawing code in src/UI.cpp (128x64 1-bit canvas, Adafruit GFX
5x7 font) with sample data, then applies the same scaling as present():
240x120 centred on the 240x240 panel.

    pip install pillow
    python docs/screens/render_screens.py
"""
import re
from pathlib import Path
from PIL import Image, ImageDraw

HERE = Path(__file__).parent
ROOT = HERE.parent.parent
FONT_SRC = next((ROOT / ".pio" / "libdeps").glob("*/Adafruit GFX Library/glcdfont.c"))
FONT = [int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})", FONT_SRC.read_text().split("{", 1)[1])]

W, H = 128, 64
TFT, VIEW_H = 240, 120
ZOOM = 2  # output pixels per panel pixel


class Canvas:
    def __init__(self):
        self.px = [[0] * W for _ in range(H)]
        self.color = 1
        self.cx = self.cy = 0

    def pixel(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = c

    def fill_rect(self, x, y, w, h, c=1):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.pixel(xx, yy, c)

    def rect(self, x, y, w, h, c=1):
        self.fill_rect(x, y, w, 1, c)
        self.fill_rect(x, y + h - 1, w, 1, c)
        self.fill_rect(x, y, 1, h, c)
        self.fill_rect(x + w - 1, y, 1, h, c)

    def hline(self, x, y, w):
        self.fill_rect(x, y, w, 1)

    def char(self, x, y, ch):
        code = ord(ch)
        for col in range(5):
            bits = FONT[code * 5 + col]
            for row in range(8):
                if bits >> row & 1:
                    self.pixel(x + col, y + row, self.color)

    def text(self, x, y, s):
        # GFX print with wrap on: a char that would pass the right edge moves to the next line
        self.cx, self.cy = x, y
        for ch in s:
            if self.cx + 6 > W:
                self.cx, self.cy = 0, self.cy + 8
            self.char(self.cx, self.cy, ch)
            self.cx += 6

    def header(self, title):
        self.text(0, 0, title)
        self.hline(0, 9, 128)

    def bar(self, x, y, w, h, f):
        self.rect(x, y, w, h)
        self.fill_rect(x + 1, y + 1, int((w - 2) * f), h - 2)

    def list(self, title, items, cursor, scroll=0):
        self.header(title)
        if cursor < scroll:
            scroll = cursor
        if cursor >= scroll + 6:
            scroll = cursor - 5
        for row in range(6):
            i = scroll + row
            if i >= len(items):
                break
            y = 12 + row * 9
            if i == cursor:
                self.fill_rect(0, y - 1, 128, 9, 1)
                self.color = 0
            self.text(2, y, items[i])
            self.color = 1

    def to_panel(self):
        # Same mapping as present() in UI.cpp
        img = Image.new("RGB", (TFT, TFT), (0, 0, 0))
        top = (TFT - VIEW_H) // 2
        for y in range(VIEW_H):
            sy = y * 64 // VIEW_H
            for x in range(TFT):
                if self.px[sy][x * 128 // TFT]:
                    img.putpixel((x, top + y), (255, 255, 255))
        return img


def frac(v):
    return min(max((v - 172) / (1811 - 172), 0.0), 1.0)


def pad(label, value, width=20):
    return label + " " * max(width - len(label) - len(value), 0) + value


# ---------- sample state ----------
CH = [992, 1210, 172, 940, 1811, 172, 992, 172, 992, 992, 992, 992, 992, 992, 992, 992]


def home(c, alarm=None):
    c.text(0, 0, "Model 1")
    c.text(128 - 4 * 6, 0, "7.8V")
    c.hline(0, 9, 128)
    c.text(0, 12, "DIY2400 TX ESP32 SX")
    c.text(0, 21, "LQ 100%  -42dBm")
    c.text(0, 30, "250Hz  12.4V")
    if alarm:
        c.fill_rect(0, 38, 128, 10, 1)
        c.color = 0
        c.text(2, 39, alarm)
        c.color = 1
    else:
        c.text(0, 39, "ANGL")
    for a, ch in enumerate([0, 1, 2, 3]):
        c.bar(a * 32, 50, 30, 6, frac(CH[ch]))
    for i in range(4):
        if CH[4 + i] > 992:
            c.fill_rect(i * 32 + 11, 58, 8, 6, 1)
        else:
            c.rect(i * 32 + 11, 58, 8, 6)


def menu(c):
    c.list("Menu", ["ExpressLRS", "Channels", "Telemetry", "Calibrate", "Settings",
                    "Profiles", "WiFi / OTA", "Info"], 0)


ELRS_ROOT = [("Packet Rate", "250Hz(-108d"), ("Telem Ratio", "Std"), ("Switch Mode", "Wide"),
             ("Model Match", "Off"), ("TX Power", ">"), ("VTX Administrator", ">"),
             ("WiFi Connectivity", ">"), ("[Bind]", None)]


def elrs_item(name, value, edited=False):
    if value is None:
        return name
    if edited:
        value = "<" + value + ">"
    room = 20 - len(value) - 1
    return pad(name[:max(room, 0)], value)


def elrs(c):
    c.list("ExpressLRS", [elrs_item(n, v) for n, v in ELRS_ROOT], 0)


def elrs_edit(c):
    items = [elrs_item(n, v) for n, v in ELRS_ROOT]
    items[0] = elrs_item("Packet Rate", "500Hz(-105d", edited=True)
    c.list("ExpressLRS", items, 0)


def elrs_power(c):
    items = [elrs_item("Max Power", "100"), elrs_item("Dynamic", "Off"),
             elrs_item("Fan Thresh", "250mW"), elrs_item("Units", "mW")]
    c.list("TX Power", items, 0)


def elrs_bind(c):
    c.header("Bind")
    c.text(0, 14, "Binding...")
    c.text(0, 56, "Running.. BACK=stop")


def channels(c):
    for i in range(8):
        y = i * 8
        c.text(0, y, str(i + 1))
        c.bar(10, y + 1, 84, 6, frac(CH[i]))
        c.text(100, y, "%4d" % ((CH[i] - 992) * 5 // 8 + 1500))


def telemetry(c):
    c.header("Telemetry")
    c.text(0, 12, "Up -42/-45 LQ100")
    c.text(0, 21, "SNR 9  Dn -38 LQ100")
    c.text(0, 30, "12.4V 3.2A 450mAh")
    c.text(0, 39, "P2 R-1 Y178")
    c.text(0, 48, "GPS 11 sats 34m")
    c.text(0, 57, "ANGL")


def calibrate(c):
    c.header("Calibrate")
    c.text(0, 12, "2. Move all sticks to")
    c.text(0, 21, "their limits, then OK")
    for a, (n, lo, v, hi) in enumerate([("R", 112, 2051, 3990), ("T", 98, 160, 4011),
                                        ("Y", 130, 2040, 3975), ("P", 101, 3550, 4002)]):
        c.text(0, 30 + a * 8, "%s %4d %4d %4d" % (n, lo, v, hi))


def settings(c):
    vals = [("Deadband", "20"), ("Invert roll", "off"), ("Invert pitch", "off"),
            ("Invert thr", "off"), ("Invert yaw", "off"), ("Safe start", "ON")]
    items = [pad(l, v) for l, v in vals]
    items[0] = pad("Deadband", "<20>")
    c.list("Settings *", items, 0)


def profiles(c):
    c.list("Profiles", ["0 Model 1 *", "1 Quad 5in", "2 Wing", "3 Profile 3"], 1)


def wifi(c):
    c.header("WiFi / OTA")
    c.fill_rect(0, 11, 128, 9, 1)
    c.color = 0
    c.text(2, 12, "WiFi AP: ON")
    c.color = 1
    c.text(0, 30, "ELRSTx-A1B2")
    c.text(0, 39, "pw elrstx1234")
    c.text(0, 48, "http://192.168.4.1")


def info(c):
    c.header("Info")
    c.text(0, 12, "FW 2.1.0")
    c.text(0, 21, "Frames 75120")
    c.text(0, 30, "Telem 18342/3 bad")
    c.text(0, 39, "Heap 214560")
    c.text(0, 48, "Inputs: GPIO")
    c.text(0, 57, "Up 301s")


SCREENS = [
    ("01-home", "Inicial", home),
    ("02-home-alarm", "Inicial com alarme", lambda c: home(c, "LINK LOST")),
    ("03-menu", "Menu", menu),
    ("04-elrs", "ExpressLRS", elrs),
    ("05-elrs-edit", "ExpressLRS editando", elrs_edit),
    ("06-elrs-folder", "Pasta TX Power", elrs_power),
    ("07-elrs-bind", "Comando Bind", elrs_bind),
    ("08-channels", "Canais", channels),
    ("09-telemetry", "Telemetria", telemetry),
    ("10-calibrate", "Calibrar", calibrate),
    ("11-settings", "Ajustes", settings),
    ("12-profiles", "Perfis", profiles),
    ("13-wifi", "WiFi / OTA", wifi),
    ("14-info", "Info", info),
]


def framed(panel):
    """Panel scaled up with a dark bezel, like the physical module."""
    big = panel.resize((TFT * ZOOM, TFT * ZOOM), Image.NEAREST)
    out = Image.new("RGB", (big.width + 24, big.height + 24), (60, 60, 66))
    out.paste(big, (12, 12))
    return out


def main():
    shots = []
    for name, label, fn in SCREENS:
        c = Canvas()
        fn(c)
        img = framed(c.to_panel())
        img.save(HERE / f"{name}.png")
        shots.append((label, img))

    cols = 4
    tw, th = shots[0][1].size
    sheet_w, cell_h = cols * (tw + 20) + 20, th + 40
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (sheet_w, rows * cell_h + 20), "white")
    d = ImageDraw.Draw(sheet)
    for i, (label, img) in enumerate(shots):
        x, y = 20 + (i % cols) * (tw + 20), 20 + (i // cols) * cell_h
        sheet.paste(img, (x, y))
        d.text((x, y + th + 6), label, fill="black")
    sheet.save(HERE / "all-screens.png")


if __name__ == "__main__":
    main()
