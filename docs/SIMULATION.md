# Modo de Simulação e Análise de Frames CRSF

## 🎮 Modo de Simulação

O modo de simulação permite testar todo o sistema **sem joysticks físicos soldados**. Ele gera movimentos suaves e naturais simulando um piloto real.

### Ativação

No arquivo `src/main.cpp`, o modo está **ATIVO por padrão**:

```cpp
// Uncomment to enable SIMULATION MODE (without physical joysticks)
#define SIMULATION_MODE
```

Para desativar e usar joysticks reais, comente a linha:
```cpp
// #define SIMULATION_MODE
```

### Movimentos Simulados

Os 4 eixos são simulados com padrões diferentes:

| Eixo | Padrão | Descrição |
|------|--------|-----------|
| **Roll** | Senoidal (1.0x) | Movimento lado a lado suave |
| **Pitch** | Senoidal (0.7x) | Movimento frente/trás mais lento |
| **Throttle** | Rampa (0.3x) | Sobe e desce lentamente (30-70%) |
| **Yaw** | Senoidal (1.3x) | Rotação mais rápida |

### Parâmetros Ajustáveis

```cpp
#define SIM_SPEED       1.0f    // Velocidade da simulação (multiplicador)
#define SIM_AMPLITUDE   0.8f    // Amplitude dos movimentos (0.0 a 1.0)
```

## 📊 Análise de Frames CRSF

### Visualização Automática

Para ver **todos os frames** em detalhe, ative:

```cpp
// Uncomment to show CRSF frame data in hex
#define SHOW_CRSF_FRAMES
```

Isso exibirá a cada 500ms:
- Frame completo em hexadecimal
- Análise byte por byte
- Valores dos 8 primeiros canais
- Conversão para microsegundos (1000-2000μs)

### Exemplo de Saída

```
╔════════════════════════════════════════════════════╗
║           CRSF Frame Analysis                      ║
╠════════════════════════════════════════════════════╣
║ [00] Sync Byte:    0xC8 (✓)                       ║
║ [01] Frame Length: 24 bytes                        ║
║ [02] Frame Type:   0x16 (RC Channels)              ║
║                                                    ║
║ Payload (22 bytes - packed channels):             ║
║ E0 03 F8 03 F8 03 F8 03 F8 03 F8                  ║
║ E0 03 F8 03 F8 03 F8 03 F8 03 F8                  ║
║ [25] CRC8:         0x2D                            ║
╚════════════════════════════════════════════════════╝

Channel Values:
┌────┬─────────┬──────────┬─────────┐
│ CH │  Value  │   μs     │  Label  │
├────┼─────────┼──────────┼─────────┤
│  1 │   992   │  1500 μs │ Roll    │
│  2 │   992   │  1500 μs │ Pitch   │
│  3 │   992   │  1500 μs │ Throt   │
│  4 │   992   │  1500 μs │ Yaw     │
│  5 │   992   │  1500 μs │ AUX1    │
│  6 │   992   │  1500 μs │ AUX2    │
│  7 │   992   │  1500 μs │ AUX3    │
│  8 │   992   │  1500 μs │ AUX4    │
└────┴─────────┴──────────┴─────────┘
```

## ⌨️ Comandos Interativos via Serial

Durante a execução, você pode enviar comandos pelo Serial Monitor:

| Comando | Função |
|---------|--------|
| `t` | **Toggle** - Liga/desliga modo simulação em tempo real |
| `f` | **Frame** - Mostra análise detalhada do frame atual |
| `r` | **Reset** - Reinicia o timer de simulação |
| `h` | **Help** - Mostra menu de ajuda |

### Exemplo de Uso

1. Abra o Serial Monitor (115200 baud)
2. Digite `t` e pressione Enter para alternar entre simulação e hardware
3. Digite `f` para ver o frame CRSF completo a qualquer momento
4. Digite `h` para ver o menu de ajuda

## 🔍 Validação dos Testes Unitários

Os testes unitários implementados validam:

### 1. **Geração de Pacotes** ✅
- Verifica se canais são setados corretamente
- Valida leitura de valores
- Confirma consistência dos dados

