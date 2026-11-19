# Opções de placa e pinagem

O firmware compila para duas placas. A pinagem de cada uma fica em [include/Pins.h](../include/Pins.h) e é escolhida automaticamente pelo ambiente do PlatformIO.

| | ESP32 DevKit (padrão) | ESP32-C3 (alternativa) | ESP32-S3 (não implementado) |
|---|---|---|---|
| Ambiente | `esp32dev` | `esp32-c3-devkitm-1` | — |
| GPIOs livres | ~25 | ~15 | ~35 |
| Botões e switches | direto nos GPIOs | PCF8574 por I2C | direto nos GPIOs |
| ADC | menos linear (a calibração compensa) | bom | bom |
| USB | chip USB-serial | USB nativo | USB nativo |
| Bluetooth | BLE (Bluedroid, firmware maior) | BLE (NimBLE) | BLE (NimBLE) |
| Flash usada | ~1,8 MB de 1,9 MB | ~1,4 MB de 1,9 MB | — |

Gravar:

```bash
pio run -e esp32dev -t upload              # DevKit (padrão, basta 'pio run -t upload')
pio run -e esp32-c3-devkitm-1 -t upload    # C3
```

## Esquemático

### ESP32 DevKit (padrão)

![Esquemático ESP32 DevKit](wiring/schematic-esp32dev.svg)

### ESP32-C3 + PCF8574

![Esquemático ESP32-C3](wiring/schematic-esp32c3.svg)

As ligações são feitas por **rótulos de rede**: pinos com o mesmo nome (ex.: `SDA`, `CRSF_TX`) estão ligados entre si. `3V3` e `5V` são as linhas de alimentação. O LED de status já vem na placa.

## Diagrama de blocos

### ESP32 DevKit (padrão)

![Diagrama de blocos ESP32 DevKit](wiring/blocks-esp32dev.svg)

### ESP32-C3 + PCF8574

![Diagrama de blocos ESP32-C3](wiring/blocks-esp32c3.svg)

Cores: vermelho = alimentação, azul = entradas, roxo = interface com o usuário, verde = link CRSF com o módulo, cinza tracejado = rádio até o modelo. O fio a fio de cada placa está nos diagramas de ligações mais abaixo.

## ESP32 DevKit (ESP32-WROOM-32) — padrão

| GPIO | Função |
|------|--------|
| 36 (VP) | Joystick esquerdo X → Roll (CH1) |
| 39 (VN) | Joystick esquerdo Y → Throttle (CH3) |
| 34 | Joystick direito X → Yaw (CH4) |
| 35 | Joystick direito Y → Pitch (CH2) |
| 32 | Bateria (meio do divisor) |
| 21 / 22 | I2C SDA / SCL (OLED) |
| 16 | CRSF RX ← TX do módulo |
| 17 | CRSF TX → RX do módulo |
| 2 | LED azul da placa |
| 25 | Buzzer |
| 4 / 13 / 14 / 27 | Botões cima / baixo / OK / voltar |
| 18 / 19 / 23 / 26 | Switches AUX1 (arm) / AUX2 / AUX3 / AUX4 |
| 33 | livre |

Regras seguidas nessa pinagem:
- **Analógicos só no ADC1 (GPIO 32–39).** O ADC2 para de funcionar com o WiFi ligado.
- **GPIO 34–39 são só entrada e não têm pull-up**: servem para analógicos, não para botões.
- **Pinos de boot (0, 2, 5, 12, 15) fora dos botões.** O GPIO12 puxado para cima, por exemplo, impede o boot.
- Botões e switches vão **entre o pino e o GND**, usando o pull-up interno.

### Diagrama de ligações (DevKit)

![Ligações ESP32 DevKit](wiring/esp32dev.svg)

Fonte: [wiring/esp32dev.yml](wiring/esp32dev.yml). Inclui alimentação, divisor da bateria e todos os GND.

## ESP32-C3 com PCF8574 — alternativa

O C3 não tem pinos suficientes para 8 botões/switches, então eles entram por um expansor I2C **PCF8574** (endereço 0x20; o PCF8574A usa 0x38), no mesmo barramento do OLED.

