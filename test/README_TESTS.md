# Testes do protocolo CRSF

Os testes rodam no próprio ESP32 no boot quando `RUN_UNIT_TESTS` está definido em `src/main.cpp`. O resultado aparece no monitor serial (115200 baud).

| Grupo | O que valida |
|-------|--------------|
| Packet generation | canais guardam o valor setado |
| Channel mapping | ADC 0/2048/4095 → 172/992/1811 e limites |
| CRC8 DVB-S2 | valor de verificação de `"123456789"` = `0xBC` |
| RC frame round trip | cabeçalho `EE 18 16`, CRC do frame e os 16 canais desempacotados iguais aos setados |
| Telemetry parsing | link statistics após lixo na linha, sync de timing (4000 µs) e frame corrompido rejeitado |

## Frame RC (26 bytes)

```
[0]     0xEE  endereço do módulo TX
[1]     0x18  tamanho (tipo + 22 + CRC)
[2]     0x16  RC channels
[3-24]  16 canais x 11 bits, little-endian
[25]    CRC8 DVB-S2 sobre os bytes 2..24
```

O comando `frame` (ou `f`) mostra o último frame enviado, confere o CRC e decodifica CH1–CH8.
