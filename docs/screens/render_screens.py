"""Renders the ELRSTx screens for each display driver, with sample data.

Mirrors the layout code of src/DisplaySSD1306.cpp and src/DisplayST7789.cpp
(Adafruit GFX 5x7 font). Keep it in sync when those files change.

    pip install pillow
    python docs/screens/render_screens.py

Output: docs/screens/ssd1306/*.png, docs/screens/st7789/*.png and a contact
sheet per display (all-ssd1306.png, all-st7789.png).
"""
import re
from pathlib import Path
from PIL import Image, ImageDraw

HERE = Path(__file__).parent
ROOT = HERE.parent.parent
FONT_SRC = next((ROOT / ".pio" / "libdeps").glob("*/Adafruit GFX Library/glcdfont.c"))
FONT = [int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})", FONT_SRC.read_text().split("{", 1)[1])]


def rgb565(r, g, b):
    # What the panel actually shows after RGB565 quantisation
    return ((r & 0xF8) | (r >> 5), (g & 0xFC) | (g >> 6), (b & 0xF8) | (b >> 5))


class Gfx:
    """Subset of Adafruit_GFX: rects, round rects and the classic font."""

    def __init__(self, w, h, bg):
        self.w, self.h = w, h
        self.img = Image.new("RGB", (w, h), bg)
        self.d = ImageDraw.Draw(self.img)
        self.wrap = False

    def fill_rect(self, x, y, w, h, c):
        if w > 0 and h > 0:
            self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=c)

    def rect(self, x, y, w, h, c):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], outline=c)

    def round_rect(self, x, y, w, h, r, c, fill=True):
        if fill:
            self.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], r, fill=c)
        else:
            self.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], r, outline=c)

    def text(self, x, y, s, c, size=1):
        cx, cy = x, y
        for ch in s:
            if self.wrap and cx + 6 * size > self.w:
                cx, cy = 0, cy + 8 * size
            for col in range(5):
                bits = FONT[ord(ch) * 5 + col]
                for row in range(8):
                    if bits >> row & 1:
                        self.fill_rect(cx + col * size, cy + row * size, size, size, c)
            cx += 6 * size


def width(s, size=2):
    return len(s) * 6 * size


def frac(v):
    return min(max((v - 172) / 1639, 0.0), 1.0)


def us(v):
    return int((v - 992) * 5 / 8) + 1500


def row_text(r, edited, cols):
    """Label/value fitting shared by both drivers (formatRow / list())."""
    value = ">" if r.get("folder") else r.get("value", "")
    value = value[:11]
    if edited:
        value = "<" + value + ">"
    label = r["label"]
    room = cols - len(value) - 1 if value else cols
    return label[:max(room, 0)], value


