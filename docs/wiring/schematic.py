"""Esquemáticos do ELRSTx. Gerar: python docs/wiring/schematic.py  (requer: pip install schemdraw matplotlib)"""
from pathlib import Path

import schemdraw
import schemdraw.elements as elm

OUT = Path(__file__).parent

BOARDS = {
    "esp32dev": {
        "title": "ELRSTx - ESP32 DevKit (ESP32-WROOM-32)",
        "chip": "ESP32 DevKit",
        "left": [("GPIO36", "ROLL"), ("GPIO39", "THROTTLE"), ("GPIO34", "YAW"), ("GPIO35", "PITCH"),
                 ("GPIO32", "VBAT_SENSE"), ("GPIO4", "BTN_UP"), ("GPIO13", "BTN_DOWN"), ("GPIO14", "BTN_OK"),
                 ("GPIO27", "BTN_BACK"), ("GPIO18", "AUX1_ARM"), ("GPIO19", "AUX2"), ("GPIO23", "AUX3"),
                 ("GPIO26", "AUX4"), ("GPIO33", "BTN_ADC")],
        "right": [("GPIO21", "SDA"), ("GPIO22", "SCL"), ("GPIO17", "CRSF_TX"), ("GPIO16", "CRSF_RX"),
                  ("GPIO25", "BUZZER"), ("GPIO2", "LED")],
        "vin": "VIN",
        "expander": False,
        "led_active_low": False,
        "ladder": "GPIO33",
    },
    "esp32c3": {
        "title": "ELRSTx - ESP32-C3 + PCF8574",
        "chip": "ESP32-C3",
        "left": [("GPIO0", "ROLL"), ("GPIO1", "THROTTLE"), ("GPIO2", "YAW"), ("GPIO3", "PITCH"),
                 ("GPIO4", "VBAT_SENSE")],
        "right": [("GPIO5", "SDA"), ("GPIO6", "SCL"), ("GPIO21", "CRSF_TX"), ("GPIO20", "CRSF_RX"),
                  ("GPIO10", "BUZZER"), ("GPIO8", "LED")],
        "vin": "5V",
        "expander": True,
        "led_active_low": True,
    },
}

# (line color, light fill) per function
GROUPS = {
    "5V":     ("#c62828", "#ffebee"),
    "3V3":    ("#ef6c00", "#fff3e0"),
    "GND":    ("#616161", "#f5f5f5"),
    "analog": ("#1565c0", "#e3f2fd"),
    "button": ("#d84315", "#fbe9e7"),
    "switch": ("#6a1b9a", "#f3e5f5"),
    "i2c":    ("#2e7d32", "#e8f5e9"),
    "crsf":   ("#00838f", "#e0f7fa"),
    "ind":    ("#ad1457", "#fce4ec"),
}
NET_GROUP = {
    "ROLL": "analog", "THROTTLE": "analog", "YAW": "analog", "PITCH": "analog", "VBAT_SENSE": "analog",
    "BTN_UP": "button", "BTN_DOWN": "button", "BTN_OK": "button", "BTN_BACK": "button", "BTN_ADC": "button",
    "AUX1_ARM": "switch", "AUX2": "switch", "AUX3": "switch", "AUX4": "switch",
    "SDA": "i2c", "SCL": "i2c", "CRSF_TX": "crsf", "CRSF_RX": "crsf", "BUZZER": "ind", "LED": "ind",
    "5V": "5V", "3V3": "3V3",
}
CHIP_STYLE = ("#5d4037", "#fff8e1")

NET_FONT = 9
TAG_W = 2.3
PIN_STEP = 0.9
LW = 1.6


def colors(net):
    return GROUPS[NET_GROUP[net]]


def tag(d, at, direction, name, length=0.6):
    """Short stub from `at` ending in a net label, colored by net."""
    c, fill = colors(name)
    d.add(getattr(elm.Line(color=c, lw=LW).at(at).length(length), direction)())
    d.add(getattr(elm.Tag(width=TAG_W, color=c, fill=fill), direction)()
          .label(name, fontsize=NET_FONT, color=c))


def rail(d, at, name):
    c, _ = GROUPS[name]
    d.add(elm.Vdd(color=c, lw=LW).at(at).right().label(name, fontsize=NET_FONT, color=c))


