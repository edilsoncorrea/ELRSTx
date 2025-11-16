# Testes Unitários CRSF

## Como Executar os Testes

### Opção 1: Ativar testes no código

No arquivo `src/main.cpp`, descomente a linha:

```cpp
#define RUN_UNIT_TESTS
```

Compile e faça upload. Os testes serão executados no boot antes do loop principal.

### Opção 2: Via Serial Monitor

Durante a execução normal, envie os seguintes comandos via Serial Monitor:

- `t` - Executar todos os testes
- `p` - Testar geração de pacotes
- `m` - Testar mapeamento de canais
- `c` - Testar CRC
- `f` - Mostrar frame CRSF atual

## Testes Implementados

### 1. Test Packet Generation
Valida que:
- Canais podem ser setados corretamente
- Valores são lidos de volta corretamente
- Estrutura do frame está correta

### 2. Test Channel Mapping
Valida que:
- ADC 0 → CRSF 172 (mínimo)
- ADC 2048 → CRSF 992 (centro)
- ADC 4095 → CRSF 1811 (máximo)
- Valores fora do range são limitados

### 3. Test CRC
Valida que:
- CRC8 é calculado corretamente
- Frames tem CRC válido

## Validação de Frames CRSF

Cada frame enviado tem a seguinte estrutura (26 bytes):

```
Byte  | Descrição
------|------------------------------------------
[0]   | 0xC8 - Sync byte
[1]   | 0x18 (24) - Frame length
[2]   | 0x16 - Frame type (RC channels)
[3-24]| 22 bytes - 16 canais packed (11 bits cada)
[25]  | CRC8 - DVB-S2 polynomial
```

### Exemplo de Frame Válido (todos canais em 992)

```
C8 18 16 E0 03 F8 03 F8 03 F8 03 F8 03 F8 03
E0 03 F8 03 F8 03 F8 03 F8 03 F8 03 [CRC]
```

## Correção: Pinout Serial

⚠️ **IMPORTANTE**: O ExpressLRS TX module só precisa do pino RX!

**Conexão correta:**
```
ESP32 GPIO17 (TX2) → ExpressLRS RX
ESP32 GND          → ExpressLRS GND
```

**Não é necessário conectar:**
- RX do ESP32 (não usado)
- TX do ExpressLRS (não envia dados de volta)

O módulo ExpressLRS recebe os comandos via CRSF e transmite via rádio. Comunicação é unidirecional do controlador para o módulo.
