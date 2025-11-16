# ESP32-C3 ExpressLRS Radio Controller

Rádio controle simplificado baseado em ESP32-C3 DevKit com dois joysticks analógicos, comunicando via protocolo CRSF com transmissor ExpressLRS.

## 📋 Características

- ✅ Suporte para 2 joysticks analógicos (4 canais)
- ✅ Protocolo CRSF para comunicação com ExpressLRS
- ✅ Taxa de atualização de 50 Hz
- ✅ Calibração e deadband configuráveis
- ✅ Debug via Serial Monitor
- ✅ Indicador LED de status

## 🔧 Hardware Necessário

- **ESP32-C3 DevKit** (ou ESP32-C3-DevKitM-1)
- **2x Joysticks Analógicos** (módulos com saída 0-3.3V)
- **Módulo ExpressLRS TX** (com suporte a CRSF)
- Cabos jumper para conexões

## 📌 Pinout ESP32-C3

### Joysticks

| Joystick | Eixo | Pino GPIO | Canal RC | Função |
|----------|------|-----------|----------|--------|
| Left (1) | X | GPIO 0 | CH1 | Roll (Aileron) |
| Left (1) | Y | GPIO 1 | CH3 | Throttle |
| Right (2) | X | GPIO 2 | CH4 | Yaw (Rudder) |
| Right (2) | Y | GPIO 3 | CH2 | Pitch (Elevator) |

**Nota**: ESP32-C3 tem ADC1 em GPIO0-4 (ADC1_CH0 a ADC1_CH4)

### CRSF (ExpressLRS)

| Função | Pino GPIO | Descrição |
|--------|-----------|-----------|
| TX | GPIO 21 | Transmite para ELRS (conecta ao RX do módulo) |
| Baudrate | 420000 | Velocidade serial CRSF |

**Nota**: Apenas TX é necessário - comunicação unidirecional.

### LED de Status

| Função | Pino GPIO |
|--------|-----------|
| LED | GPIO 8 | LED interno do ESP32-C3 |

## 🔌 Esquema de Conexão

### Joysticks → ESP32-C3
```
Joystick 1 (Esquerdo):
  VCC → 3.3V
  GND → GND
  VRx → GPIO 0
  VRy → GPIO 1

Joystick 2 (Direito):
  VCC → 3.3V
  GND → GND
  VRx → GPIO 2
  VRy → GPIO 3
```

**⚠️ IMPORTANTE**: Use joysticks compatíveis com 3.3V! O ESP32-C3 não tolera 5V nos pinos ADC.

### ESP32-C3 → ExpressLRS TX
```
ESP32-C3       ELRS TX Module
GPIO 21 (TX)  → RX (pino de dados do módulo)
GND           → GND
5V ou 3.3V    → VCC (conforme especificação do módulo)
```

**Nota**: O ExpressLRS TX module só precisa receber dados via CRSF. Não é necessário conectar o pino RX do ESP32-C3 ao TX do módulo, pois a comunicação é unidirecional (controlador → módulo → rádio).

## 🚀 Como Usar

### 1. Instalação do PlatformIO

Se ainda não tem o PlatformIO instalado:
- Instale a extensão PlatformIO IDE no VS Code

### 2. Compilar e Upload

```bash
# No terminal do PlatformIO
pio run -t upload

# Ou use o botão "Upload" na barra inferior do VS Code
```

### 3. Monitor Serial

```bash
pio device monitor

# Ou use o botão "Serial Monitor" no VS Code
# Velocidade: 115200 baud
```

### 4. Configuração do ExpressLRS

No seu módulo ExpressLRS TX:
1. Configure para aceitar protocolo CRSF
2. Conecte fisicamente os pinos RX/TX conforme o esquema
3. Alimente o módulo (pode usar 5V do ESP32 ou fonte externa)

## ⚙️ Configurações

### Calibração dos Joysticks

No arquivo `src/main.cpp`, ajuste os valores conforme seus joysticks:

