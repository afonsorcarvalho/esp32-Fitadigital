# Viabilidade — Tailscale no FitaDigital via microlink

**Data:** 2026-06-25
**Branch:** `feature/tailscale-microlink`
**Repo avaliado:** https://github.com/CamM2325/microlink (MicroLink v2)
**Objectivo:** avaliar se vale a pena adoptar o microlink para dar acesso remoto
(VPN Tailscale) ao FitaDigital. **Sem compilar nada** — só análise.

---

## TL;DR — Veredicto

| Cenário | Veredicto |
|---|---|
| **Hardware da placa é compatível?** | ✅ **SIM** (PSRAM octal, 16MB flash, S3, IDF 5.1) |
| **POC standalone do microlink na placa** | ✅ **Viável** — recomendado como 1º passo de de-risk |
| **Integrar no firmware FitaDigital ACTUAL (Arduino/PlatformIO)** | ❌ **Não viável as-is** — 2 bloqueios duros |
| **Licença** | ✅ MIT (uso comercial OK, só atribuição) |

**Conclusão:** o microlink/Tailscale é arquitecturalmente **mais robusto** que o
WireGuard custom abandonado (control plane gerido + relay DERP + NAT traversal),
e a **placa suporta-o**. MAS **não se cola** ao firmware actual: a SRAM interna
não chega e o build é ESP-IDF puro (não Arduino). Caminho realista = POC isolado
primeiro, depois decidir arquitectura (co-processador vs migração ESP-IDF), **não**
integração in-place.

---

## O que é o microlink

- Cliente **Tailscale nativo** para ESP32, em **C**, sobre **ESP-IDF v5.0+** (`idf.py`).
- Implementação directa do protocolo: **ts2021** (control plane), **WireGuard**
  (ChaCha20-Poly1305) nos túneis, **DERP** (relay), **DISCO/STUN** (NAT traversal).
  Userspace via lwIP — não é bridge/proxy.
- Deps: ESP-IDF, lwIP, MbedTLS, **`wireguard-lwip`**.
- Licença **MIT** (Cameron Malone, 2025-2026). Implementação independente,
  protocolo Tailscale **engenharia-reversa**, não afiliada à Tailscale Inc.
- Maturidade: "v2", 1 autor principal + 2 contribuidores, testado em tailnets
  300+ peers, várias placas S3. Arquitectura async task-based.

### Pegada de memória (declarada, ESP32-S3 / IDF 5.3)
| Recurso | Valor |
|---|---|
| **SRAM estática** (WiFi + Web UI) | **116 KB** |
| Stacks de tasks | 42 KB |
| PSRAM pico | ~1 MB (buffers H2 + JSON, 512KB cada, configuráveis até 64KB) |
| Flash | 950 KB |

---

## Cruzamento com o FitaDigital

### ✅ Compatível
- **PSRAM octal:** microlink exige `CONFIG_SPIRAM_MODE_OCT=y`. A placa tem
  `board_build.psram_type = opi` + `memory_type = qio_opi` → **OPI = octal**. OK.
- **Flash:** 16MB na placa vs 950KB necessários. Folga enorme.
- **IDF:** Arduino-ESP32 **3.0.3** assenta em **ESP-IDF 5.1** ≥ 5.0 exigido. OK.
- **Licença MIT:** uso comercial permitido (produto hospitalar OK), só atribuição.
- **Control plane self-host:** suporta **Headscale/Ionscale** — alinha com o
  objectivo antigo de túnel servidor↔ESP (o WG usava server self-host em 10.0.0.1).

### ❌ Bloqueio 1 — SRAM interna (o killer da integração in-place)
- microlink precisa de **~116 KB SRAM estática + 42 KB stacks**.
- FitaDigital corre com **~36 KB de heap interno livre** (medido:
  `int_free` 36–39 KB), `HEAP_GUARD` a 5 KB, e **97 `heap_guard_reboots`** já
  acumulados (reboots por OOM são crónicos sob carga).
- LVGL + framebuffers RGB + portal web (AsyncWebServer/TCP) + RS485 + SD + FTP já
  ocupam a SRAM interna. **Não há 116 KB para dar.** Integrar in-place implicaria
  cirurgia de memória que não existe margem para fazer.

