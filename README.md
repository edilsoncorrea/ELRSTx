# ELRSTx — Rádio controle ESP32 para módulo ExpressLRS

Rádio com dois joysticks analógicos, quatro switches e um OLED, que controla um módulo **ExpressLRS TX** via CRSF.

## Recursos

- 4 eixos analógicos (AETR) + 4 switches AUX (CH5–CH8, AUX1 = arm)
- CRSF a 400000 baud com **telemetria bidirecional**: link (RSSI/LQ/SNR/potência), bateria do modelo, atitude, GPS, modo de voo e nome do módulo
- **Sincronização com o módulo**: envia na mesma taxa de pacotes do ELRS (ex.: 250 Hz), ou 50 Hz sem sync
- Calibração por eixo, deadband, inversão e mapeamento de canais
- **5 perfis de modelo** salvos na flash (NVS, com checksum)
- **Segurança**: safe start (acelerador e arm bloqueados no boot até acelerador baixo + arm desligado), alarme de perda de link e de bateria baixa (LED + buzzer)
- **Bateria do rádio** por divisor resistivo, com porcentagem para LiPo
- **Display OLED 128x64 ou IPS ST7789 240x240 colorido** com menu: canais, telemetria, calibração, ajustes, perfis, WiFi/OTA e info
- **Configuração do módulo ExpressLRS** pelo rádio, como o script Lua do EdgeTX: taxa de pacotes, telemetria, potência, Bind, WiFi do módulo, Model Match e demais opções, lidas do próprio módulo
- **Pinagem, display e ligação do CRSF num `hardware.json`**, como no ExpressLRS: muda pelo serial ou pela web, sem recompilar
- **CRSF de um fio** (half-duplex invertido, como a baia JR de um rádio comum) ou de dois fios
- **WiFi AP** com página web de status, comandos e **atualização OTA**; também OTA pelo PlatformIO

> **Failsafe:** quando o link cai, o rádio não consegue mais falar com o modelo. O comportamento do aeromodelo na perda de sinal é configurado no receptor/controladora de voo (ex.: Betaflight → Failsafe). O rádio cuida do safe start e dos alarmes.

## Placas suportadas

| Placa | Ambiente | Botões/switches |
|-------|----------|-----------------|
| **ESP32 DevKit (ESP32-WROOM-32)** — padrão | `esp32dev` | direto nos GPIOs |
| ESP32-C3 | `esp32-c3-devkitm-1` | expansor I2C PCF8574 |

Pinagem completa das duas, comparação e a opção ESP32-S3: **[docs/HARDWARE.md](docs/HARDWARE.md)**.

![Esquemático ESP32 DevKit](docs/wiring/schematic-esp32dev.svg)

## Hardware (DevKit)

- ESP32 DevKit (ESP32-WROOM-32)
- Módulo ExpressLRS TX: UART de 2 pinos (padrão) ou **um fio**, como a baia JR
- 2 joysticks analógicos 3.3 V
- 4 botões de menu + 4 switches (entre o pino e o GND)
- OLED 0.96" SSD1306 128x64 I2C de 4 pinos (0x3C) — opcional
- Divisor 100k/39k para a bateria (até ~11.7 V) — opcional
- Buzzer ativo — opcional

| GPIO | Função |
|------|--------|
| 36 / 39 | Joystick esquerdo X (Roll) / Y (Throttle) |
| 34 / 35 | Joystick direito X (Yaw) / Y (Pitch) |
| 32 | Bateria |
| 21 / 22 | I2C SDA / SCL (OLED) |
| 16 / 17 | CRSF RX ← TX do módulo / CRSF TX → RX do módulo (com um fio: só o 17, `hw set crsf_rx 17`) |
| 2 | LED da placa |
| 25 | Buzzer |
| 4 / 13 / 14 / 27 | Botões cima / baixo / OK / voltar |
| 18 / 19 / 23 / 26 | AUX1 (arm) / AUX2 / AUX3 / AUX4 |