### 2. **Mapeamento de Valores** ✅
```
ADC 0    → CRSF 172   (mínimo)
ADC 2048 → CRSF 992   (centro)
ADC 4095 → CRSF 1811  (máximo)
```

### 3. **Estrutura do Frame CRSF** ✅
- **Byte 0**: Sync (0xC8)
- **Byte 1**: Length (24)
- **Byte 2**: Type (0x16)
- **Bytes 3-24**: Payload (16 canais × 11 bits empacotados)
- **Byte 25**: CRC8 (DVB-S2)

### 4. **Integridade CRC8** ✅
- Algoritmo DVB-S2 polynomial
- Validação sobre type + payload (23 bytes)
- Detecta corrupção de dados

## 🧪 Workflow de Teste Recomendado

### Fase 1: Simulação Pura
```cpp
#define SIMULATION_MODE     // ✓ ATIVO
#define SHOW_CRSF_FRAMES    // ✓ ATIVO
// #define RUN_UNIT_TESTS   // ✗ Opcional
```

**Objetivo**: Ver os frames CRSF sendo gerados com movimentos suaves

### Fase 2: Validação com Hardware
```cpp
// #define SIMULATION_MODE  // ✗ DESATIVADO
#define SHOW_CRSF_FRAMES    // ✓ ATIVO
```

**Objetivo**: Comparar valores dos joysticks reais com frames CRSF

### Fase 3: Operação Normal
```cpp
// #define SIMULATION_MODE  // ✗ DESATIVADO
// #define SHOW_CRSF_FRAMES // ✗ DESATIVADO
```

**Objetivo**: Performance máxima, apenas logs essenciais

## 📈 Interpretação dos Valores

### Valores CRSF (11 bits)
- **Mínimo**: 172 (stick no limite esquerdo/baixo)
- **Centro**: 992 (stick centralizado)
- **Máximo**: 1811 (stick no limite direito/cima)

### Conversão para μs (padrão PWM)
```
CRSF 172  → 1000 μs (mínimo)
CRSF 992  → 1500 μs (centro)
CRSF 1811 → 2000 μs (máximo)
```

### Exemplo de Interpretação

```
[SIM] Time:5.2s | ADC: L(2048,1500) R(3000,2048) | CRSF: R:992 P:1200 T:700 Y:992 | Frames:260

Interpretação:
- Roll (R): 992 = centro → stick neutro
- Pitch (P): 1200 = +20% → stick empurrado para frente
- Throttle (T): 700 = -30% → stick baixo (motor devagar)
- Yaw (Y): 992 = centro → stick neutro
- Frames: 260 enviados em 5.2s (50 Hz ✓)
```

## 🐛 Troubleshooting

### Valores não mudam na simulação
- Verifique se `SIMULATION_MODE` está definido
- Envie comando `t` via serial para ativar

### Frames CRSF não aparecem
- Verifique se `SHOW_CRSF_FRAMES` está definido
- Ou envie comando `f` via serial

### CRC inválido
- Possível problema no cálculo ou empacotamento
- Execute testes unitários (`#define RUN_UNIT_TESTS`)

### Movimentos muito rápidos/lentos
- Ajuste `SIM_SPEED` (0.5 = metade da velocidade, 2.0 = dobro)
- Ajuste `SIM_AMPLITUDE` (0.5 = movimento mais sutil)

## 📝 Próximos Passos

1. ✅ Compile e faça upload
2. ✅ Abra Serial Monitor (115200 baud)
3. ✅ Observe os movimentos simulados
4. ✅ Digite `f` para ver frame completo
5. ✅ Solde os joysticks
6. ✅ Digite `t` para testar hardware real
7. ✅ Compare valores simulados vs reais
8. ✅ Desative simulação para uso final

---

**Vantagens do Modo Simulação:**
- ✅ Testa comunicação CRSF antes de soldar
- ✅ Valida protocolo e frames
- ✅ Debug sem hardware físico
- ✅ Padrões de movimento realistas
- ✅ Facilita desenvolvimento
