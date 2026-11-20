# Modo de simulação

Gera movimentos suaves nos 4 eixos sem joysticks ligados, para testar o link CRSF com o módulo ExpressLRS antes de soldar.

## Ativar

É uma configuração do perfil, ligada em tempo de execução:

- Serial/web: `sim on`, `sim off` ou `t` (alterna)
- Menu: **Settings → Simulation**
- Para manter após reiniciar: `save`

Com a simulação ligada, a tela inicial mostra `SIM` e a linha de status começa com `[SIM]`.

## Padrões

| Eixo | Movimento |
|------|-----------|
| Roll | senoide, 1,0x |
| Pitch | senoide, 0,7x |
| Throttle | rampa lenta entre 30% e 70% |
| Yaw | senoide, 1,3x |

Os valores simulados passam pela mesma calibração, deadband e inversão que os joysticks reais. Os switches AUX continuam vindo do expander.

## Verificar

- `frame` (ou `f`): mostra o frame CRSF em hex, confere o CRC e decodifica CH1–CH8 em µs
- `status`: mostra se o módulo responde e a taxa de envio
- Com receptor ligado a uma controladora de voo, a aba Receiver do Betaflight deve mostrar os canais se movendo