Essa é a pinagem padrão do [hardware/esp32dev.json](hardware/esp32dev.json); veja [Pinagem e hardware.json](#pinagem-e-hardwarejson).

O módulo detecta o baud automaticamente entre `400000, 115200, 921600, 1870000, ...`. **420000 não funciona** — essa é a velocidade entre receptor e controladora de voo.

## Compilar e gravar

```bash
pio run -t upload                         # ESP32 DevKit
pio run -e esp32-c3-devkitm-1 -t upload   # ESP32-C3
pio device monitor                        # 115200 baud
```

No primeiro boot, os testes unitários rodam e o resultado aparece no monitor. Desative `RUN_UNIT_TESTS` em [src/main.cpp](src/main.cpp) depois de conferir.

## Primeiros passos

1. Grave o firmware e abra o monitor. A linha de status mostra `module ok` quando o módulo responde.
2. Digite `status`. Se aparecer `NOT RESPONDING`, confira o fio do CRSF (ou os dois, cruzados), o GND, se o módulo está ligado e se `crsf_rx`/`crsf_tx` combinam com o `serial_rx`/`serial_tx` do módulo.
3. Calibre: `cal center` (sticks no centro), `cal start`, mova todos os sticks até o limite, `cal stop`. Ou use o menu **Calibrate**.
4. Ligue o receptor: `status` deve mostrar LQ e RSSI.
5. Abaixe o acelerador e desligue o AUX1 para liberar o safe start.

## Display

Tela inicial (botão OK abre o menu):

```
Model 1            7.6V   <- perfil e bateria do rádio
ELRS 2.4G TX              <- nome do módulo (ou "No ELRS module")
LQ 100%  -45dBm           <- link com o receptor
250Hz  15.8V              <- taxa de envio e bateria do modelo
┌──┐    ANGL     ┌──┐   <- modo de voo
│ •│  ■ □ □ □    │• │   <- sticks (quadrado com bolinha, como no EdgeTX) e AUX1..AUX4
└──┘             └──┘
```

Um alarme (ex.: `LINK LOST`) aparece em destaque no lugar do nome do módulo. Prévias das telas dos dois displays: [docs/screens/](docs/screens/).

Menu: **ExpressLRS** (parâmetros do módulo TX: OK entra na pasta ou edita, cima/baixo muda o valor, OK grava, voltar cancela; comandos como Bind abrem uma janela de confirmação), **Channels** (barras e µs de CH1–8), **Telemetry** (RSSI/LQ/SNR, bateria, atitude, GPS, modo de voo), **Calibrate** (assistente em 2 passos), **Settings** (deadband, inversões, safe start, alarme de link, células e limites da bateria, simulação; grava ao sair), **Profiles** (5 modelos), **WiFi / OTA** (liga/desliga e mostra rede, senha e endereço) e **Info** (versão, frames, telemetria ok/ruim, memória, tempo ligado).

## Comandos (Serial e web)

```
status                  módulo, link, bateria, taxa
config                  configurações em JSON
set <chave> <valor>     altera (na RAM); depois 'save'
save                    grava na flash
profile [0-4]           lista ou seleciona perfil
defaults                volta aos padrões (não grava)
cal [center|start|stop|cancel]
sim [on|off]            simulação dos sticks (t alterna)
frame                   último frame CRSF (f)
wifi on|off             WiFi: rede de casa (se salva) ou rede própria; web + OTA
wifi home <rede> <senha>  salva a rede de casa (http://elrstx.local)
wifi forget | status | scan
mon on|off              linha de status a cada segundo
elrs                    parâmetros do módulo ExpressLRS
elrs set <id> <valor>   altera um parâmetro (texto da opção ou índice)
elrs cmd <id> [confirm|cancel]  executa um comando (Bind, WiFi...)
elrs reload             relê os parâmetros
hw                      hardware.json ativo (pinos, display, CRSF)
hw set <chave> <valor>  altera uma chave (gravado, vale após reboot)
hw saved | default      hardware.json pendente / embutido
hw reset                apaga o hardware.json gravado
hw learn [direção]      botão de 5 direções: grava a leitura de up/down/left/right/enter/idle
reboot
```

Chaves: `name`, `deadband`, `invert.<eixo>`, `map.<eixo>` (canal 1–4), `safestart`, `linkalarm`, `linktimeout`, `batt.ratio` ((R1+R2)/R2 × 1000), `batt.cells`, `batt.warn`, `batt.crit` (mV por célula), `cal.<eixo>` (`min,centro,max`), `wifipass`. Eixos: `roll pitch throttle yaw`.

## Pinagem e hardware.json

Como no ExpressLRS, os pinos não ficam fixos no código. Cada placa tem um arquivo padrão, [hardware/esp32dev.json](hardware/esp32dev.json) ou [hardware/esp32c3.json](hardware/esp32c3.json), que vai embutido no firmware. Para mudar sem recompilar:

- **Serial:** `hw set display st7789`, `hw set crsf_rx 16`, ... e depois `reboot`.
- **Web:** `wifi on`, abra `http://192.168.4.1/hardware`, edite o JSON, **Save** e **Reboot**.

Tudo é validado antes de gravar: pinos repetidos, pinos da flash, analógicos fora do ADC1, pinos só de entrada usados como saída ou botão. As mudanças só valem depois de reiniciar. Se uma configuração errada deixar o rádio sem tela ou botões, **ligue segurando OK** (pino padrão) para usar o arquivo embutido, ou mande `hw reset` pelo serial.

Chaves principais:

| Chave | Exemplo | |
|---|---|---|
| `crsf_rx`, `crsf_tx` | `16`, `17` | iguais = **um fio** (half-duplex invertido); diferentes = dois fios (full-duplex) |
| `crsf_baud` | `400000` | |
| `display` | `ssd1306` | `none`, `ssd1306` ou `st7789` |
| `oled_addr` | `60` | 0x3C |
| `tft_sclk`, `tft_mosi`, `tft_dc`, `tft_rst`, `tft_rotation` | `22`, `21`, `15`, `5`, `0` | ST7789 |
| `inputs` | `gpio` | `gpio` ou `pcf8574` (+ `expander_addr`) |
| `btn_up` … `btn_back`, `aux1` … `aux4` | | botões e switches (modo `gpio`) |
| `buttons` | `inputs` | `inputs` (botões como em `inputs`) ou `adc` (botão de 5 direções num pino só) |
| `btn_adc`, `btn_adc_values` | `33`, `[185, 308, 436, 147, 2051, 4095]` | pino do ADC1 e leituras de cima, baixo, esquerda, direita, enter, solto (ordem do `joystick_values` do ELRS) |
| `joy_roll`, `joy_pitch`, `joy_throttle`, `joy_yaw`, `battery` | | pinos do ADC1 |
| `i2c_sda`, `i2c_scl`, `led`, `led_rgb`, `led_active_low`, `buzzer` | | |

Use `-1` para algo que não está ligado (ex.: `"battery": -1`).

## Botões do menu num fio só (botão de 5 direções)

Como no ExpressLRS, os botões do menu podem ficar num único pino analógico: o botão de 5 direções vem ligado a uma escada de resistores e cada direção gera uma tensão diferente. É o mesmo esquema do `joystick` / `joystick_values` do `hardware.json` do ExpressLRS, **na mesma ordem**: cima, baixo, esquerda, direita, enter, solto.

O padrão (`[185, 308, 436, 147, 2051, 4095]`) são os valores do `hardware.json` do módulo ELRS DIY (alvo "DIY 2400 TX ESP32 SX1280 E28"). Servem se o seu botão de 5 direções for montado igual ao desse módulo; caso contrário, ensine as leituras com `hw learn`.

| Direção | No menu |
|---|---|
| Cima / baixo | move o cursor, muda valores |
| Esquerda | voltar |
| Direita / enter (apertar) | OK |

1. Ligue o botão de 5 direções no **GPIO33** (ADC1), com a escada de resistores, e mande `hw set buttons adc` e `reboot`. Os GPIO 4, 13, 14 e 27 ficam livres.
2. Se as direções saírem trocadas: segure cada uma e mande `hw learn up`, `hw learn down`, `hw learn left`, `hw learn right`, `hw learn enter`. Solto: `hw learn idle`. Vale na hora, sem reboot. `hw learn` sozinho mostra a leitura atual e a tabela.
3. Para copiar de um módulo ELRS: `hw set btn_adc_values 185,308,436,147,2051,4095` (os números do `joystick_values` dele).

Uma direção por vez (o menu não precisa de mais). As chaves AUX continuam em pinos digitais. O pino tem de ser do ADC1: no DevKit só sobra o **GPIO33**, reservado para ele (o DC do ST7789 fica no GPIO15). No ESP32-C3 todos os pinos do ADC1 já estão ocupados (os botões vão no PCF8574).

## Configuração remota e OTA

- **Rede de casa:** salve uma vez com `wifi home <rede> <senha>`. Depois, `wifi on` (ou menu WiFi / OTA) conecta nela e a página abre em **http://elrstx.local** (ou pelo IP mostrado em `wifi` e na tela). Só redes de 2.4 GHz; `wifi scan` lista as que o rádio enxerga.
- **Rede própria:** sem rede de casa salva, ou se ela não responder em 20 s (ex.: no campo), o rádio cria a rede `ELRSTx-XXXX` (senha padrão `elrstx1234`) em `http://192.168.4.1`. Se a de casa falhar, `wifi` mostra o motivo (senha errada, rede não encontrada...).
- **Página web:** status, comandos, `hardware.json` (`/hardware`) e upload do `firmware.bin`.
- **OTA pelo PlatformIO:** `pio run -e ota -t upload` (vai para `elrstx.local`). Na rede própria: `pio run -e ota -t upload --upload-port 192.168.4.1`.

Deixe o WiFi desligado durante o voo: ele usa 2.4 GHz, a mesma faixa dos módulos ELRS de 2.4 GHz.

## LED de status

| Padrão | Significado |
|--------|-------------|
| Aceso fixo | Link com o receptor |
| Pisca lento (1 Hz) | Sem receptor |
| Duas piscadas curtas por segundo | Aviso (sem módulo, safe start, bateria baixa) |
| Pisca rápido (4 Hz) | Perda de link ou bateria crítica |
| Aceso com uma falha curta por segundo | WiFi ligado |

Se o LED aparecer invertido: `hw set led_active_low true`. Com `hw set led_rgb true`, um WS2812 mostra cores (verde = link, azul = sem receptor, amarelo = aviso, vermelho = alarme, roxo = WiFi).

## Estrutura

```
hardware/*.json       pinagem padrão de cada placa (embutida no firmware)
src/Hardware.cpp      lê, valida e grava o hardware.json
src/Display*.cpp      um driver por display (SSD1306, ST7789, nenhum)
src/main.cpp          task de RC (timer) + loop da interface
src/CRSF.cpp          protocolo: frames RC, ping, parser de telemetria
src/Config.cpp        configurações e perfis na NVS
src/Inputs.cpp        joysticks, simulação, bateria
src/Safety.cpp        safe start e calibração
src/State.cpp         estado compartilhado entre as tasks
src/UI.cpp            OLED, menu, botões/switches, LED, buzzer, alarmes
src/ElrsParams.cpp    parâmetros do ExpressLRS (protocolo do Lua)
src/Commands.cpp      comandos de texto
src/Remote.cpp        WiFi AP, web, OTA
src/CRSFTest.cpp      testes do protocolo
docs/HARDWARE.md      placas, pinagens e ligações
docs/SIMULATION.md    modo de simulação
```