def ground(d, at):
    d.add(elm.Ground(color=GROUPS["GND"][0], lw=LW).at(at).right())


def section(d, xy, text, group):
    d.add(elm.Label().at(xy).label(text, fontsize=12, halign="left", color=GROUPS[group][0]))


def ic_block(d, at, anchor, label, pins, width, style, loc="center", ofst=0.0):
    """IC drawn left-to-right; pins listed top-to-bottom on each side."""
    line, fill = style
    rows = max(sum(1 for p in pins if p.side == s) for s in ("left", "right"))
    height = rows * PIN_STEP + 0.6
    by_side = {s: [p for p in pins if p.side == s] for s in ("left", "right", "top", "bottom")}
    ordered = list(reversed(by_side["left"])) + list(reversed(by_side["right"])) + by_side["top"] + by_side["bottom"]
    ic = elm.Ic(pins=ordered, size=(width, height), pinspacing=PIN_STEP, leadlen=0.6, plblsize=10,
                color=line, fill=fill, lw=LW)
    if by_side["top"]:
        ic.side("T", spacing=1.8, pad=0.9)
    return d.add(ic.right().label(label, loc=loc, fontsize=12, ofst=ofst, color=line).anchor(anchor).at(at))


def chip(d, board):
    pins = [elm.IcPin(name=g, side="left", anchorname=g) for g, _ in board["left"]]
    pins += [elm.IcPin(name=g, side="right", anchorname=g) for g, _ in board["right"]]
    pins += [elm.IcPin(name=board["vin"], side="top", anchorname="VIN"),
             elm.IcPin(name="3V3", side="top", anchorname="V33"),
             elm.IcPin(name="GND", side="bottom", anchorname="GND")]
    ic = ic_block(d, (0, 0), "center", board["chip"], pins, 4.5, CHIP_STYLE, loc="top", ofst=1.4)
    for g, net in board["left"]:
        tag(d, getattr(ic, g), "left", net)
    for g, net in board["right"]:
        tag(d, getattr(ic, g), "right", net)
    rail(d, ic.VIN, "5V")
    rail(d, ic.V33, "3V3")
    ground(d, ic.GND)


def power(d, x, y):
    red = GROUPS["5V"][0]
    blue = GROUPS["analog"][0]
    section(d, (x, y + 1.2), "Alimentação e medição da bateria", "5V")
    bat = d.add(elm.BatteryCell(color=red, lw=LW).at((x + 0.5, y - 3)).up()
                .label("Bateria\nLiPo 2S", loc="bottom", fontsize=10, color=red))
    ground(d, bat.start)
    d.add(elm.Line(color=red, lw=LW).at(bat.end).right().length(2.5))
    node = d.add(elm.Dot(color=red))
    d.add(elm.Line(color=red, lw=LW).right().length(1.5))
    reg = d.add(elm.VoltageRegulator(color=red, fill=GROUPS["5V"][1], lw=LW).right().anchor("in")
                .label("Regulador 5V", loc="top", fontsize=10, color=red))
    ground(d, reg.gnd)
    tag(d, reg.out, "right", "5V", length=0.8)
    r1 = d.add(elm.Resistor(color=blue, lw=LW).at(node.center).down().label("100k", loc="bottom", fontsize=10, color=blue))
    mid = d.add(elm.Dot(color=blue))
    tag(d, mid.center, "right", "VBAT_SENSE", length=0.8)
    r2 = d.add(elm.Resistor(color=blue, lw=LW).at(r1.end).down().label("39k", loc="bottom", fontsize=10, color=blue))
    ground(d, r2.end)


def joysticks(d, x, y):
    blue = GROUPS["analog"][0]
    section(d, (x, y + 1.2), "Joysticks (potenciômetros) - esquerdo: ROLL / THROTTLE, direito: YAW / PITCH", "analog")
    for i, net in enumerate(["ROLL", "THROTTLE", "YAW", "PITCH"]):
        pot = d.add(elm.Potentiometer(color=blue, lw=LW).at((x + 0.5 + i * 3.7, y - 3)).up().flip())
        ground(d, pot.start)
        rail(d, pot.end, "3V3")
        tag(d, pot.tap, "right", net, length=0.3)