### ❌ Bloqueio 2 — Build system (Arduino vs ESP-IDF)
- FitaDigital: **PlatformIO + framework Arduino**. microlink: **ESP-IDF puro**
  (`idf.py`, components, `sdkconfig` específico: mbedTLS TLS1.2,
  `SPIRAM_MODE_OCT`, partição `SINGLE_APP_LARGE`, main task stack 8192).
- Arduino-ESP32 3.0.3 É IDF 5.1 por baixo, mas o PlatformIO/Arduino **não expõe**
  a edição de `sdkconfig` como o `idf.py`. Meter um component ESP-IDF completo
  (com o seu CMake) dentro do build Arduino é **alto esforço e frágil**.

### ⚠️ Riscos a registar
- **`wireguard-lwip`:** é a **mesma família de lib** que mordeu o projeto antes
  (rekey storm pós-180s, "kernel rejeita TODA INIT pós-1º-handshake", bug upstream
  smartalock/wireguard-lwip — ver sessões 2026-05-13/14). O microlink conduz a lib
  de forma diferente (camada Tailscale com DERP/DISCO pode mascarar o handshake
  directo), mas o risco da lib base permanece.
- **Contenção PSRAM ↔ LCD RGB:** o painel RGB consome muita largura de banda PSRAM
  (tearing foi problema combatido, PCLK 16→12 MHz). +1 MB de buffers/crypto Tailscale
  no mesmo bus pode reintroduzir tearing/perda de perf.
- **Maturidade/protocolo:** 1 autor, protocolo ts2021 por engenharia-reversa, não
  afiliado à Tailscale. Se a Tailscale mudar o control plane, pode partir. Self-host
  Headscale mitiga (controlamos a versão do server).

---

## Comparação com o estado actual

- O acesso remoto está **desligado** na v2.x (`FITA_ENABLE_WG=0`) — o WireGuard
  custom nunca ficou suficientemente estável (apesar do "timer cego 90s" v1.76 ter
  estabilizado o túnel, foi removido do scope v2.x por fragilidade da lib).
- A motivação para o Tailscale é **acesso remoto fiável** que o WG custom não deu.
- Tailscale (control plane gerido + DERP relay + NAT traversal) é **genuinamente
  mais robusto** que o enrollment WG custom. O mérito existe — o problema é o **custo
  de integração**, não a tecnologia.

---

## Recomendação

**Se acesso remoto é o objectivo, o microlink/Tailscale tem mérito, mas NÃO pode ser
acoplado ao firmware Arduino actual.** Caminhos, por ordem de pragmatismo:

1. **POC standalone (de-risk, baixo custo)** — compilar `examples/basic_connect`
   do microlink (ESP-IDF) numa placa S3 dedicada, com Headscale self-host, confirmar
   que entra no tailnet e dá ping. Valida a tecnologia na prática **sem tocar no
   FitaDigital**. Próximo passo natural se quiseres avançar.

2. **Co-processador / gateway separado** — Tailscale numa segunda placa (ou box) que
   faz bridge para o FitaDigital pela rede local. Mantém o firmware actual intocado.
   Mais pragmático se o objectivo é só "chegar ao device de fora".

3. **Migração ESP-IDF (longo prazo)** — passar o FitaDigital de Arduino → ESP-IDF
   tornaria o microlink um component nativo, mas implica reescrever a plumbing de
   LVGL/display/portal/RS485. Esforço grande; só justifica se houver outras razões
   para sair do Arduino.

**NÃO recomendado:** tentar integração in-place no firmware Arduino actual — a SRAM
interna sozinha inviabiliza, antes mesmo do esforço de build.

---

## Próximos passos possíveis (decisão do utilizador)
- [ ] Avançar para POC standalone (placa S3 dedicada + Headscale) — produz dados reais
- [ ] Avaliar opção co-processador (qual hardware/bridge)
- [ ] Arquivar e manter WG custom como única opção de acesso remoto
- [ ] Nada por agora — relatório fica como referência