# ======================================================================
# SSD1306 128x64 (src/DisplaySSD1306.cpp)
# ======================================================================
class Mono:
    name = "ssd1306"
    ON, OFF = (255, 255, 255), (0, 0, 0)

    def __init__(self):
        self.g = Gfx(128, 64, self.OFF)
        self.g.wrap = True

    def t(self, x, y, s, c=None):
        self.g.text(x, y, s, c or self.ON)

    def header(self, title):
        self.t(0, 0, title)
        self.g.fill_rect(0, 9, 128, 1, self.ON)

    def bar(self, x, y, w, h, f):
        self.g.rect(x, y, w, h, self.ON)
        self.g.fill_rect(x + 1, y + 1, int((w - 2) * f), h - 2, self.ON)

    def inverted(self, y, s):
        self.g.fill_rect(0, y - 1, 128, 9, self.ON)
        self.t(2, y, s, self.OFF)

    def home(self, v):
        self.t(0, 0, v["title"])
        if v["tx_batt"]:
            b = "%.1fV" % v["tx_batt"]
            self.t(128 - len(b) * 6, 0, b)
        self.g.fill_rect(0, 9, 128, 1, self.ON)
        if v["alarm"]:
            self.inverted(12, v["alarm"][1])
        else:
            self.t(0, 12, v["device"])
        self.t(0, 21, "LQ %3u%%  -%udBm" % (v["lq"], v["rssi"]) if v["link"] else "No receiver link")
        rate = "%uHz" % v["rate"] + ("" if v["synced"] else " nosync")
        if v["rx_batt"]:
            rate += "  %.1fV" % v["rx_batt"]
        self.t(0, 30, rate)
        self.gimbal(0, 40, v["gimbal"][0])
        self.gimbal(104, 40, v["gimbal"][1])
        if v["mode"]:
            m = v["mode"][:12]
            self.t(64 - len(m) * 3, 42, m)
        for i in range(4):
            x = 33 + i * 17
            if v["aux"][i]:
                self.g.fill_rect(x, 56, 11, 7, self.ON)
            else:
                self.g.rect(x, 56, 11, 7, self.ON)

    def gimbal(self, x, y, pos):
        S = 24
        self.g.rect(x, y, S, S, self.ON)
        self.g.fill_rect(x + S // 2, y + S // 2, 1, 1, self.ON)
        px = x + 3 + int(pos[0] * (S - 7))
        py = y + 3 + int((1 - pos[1]) * (S - 7))
        self.g.d.ellipse([px - 2, py - 2, px + 2, py + 2], fill=self.ON)

    def list(self, v):
        self.header(v["title"])
        if v.get("busy"):
            self.t(122, 0, "*")
        cur, rows = v["cursor"], v["rows"]
        scroll = max(0, min(cur - 5, len(rows) - 6)) if cur >= 6 else 0
        self.g.wrap = False
        for n in range(6):
            i = scroll + n
            if i >= len(rows):
                break
            r = rows[i]
            if r.get("command"):
                s = "[" + r["label"][:18] + "]"
            else:
                label, value = row_text(r, v.get("editing") and i == cur, 20)
                s = label + " " * (20 - len(label) - len(value)) + value
            y = 12 + n * 9
            if i == cur:
                self.inverted(y, s)
            else:
                self.t(2, y, s)

    def channels(self, ch):
        for i in range(8):
            y = i * 8
            self.t(0, y, str(i + 1))
            self.bar(10, y + 1, 84, 6, frac(ch[i]))
            self.t(100, y, "%4d" % us(ch[i]))

    def telemetry(self, v):
        self.header("Telemetry")
        self.t(0, 12, "Up -%u/-%u LQ%u" % (v["rssi1"], v["rssi2"], v["lq"]))
        self.t(0, 21, "SNR %d  Dn -%u LQ%u" % (v["snr"], v["down_rssi"], v["down_lq"]))
        self.t(0, 30, "%.1fV %.1fA %umAh" % (v["volt"], v["amp"], v["mah"]))
        self.t(0, 39, "P%d R%d Y%d" % v["pry"])
        self.t(0, 48, "GPS %u sats %dm" % (v["sats"], v["alt"]))
        self.t(0, 57, v["mode"])

    def message(self, v):
        self.header(v["title"])
        for i, line in enumerate(v["lines"]):
            y = 12 + i * 9
            if i == v.get("highlight", -1):
                self.inverted(y, line or "")
            elif line:
                self.t(0, y, line)
        if v.get("footer"):
            self.g.fill_rect(0, 55, 128, 9, self.OFF)
            self.t(0, 56, v["footer"])

    def splash(self, version):
        self.t(28, 20, "ELRSTx " + version)
        self.t(28, 34, "starting...")

    def image(self, zoom=4):
        img = self.g.img.resize((128 * zoom, 64 * zoom), Image.NEAREST)
        out = Image.new("RGB", (img.width + 24, img.height + 24), (40, 40, 46))
        out.paste(img, (12, 12))
        return out


# ======================================================================
# ST7789 240x240 RGB565 (src/DisplayST7789.cpp)
# ======================================================================
class Color:
    name = "st7789"
    SIZE = 240
    BG = rgb565(0, 0, 0)
    FG = rgb565(235, 235, 235)
    DIM = rgb565(130, 135, 150)
    HEADER_BG = rgb565(28, 32, 48)
    ACCENT = rgb565(0, 200, 230)
    SELECT_BG = rgb565(0, 70, 140)
    EDIT_BG = rgb565(90, 60, 0)
    GOOD = rgb565(40, 210, 90)
    WARN = rgb565(255, 190, 0)
    CRIT = rgb565(240, 50, 50)
    OUTLINE = rgb565(90, 95, 110)
    HEADER_H, ROW_H, LIST_TOP, LIST_ROWS, COLS = 28, 22, 32, 9, 19

    def __init__(self):
        self.g = Gfx(240, 240, self.BG)

    def t(self, x, y, s, c=None, size=2):
        self.g.text(x, y, s, c or self.FG, size)

    def tr(self, right, y, s, c=None, size=2):
        self.t(right - width(s, size), y, s, c, size)

    def level(self, l):
        return {"crit": self.CRIT, "warn": self.WARN}.get(l, self.GOOD)

    def lq_color(self, lq):
        return self.GOOD if lq >= 80 else self.WARN if lq >= 50 else self.CRIT

    def header(self, title, right=None, rc=None):
        self.g.fill_rect(0, 0, 240, self.HEADER_H, self.HEADER_BG)
        self.g.fill_rect(0, self.HEADER_H - 1, 240, 1, self.ACCENT)
        self.t(8, 6, title, self.ACCENT)
        if right:
            self.tr(232, 6, right, rc)

    def bar(self, x, y, w, h, f, c):
        self.g.rect(x, y, w, h, self.OUTLINE)
        self.g.fill_rect(x + 2, y + 2, int((w - 4) * f), h - 4, c)

    def home(self, v):
        batt = "%.1fV" % v["tx_batt"] if v["tx_batt"] else None
        self.header(v["title"], batt, self.level(v.get("tx_level")))
        self.t(8, 36, v["device"], self.WARN if v["device"] == "No ELRS module" else self.FG)
        if v["link"]:
            self.t(8, 66, "LQ", self.DIM)
            self.t(40, 58, "%u%%" % v["lq"], self.lq_color(v["lq"]), 4)
            self.tr(232, 66, "-%udBm" % v["rssi"])
        else:
            self.t(8, 66, "No receiver link", self.CRIT)
        rate = "%uHz" % v["rate"]
        self.t(8, 100, rate, self.FG if v["synced"] else self.WARN)
        if not v["synced"]:
            self.t(8 + width(rate) + 8, 100, "nosync", self.WARN)
        if v["rx_batt"]:
            self.tr(232, 100, "RX %.1fV" % v["rx_batt"])
        if v["alarm"]:
            self.g.round_rect(4, 124, 232, 26, 4, self.level(v["alarm"][0]))
            self.t((240 - width(v["alarm"][1])) // 2, 129, v["alarm"][1], self.BG)
        elif v["mode"]:
            self.t(8, 129, v["mode"], self.ACCENT)
        self.gimbal(8, 160, v["gimbal"][0])
        self.gimbal(240 - 8 - 70, 160, v["gimbal"][1])
        for i in range(4):
            x, y = 86 + (i % 2) * 36, 162 + (i // 2) * 36
            if v["aux"][i]:
                self.g.round_rect(x, y, 32, 32, 4, self.GOOD)
                self.t(x + 4, y + 9, "A%d" % (i + 1), self.BG)
            else:
                self.g.round_rect(x, y, 32, 32, 4, self.OUTLINE, fill=False)
                self.t(x + 4, y + 9, "A%d" % (i + 1), self.DIM)

    def gimbal(self, x, y, pos, S=70, R=6):
        self.g.rect(x, y, S, S, self.OUTLINE)
        self.g.fill_rect(x + 1, y + S // 2, S - 2, 1, self.HEADER_BG)
        self.g.fill_rect(x + S // 2, y + 1, 1, S - 2, self.HEADER_BG)
        px = x + R + 2 + int(pos[0] * (S - 2 * R - 4))
        py = y + R + 2 + int((1 - pos[1]) * (S - 2 * R - 4))
        self.g.d.ellipse([px - R, py - R, px + R, py + R], fill=self.ACCENT)

    def list(self, v):
        rows, cur, n = v["rows"], v["cursor"], self.LIST_ROWS
        scroll = max(0, min(cur - (n - 1), len(rows) - n)) if cur >= n else 0
        self.header(v["title"], "..." if v.get("busy") else None, self.DIM)
        for k in range(n):
            i = scroll + k
            if i >= len(rows):
                break
            r = rows[i]
            y = self.LIST_TOP + k * self.ROW_H
            sel = i == cur
            edited = sel and v.get("editing")
            if sel:
                self.g.fill_rect(0, y, 234, self.ROW_H, self.EDIT_BG if edited else self.SELECT_BG)
            rr = dict(r, label="[" + r["label"] + "]") if r.get("command") else r
            label, value = row_text(rr, edited, self.COLS)
            self.t(4, y + 3, label, self.ACCENT if r.get("command") else self.FG)
            if value:
                c = self.WARN if edited else self.DIM if r.get("folder") else self.ACCENT
                self.tr(230, y + 3, value, c)
        if len(rows) > n:
            track = n * self.ROW_H
            h = max(track * n // len(rows), 10)
            y = self.LIST_TOP + (track - h) * scroll // (len(rows) - n)
            self.g.fill_rect(236, self.LIST_TOP, 4, track, self.HEADER_BG)
            self.g.fill_rect(236, y, 4, h, self.OUTLINE)

    def channels(self, ch):
        self.header("Channels")
        for i in range(12):
            y = 34 + i * 17
            self.tr(22, y + 1, str(i + 1), self.DIM, 1)
            self.bar(30, y, 150, 13, frac(ch[i]), self.ACCENT if i < 4 else self.GOOD)
            self.tr(232, y + 3, str(us(ch[i])), self.FG, 1)

    def telemetry(self, v):
        self.header("Telemetry")
        y = [34]

        def line(label, value, c=None):
            self.t(8, y[0], label, self.DIM)
            self.tr(232, y[0], value, c)
            y[0] += self.ROW_H

        line("RSSI", "-%u/-%udBm" % (v["rssi1"], v["rssi2"]))
        line("LQ up", "%u%%" % v["lq"], self.lq_color(v["lq"]))
        line("LQ down", "%u%% -%u" % (v["down_lq"], v["down_rssi"]), self.lq_color(v["down_lq"]))
        line("SNR", "%d dB" % v["snr"])
        line("Batt", "%.1fV %.1fA" % (v["volt"], v["amp"]))
        line("Used", "%umAh" % v["mah"])
        line("P R Y", "%d %d %d" % v["pry"])
        line("GPS", "%u sat %dm" % (v["sats"], v["alt"]))
        line("Mode", v["mode"], self.ACCENT)

    def message(self, v):
        self.header(v["title"])
        self.g.wrap = True
        for i, line in enumerate(v["lines"]):
            y = self.LIST_TOP + i * self.ROW_H
            if i == v.get("highlight", -1):
                self.g.fill_rect(0, y, 240, self.ROW_H, self.SELECT_BG)
            if line:
                self.t(8, y + 3, line)
        self.g.wrap = False
        if v.get("footer"):
            self.g.fill_rect(0, 214, 240, 26, self.HEADER_BG)
            self.t(8, 219, v["footer"], self.DIM)

    def splash(self, version):
        self.t(48, 84, "ELRSTx", self.ACCENT, 4)
        self.tr(192, 124, version, self.DIM)
        self.t(54, 160, "starting...")

    def image(self, zoom=2):
        img = self.g.img.resize((240 * zoom, 240 * zoom), Image.NEAREST)
        out = Image.new("RGB", (img.width + 24, img.height + 24), (40, 40, 46))
        out.paste(img, (12, 12))
        return out


# ======================================================================
# Sample data
# ======================================================================
CH = [992, 1210, 172, 940, 1811, 172, 992, 172, 1400, 992, 600, 992, 992, 992, 992, 992]
HOME = dict(title="Model 1", tx_batt=7.8, tx_level=None, device="DIY2400 TX ESP32 SX", link=True, lq=100,
            rssi=42, rate=250, synced=True, rx_batt=12.4, alarm=None, mode="ANGL",
            gimbal=[(frac(CH[0]), frac(CH[2])), (frac(CH[3]), frac(CH[1]))], aux=[c > 992 for c in CH[4:8]])
ELRS_ROOT = [dict(label="Packet Rate", value="250Hz(-108dBm)"), dict(label="Telem Ratio", value="Std"),
             dict(label="Switch Mode", value="Wide"), dict(label="Model Match", value="Off"),
             dict(label="TX Power", folder=True), dict(label="VTX Administrator", folder=True),
             dict(label="WiFi Connectivity", folder=True), dict(label="Bind", command=True)]
SETTINGS = [("Deadband", "20"), ("Invert roll", "off"), ("Invert pitch", "off"), ("Invert thr", "off"),
            ("Invert yaw", "off"), ("Safe start", "ON"), ("Link alarm", "ON"), ("Link timeout", "1000ms"),
            ("TX cells", "2S"), ("Warn mV/cell", "3500"), ("Crit mV/cell", "3300"), ("Simulation", "off")]
TELEM = dict(rssi1=42, rssi2=45, lq=100, snr=9, down_rssi=38, down_lq=96, volt=12.4, amp=3.2, mah=450,
             pry=(2, -1, 178), sats=11, alt=34, mode="ANGL")

SCREENS = [
    ("00-splash", "Inicializacao", lambda d: d.splash("2.1.0")),
    ("01-home", "Inicial", lambda d: d.home(HOME)),
    ("02-home-alarm", "Inicial com alarme",
     lambda d: d.home(dict(HOME, link=False, alarm=("crit", "LINK LOST"), gimbal=[(0.5, 0.0), (0.5, 0.5)], tx_batt=6.9, tx_level="warn"))),
    ("03-menu", "Menu", lambda d: d.list(dict(title="Menu", cursor=0, rows=[dict(label=l) for l in [
        "ExpressLRS", "Channels", "Telemetry", "Calibrate", "Settings", "Profiles", "WiFi / OTA", "Info"]]))),
    ("04-elrs", "ExpressLRS", lambda d: d.list(dict(title="ExpressLRS", cursor=0, rows=ELRS_ROOT))),
    ("05-elrs-edit", "ExpressLRS editando", lambda d: d.list(dict(
        title="ExpressLRS", cursor=0, editing=True,
        rows=[dict(label="Packet Rate", value="500Hz(-105dBm)")] + ELRS_ROOT[1:]))),
    ("06-elrs-folder", "Pasta TX Power", lambda d: d.list(dict(title="TX Power", cursor=0, busy=True, rows=[
        dict(label="Max Power", value="100mW"), dict(label="Dynamic", value="Off"),
        dict(label="Fan Thresh", value="250mW")]))),
    ("07-elrs-bind", "Comando Bind", lambda d: d.message(dict(title="Bind", lines=["Binding..."],
                                                               footer="BACK=stop"))),
    ("08-channels", "Canais", lambda d: d.channels(CH)),
    ("09-telemetry", "Telemetria", lambda d: d.telemetry(TELEM)),
    ("10-calibrate", "Calibrar", lambda d: d.message(dict(title="Calibrate", lines=[
        "2. Move to limits", "   then press OK", "R  112 2051 3990", "P  101 3550 4002",
        "T   98  160 4011", "Y  130 2040 3975"]))),
    ("11-settings", "Ajustes", lambda d: d.list(dict(title="Settings *", cursor=0, editing=True,
                                                     rows=[dict(label=l, value=v) for l, v in SETTINGS]))),
    ("12-profiles", "Perfis", lambda d: d.list(dict(title="Profiles", cursor=1, rows=[dict(label=l) for l in [
        "0 Model 1 *", "1 Quad 5in", "2 Wing", "3 Profile 3"]]))),
    ("13-wifi", "WiFi / OTA", lambda d: d.message(dict(title="WiFi / OTA", highlight=0, lines=[
        "WiFi AP: ON", None, "ELRSTx-A1B2", "pw elrstx1234", "http://192.168.4.1"]))),
    ("14-info", "Info", lambda d: d.message(dict(title="Info", lines=[
        "FW 2.1.0", "Frames 75120", "Telem 18342/3 bad", "Heap 214560", "Inputs: GPIO", "Up 301s"]))),
]


def sheet(shots, path, cols=5):
    tw, th = shots[0][1].size
    cell_w, cell_h = tw + 20, th + 36
    rows = (len(shots) + cols - 1) // cols
    out = Image.new("RGB", (cols * cell_w + 20, rows * cell_h + 20), "white")
    d = ImageDraw.Draw(out)
    for i, (label, img) in enumerate(shots):
        x, y = 20 + (i % cols) * cell_w, 20 + (i // cols) * cell_h
        out.paste(img, (x, y))
        d.text((x, y + th + 6), label, fill="black")
    out.save(path)


def main():
    for old in HERE.glob("[0-9][0-9]-*.png"):
        old.unlink()  # flat layout from the first version
    for driver in (Mono, Color):
        folder = HERE / driver.name
        folder.mkdir(exist_ok=True)
        shots = []
        for name, label, fn in SCREENS:
            d = driver()
            fn(d)
            img = d.image()
            img.save(folder / f"{name}.png")
            shots.append((label, img))
        sheet(shots, HERE / f"all-{driver.name}.png")


if __name__ == "__main__":
    main()