def contacts(d, x, y, title, nets, kind):
    c = GROUPS[kind][0]
    section(d, (x, y + 1.2), title, kind)
    for i, net in enumerate(nets):
        part = elm.Button(color=c, lw=LW) if kind == "button" else elm.Switch(color=c, lw=LW)
        s = d.add(part.at((x + 2.9 + i * 3.6, y)).down())
        ground(d, s.end)
        tag(d, s.start, "left", net, length=0.01)


def module(d, x, y, name, pins, group):
    """pins: list of (pin, kind, value) with kind 'net', 'rail' or 'gnd'; drawn on the left side."""
    ic_pins = [elm.IcPin(name=p, side="left", anchorname=f"p{i}") for i, (p, _, _) in enumerate(pins)]
    ic = ic_block(d, (x, y), "p0", name, ic_pins, 2.8, GROUPS[group], loc="top", ofst=0.3)
    for i, (_, kind, value) in enumerate(pins):
        a = getattr(ic, f"p{i}")
        if kind == "net":
            tag(d, a, "left", value)
        elif kind == "rail":
            d.add(elm.Line(color=GROUPS[value][0], lw=LW).at(a).left().length(0.6))
            rail(d, d.here, value)
        else:
            d.add(elm.Line(color=GROUPS["GND"][0], lw=LW).at(a).left().length(0.6))
            ground(d, d.here)


def expander(d, x, y):
    section(d, (x - 4, y + 2), "Expansor I2C (botões e switches)", "i2c")
    pins = [elm.IcPin(name=f"P{i}", side="left", anchorname=f"P{i}") for i in range(8)]
    pins += [elm.IcPin(name="SDA", side="right", anchorname="SDA"), elm.IcPin(name="SCL", side="right", anchorname="SCL"),
             elm.IcPin(name="VCC", side="top", anchorname="VCC"),
             elm.IcPin(name="A0", side="bottom", anchorname="A0"), elm.IcPin(name="A1", side="bottom", anchorname="A1"),
             elm.IcPin(name="A2", side="bottom", anchorname="A2"), elm.IcPin(name="GND", side="bottom", anchorname="GND")]
    ic = ic_block(d, (x, y), "P0", "PCF8574 (0x20)", pins, 3.6, GROUPS["i2c"])
    for i, net in enumerate(["BTN_UP", "BTN_DOWN", "BTN_OK", "BTN_BACK", "AUX1_ARM", "AUX2", "AUX3", "AUX4"]):
        tag(d, getattr(ic, f"P{i}"), "left", net)
    tag(d, ic.SDA, "right", "SDA")
    tag(d, ic.SCL, "right", "SCL")
    rail(d, ic.VCC, "3V3")
    for a in ("A0", "A1", "A2", "GND"):
        ground(d, getattr(ic, a))


def led(d, x, y, active_low):
    c = GROUPS["ind"][0]
    section(d, (x - 3, y + 1.2), "LED de status (na placa)", "ind")
    top = d.add(elm.Line(color=c, lw=LW).at((x, y)).down().length(0.3))
    if active_low:
        rail(d, top.start, "3V3")
    else:
        tag(d, top.start, "left", "LED", length=0.01)
    d.add(elm.Resistor(color=c, lw=LW).at(top.end).down().label("1k", loc="bottom", fontsize=9, color=c))
    diode = d.add(elm.LED(color=c, lw=LW).down())
    if active_low:
        tag(d, diode.end, "left", "LED", length=0.01)
    else:
        ground(d, diode.end)


