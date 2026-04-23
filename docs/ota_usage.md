# OTA (Over-The-Air) Firmware Updates

Gravação de firmware via WiFi sem precisar de cabo USB.

## 1. Activar OTA no dispositivo

1. Na FitaDigital, vai a **Definições** (digita o PIN).
2. Abre a aba **Sistema**.
3. Carrega no botão **Activar OTA**.
4. UI mostra estado `LISTENING` e IP do ESP32 na rede WiFi.
5. Dispositivo à escuta na porta 3232 (padrão ArduinoOTA).

> Se WiFi não ligado, botão devolve erro — tens de estar na mesma rede local que o PC.

## 2. Upload do novo firmware (PC)

### Via PlatformIO (recomendado)

```bash
pio run -e esp32-s3-touch-lcd-4_3b -t upload --upload-protocol espota --upload-port <IP_DO_ESP32>
```

Substitui `<IP_DO_ESP32>` pelo IP mostrado na UI (ex: `192.168.1.42`).

### Via espota.py directo

```bash
python %USERPROFILE%\.platformio\packages\framework-arduinoespressif32\tools\espota.py ^
  -i 192.168.1.42 -p 3232 -f .pio\build\esp32-s3-touch-lcd-4_3b\firmware.bin
```

## 3. Durante o upload

- UI mostra barra de progresso 0–100%.
- Estado: `LISTENING` → `RECEIVING` → `DONE` → **reboot automático**.
- Sucesso: novo firmware entra em boot.
- Erro: estado `ERROR` na UI, dispositivo mantém firmware antigo (sem brick).

## 4. Notas de segurança e rede

| Ponto | Detalhe |
|-------|---------|
| **Autenticação** | Nenhuma — qualquer um na LAN consegue fazer upload. Usa só em rede controlada. |
| **Mesma rede** | PC e ESP32 precisam estar no mesmo segmento L2 (sem NAT entre eles). |
| **Firewall** | Porta 3232 UDP/TCP. Firewall Windows pode pedir autorização. |
| **Protecção UI** | Botão OTA só aparece após PIN de Definições validado. |

## 5. Limitações e specs

- **Tamanho**: ~2.3 MB de firmware (36% flash) — bem dentro de uma partição OTA.
- **Sem assinatura**: não há verificação de integridade/autenticidade. Assume rede de confiança.
- **Porta padrão**: 3232 (customizável em `ota_manager.h` se necessário).
- **Reboot automático**: sucesso → reboot imediato (não pede confirmação).

## 6. Fluxo típico de desenvolvimento

1. **Primeira gravação**: via USB/COM3 (meter firmware com suporte OTA).
2. **Gravações seguintes**: activar OTA na UI → `pio run -t upload --upload-protocol espota --upload-port <IP>`.
3. **Vantagem**: útil quando dispositivo está montado no autoclave sem acesso ao cabo USB.

## 7. Troubleshooting

| Problema | Causa | Solução |
|----------|-------|---------|
| "Could not resolve host" | IP errado ou dispositivo offline | Verifica IP na UI; ping `<IP>` do PC |
| "Connection refused" | OTA não activado ou porta bloqueada | Activar OTA na UI; liberta firewall porta 3232 |
| "Upload aborted" | Comunicação WiFi interrompida | Retry; verifica sinal WiFi do dispositivo |
| Reboot não acontece | Raro; firmware não completo | Volta a fazer upload via USB/COM3 |

---

**Implementação**: `src/ota_manager.h/cpp` (ArduinoOTA push), UI em `src/ui/ui_app.cpp` (aba Sistema). Versão firmware: ≥ 1.11.