| GPIO | Função |
|------|--------|
| 0 / 1 / 2 / 3 | Roll / Throttle / Yaw / Pitch |
| 4 | Bateria |
| 5 / 6 | I2C SDA / SCL (OLED + PCF8574) |
| 20 / 21 | CRSF RX / TX |
| 8 | LED (comum, aceso em LOW, como na SuperMini; WS2812 com `LED_IS_RGB 1`) |
| 10 | Buzzer |

| PCF8574 | Função |
|---------|--------|
| P0 / P1 / P2 / P3 | Cima / baixo / OK / voltar |
| P4 / P5 / P6 / P7 | AUX1 (arm) / AUX2 / AUX3 / AUX4 |

Use o módulo PCF8574 avulso. O que vem soldado atrás de LCD 16x2 já tem os pinos ligados ao LCD.

### Diagrama de ligações (C3)

![Ligações ESP32-C3 + PCF8574](wiring/esp32c3.svg)

Fonte: [wiring/esp32c3.yml](wiring/esp32c3.yml). OLED e PCF8574 dividem os mesmos fios SDA/SCL.

## ESP32-S3 — opção futura

Tem mais pinos, ADC melhor, USB nativo e BLE pequeno (NimBLE). Ainda não há pinagem nem ambiente para ele; precisaria de um bloco novo em `Pins.h` e um ambiente no `platformio.ini`.

## Alimentação e bateria

Os diagramas acima já mostram a alimentação. O regulador 5V é um exemplo: use o que fornecer a tensão que o seu módulo ELRS pede. O divisor é 100k (BAT+ → meio) e 39k (meio → GND), com o meio no GPIO32 (DevKit) ou GPIO4 (C3).


**Todos os GND ficam ligados juntos**: bateria, regulador, ESP32, módulo ELRS, joysticks, OLED, buzzer e o lado comum dos botões e switches. Joysticks, OLED e PCF8574 vão no **3V3**, nunca no 5V.

## Comum a todas as placas

**Display:** OLED 0.96" SSD1306 128x64 **I2C de 4 pinos** (GND, VCC 3.3V, SCL, SDA), endereço 0x3C (alguns usam 0x3D). Módulos SPI (6–7 pinos) e os de 1.3" com SH1106 não são suportados.

**Módulo ExpressLRS (UART de 2 pinos):** TX do ESP → RX do módulo, RX do ESP ← TX do módulo, GND comum. Baud **400000** (o módulo não aceita 420000). Para módulos de um pino só (S.Port): `CRSF_INVERTED true` e um resistor de ~1 kΩ em série.

**Bateria:** divisor 100k (do + da bateria) / 39k (para GND), com o meio no pino de bateria. Mede até ~11,7 V. Para outro divisor: `set batt.ratio <(R1+R2)/R2 × 1000>`.

**LED:** com `LED_IS_RGB 1`, um WS2812/WS2812B no pino do LED mostra cores. Um único LED funciona sem mudar o código; vários LEDs precisariam de suporte a buffer de pixels.

**Joysticks:** use módulos alimentados com 3.3V. 5V nos pinos analógicos danifica o ESP32.

## Editar os diagramas

Esquemáticos: script Python com [schemdraw](https://schemdraw.readthedocs.io), um bloco por placa em `BOARDS`.

```bash
pip install schemdraw matplotlib
python docs/wiring/schematic.py
```

Diagramas de blocos: arquivos Graphviz `docs/wiring/blocks-*.dot`.

```bash
dot -Tsvg docs/wiring/blocks-esp32dev.dot -o docs/wiring/blocks-esp32dev.svg
dot -Tsvg docs/wiring/blocks-esp32c3.dot  -o docs/wiring/blocks-esp32c3.svg
```

Diagramas de ligações (fio a fio): são gerados pelo [WireViz](https://github.com/wireviz/WireViz) a partir dos arquivos YAML em `docs/wiring/`. Depois de alterar um YAML:

```bash
pip install wireviz          # precisa do Graphviz instalado (winget install Graphviz.Graphviz)
wireviz -f ps docs/wiring/esp32dev.yml
wireviz -f ps docs/wiring/esp32c3.yml
```

`-f ps` gera só o PNG e o SVG. Mantenha os YAML em sincronia com [include/Pins.h](../include/Pins.h).