def draw(key, board):
    with schemdraw.Drawing(show=False) as d:
        d.config(fontsize=10, unit=2.5, lw=LW, bgcolor="white")
        d.add(elm.Label().at((-21, 13)).label(board["title"], fontsize=16, halign="left"))
        chip(d, board)
        power(d, -21, 10.5)
        joysticks(d, -21, 3)
        contacts(d, -21, -3, "Botões do menu (para GND)", ["BTN_UP", "BTN_DOWN", "BTN_OK", "BTN_BACK"], "button")
        contacts(d, -21, -8.5, "Switches AUX (para GND)", ["AUX1_ARM", "AUX2", "AUX3", "AUX4"], "switch")
        module(d, 11.5, 9.4, "Display OLED 0.96\nSSD1306 - I2C 0x3C",
               [("VCC", "rail", "3V3"), ("GND", "gnd", None), ("SCL", "net", "SCL"), ("SDA", "net", "SDA")], "i2c")
        module(d, 11.5, 3.4, "Módulo ELRS TX DIY (ESP32 + E28)\nUART 400000 - serial_rx 13 / serial_tx 16",
               [("VCC", "rail", "5V"), ("GND", "gnd", None), ("GPIO13 RX", "net", "CRSF_TX"),
                ("GPIO16 TX", "net", "CRSF_RX")], "crsf")
        module(d, 11.5, -2.6, "Buzzer ativo", [("+", "net", "BUZZER"), ("-", "gnd", None)], "ind")
        led(d, 19, -2.6, board["led_active_low"])
        if board["expander"]:
            expander(d, 12, -8)
        if board.get("ladder"):
            module(d, 11.5, -8.6, "Alternativa: botão 5 direções\n(escada de resistores, como no ELRS)\n\"buttons\": \"adc\" - no lugar dos 4 botões",
                   [("VCC", "rail", "3V3"), ("GND", "gnd", None), ("OUT", "net", "BTN_ADC")], "button")
            module(d, 11.5, -14.6, "Alternativa: encoder rotativo (EC11)\n\"buttons\": \"encoder\" - usa os pinos dos botões",
                   [("A", "net", "BTN_DOWN"), ("B", "net", "BTN_BACK"), ("SW", "net", "BTN_OK"), ("C / SW", "gnd", None)],
                   "button")
        d.save(str(OUT / f"schematic-{key}.svg"), transparent=False)
        d.save(str(OUT / f"schematic-{key}.png"), dpi=110, transparent=False)


def st7789():
    """Só a ligação do display ST7789 no DevKit."""
    nets = {"TFT_SCL": "i2c", "TFT_SDA": "i2c", "TFT_DC": "i2c", "TFT_RES": "i2c"}
    NET_GROUP.update(nets, GND="GND")
    with schemdraw.Drawing(show=False) as d:
        d.config(fontsize=10, unit=2.5, lw=LW, bgcolor="white")
        d.add(elm.Label().at((-3.5, 4.2)).label("ELRSTx - display ST7789 240x240 (SPI) no ESP32 DevKit", fontsize=14, halign="left"))
        pins = [elm.IcPin(name=g, side="right", anchorname=g) for g in ("GPIO22", "GPIO21", "GPIO15", "GPIO5")]
        pins += [elm.IcPin(name="3V3", side="top", anchorname="V33"), elm.IcPin(name="GND", side="bottom", anchorname="GND")]
        ic = ic_block(d, (0, 0), "center", "ESP32 DevKit", pins, 3.2, CHIP_STYLE, loc="top", ofst=1.0)
        for g, net in zip(("GPIO22", "GPIO21", "GPIO15", "GPIO5"), nets):
            tag(d, getattr(ic, g), "right", net)
        rail(d, ic.V33, "3V3")
        ground(d, ic.GND)
        module(d, 9.5, 1.95, "IPS ST7789 240x240\n(\"display\": \"st7789\")",
               [("GND", "net", "GND"), ("VCC", "net", "3V3"), ("SCL", "net", "TFT_SCL"), ("SDA", "net", "TFT_SDA"),
                ("RES", "net", "TFT_RES"), ("DC", "net", "TFT_DC"), ("BLK", "net", "3V3")], "i2c")
        d.add(elm.Label().at((-3.5, -4.6)).label(
            "Tudo em 3V3. Usa os pinos do I2C do OLED (os dois não funcionam juntos). Módulo com CS: CS no GND.\n"
            "O GPIO33 fica para o botão de 5 direções.",
            fontsize=10, halign="left", color="#444"))
        d.save(str(OUT / "st7789-esp32dev.svg"), transparent=False)
        d.save(str(OUT / "st7789-esp32dev.png"), dpi=110, transparent=False)


if __name__ == "__main__":
    for key, board in BOARDS.items():
        draw(key, board)
        print("ok", key)
    st7789()
    print("ok st7789")