```cpp
// Valores de calibração
#define ADC_MIN         200     // Valor mínimo do ADC
#define ADC_MAX         3895    // Valor máximo do ADC
#define ADC_CENTER      2048    // Valor central
#define DEADBAND        50      // Zona morta central
```

### Taxa de Atualização

```cpp
#define UPDATE_RATE     50      // Hz (padrão: 50 Hz)
```

### Mapeamento de Canais

Você pode alterar o mapeamento no arquivo `src/main.cpp`:

```cpp
#define CH_ROLL         0   // Canal 1 - Aileron
#define CH_PITCH        1   // Canal 2 - Elevator
#define CH_THROTTLE     2   // Canal 3 - Throttle
#define CH_YAW          3   // Canal 4 - Rudder
```

## 🎮 Modo de Simulação

**Teste tudo ANTES de soldar os joysticks!**

O projeto inclui um modo de simulação que gera movimentos suaves e naturais, permitindo validar:
- ✅ Comunicação CRSF com ExpressLRS
- ✅ Estrutura e integridade dos frames
- ✅ Valores dos canais e conversões
- ✅ Taxa de atualização (50 Hz)

### Ativação Rápida

No arquivo `src/main.cpp`:

```cpp
#define SIMULATION_MODE      // Movimentos simulados
#define SHOW_CRSF_FRAMES     // Análise detalhada dos frames
```

**Comandos via Serial Monitor:**
- `t` - Liga/desliga simulação em tempo real
- `f` - Mostra análise completa do frame CRSF
- `r` - Reinicia timer de simulação
- `h` - Menu de ajuda

Veja documentação completa em `docs/SIMULATION.md`

## 🧪 Testes Unitários

Para validar a saída CRSF, testes unitários foram implementados. Para executar:

1. No arquivo `src/main.cpp`, descomente a linha:
   ```cpp
   #define RUN_UNIT_TESTS
   ```

2. Compile e faça upload

3. Os testes serão executados no boot e validarão:
   - Geração correta de pacotes CRSF
   - Mapeamento de valores analógicos
   - Cálculo de CRC8
   - Estrutura dos frames

Veja mais detalhes em `test/README_TESTS.md`

## 📊 Protocolo CRSF

O protocolo CRSF (Crossfire) é usado para comunicação de alta velocidade com o ExpressLRS:

- **Baudrate**: 420000 bps
- **Formato**: 8N1 (8 bits, sem paridade, 1 stop bit)
- **Canais**: 16 canais de 11 bits cada (valores 172-1811)
- **Valor central**: 992
- **Taxa de frames**: configurável (padrão 50 Hz)

## 🐛 Troubleshooting

### Joysticks não respondem
- Verifique as conexões VCC, GND e pinos analógicos
- Use o monitor serial para ver valores raw
- Ajuste valores de calibração se necessário

### ExpressLRS não recebe sinal
- Confirme baudrate (420000)
- Verifique conexão TX/RX (atenção: TX→RX e RX→TX)
- Verifique se o módulo ELRS está configurado para CRSF
- Teste com firmware ELRS atualizado

### Valores instáveis
- Aumente o DEADBAND
- Verifique alimentação estável (3.3V)
- Use capacitores de desacoplamento nos joysticks

## 📝 Estrutura do Projeto

```
elrxtx/
├── include/
│   └── CRSF.h              # Header da biblioteca CRSF
├── src/
│   ├── CRSF.cpp            # Implementação do protocolo CRSF
│   └── main.cpp            # Código principal
├── platformio.ini          # Configuração do PlatformIO
└── README.md              # Este arquivo
```

## 🔗 Referências

- [ExpressLRS Documentation](https://www.expresslrs.org/)
- [CRSF Protocol](https://github.com/ExpressLRS/ExpressLRS/wiki/CRSF-Protocol)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)

## 📄 Licença

Este projeto é de código aberto e livre para uso pessoal e educacional.

## 🤝 Contribuições

Contribuições são bem-vindas! Sinta-se à vontade para abrir issues ou pull requests.

---

**Nota**: Este é um projeto educacional/hobby. Para uso em competições ou aplicações críticas, considere usar hardware profissional certificado.
