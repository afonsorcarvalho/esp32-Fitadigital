# FitaDigital — Manual de Instalação

**Versão firmware:** v2.0.0
**Público-alvo:** técnico instalador (eletricista / TI / engenheiro clínico)

---

## 1. Requisitos do local

### 1.1 Equipamento alvo
- Saída RS485 (porta serial diferencial) — autoclaves, esterilizadores, biorreatores, sealers, etc.
- Baud rate típico **9600 8N1** (verificar manual do fabricante do equipamento).
- Conector A/B identificado (alguns fabricantes usam D+/D− ou TX+/TX−).

### 1.2 Infraestrutura local
- **Tomada elétrica 110/220 V** próxima (alimentação por adaptador USB-C 5 V / 1 A).
- **Cobertura Wi-Fi 2.4 GHz** (FitaDigital **não suporta 5 GHz**). RSSI mínimo: -75 dBm.
- Acesso à rede LAN para PC/servidor que vai consultar arquivos.
- (Opcional) Servidor NTP local — se ausente, usa internet (`pool.ntp.org`).

---

## 2. Conteúdo da caixa

| Item | Quantidade |
|------|------------|
| Aparelho FitaDigital (ESP32-S3-Touch-LCD-4.3B em case) | 1 |
| Cabo USB-C → USB-A (alimentação, 1,5 m) | 1 |
| Adaptador USB 5 V / 1 A | 1 |
| Cartão microSD 16 GB pré-formatado FAT32 | 1 (já instalado) |
| Conector borne 3 vias para RS485 (A/B/GND) | 1 |

**NÃO incluído:** cabo RS485 — usar cabo blindado par-trançado (Belden 9841 ou equivalente, 24 AWG).

---

## 3. Montagem física

### 3.1 Posicionar
- Superfície estável, próxima do equipamento (≤ 5 m do RS485 idealmente).
- Cabo RS485 pode ir até **1200 m** com terminação 120 Ω nas pontas, mas para instalações típicas hospitalares ≤ 30 m basta sem terminação.
- Tela voltada para o operador.

### 3.2 Cabo RS485

**Pinout do conector borne do FitaDigital:**

```
+---+---+---+
| A | B | G |
+---+---+---+
  |   |   |
  |   |   +-- GND (terra de sinal, opcional mas recomendado)
  |   +------ B (D-)
  +---------- A (D+)
```

**Conexão ao equipamento:**
- A do FitaDigital ↔ A (ou D+ ou TX+) do equipamento
- B do FitaDigital ↔ B (ou D− ou TX−) do equipamento
- G do FitaDigital ↔ GND do equipamento (se disponível)

**Se ciclos não chegarem após instalação, INVERTER A↔B** (alguns fabricantes nomeiam invertido).

### 3.3 Cartão SD
- Já vem inserido. Slot lateral. Não retirar com aparelho ligado.
- Se substituir: formatar **FAT32** (não exFAT). Capacidade 16-32 GB recomendado. Marcas confiáveis (SanDisk, Samsung).

### 3.4 Alimentação
- Ligar adaptador USB na tomada, cabo USB-C ao aparelho.
- **Não há botão liga/desliga.** Energia = boot automático.

---

## 4. Primeiro boot — configuração Wi-Fi

Ao energizar pela primeira vez:

1. Boot screen mostra etapas. **SD** deve ficar verde (cartão OK).
2. **Wi-Fi** ficará amarelo/vermelho (sem SSID configurado) — esperado.
3. Tela principal aparece com mensagem **"Configure Wi-Fi"**.
4. Toque **Configurações → Rede → Wi-Fi**.
5. Campo **SSID:** digitar nome da rede.
6. Campo **Senha:** digitar senha da rede.
7. Toque **Salvar e Conectar**.
8. Aguardar ~10 s. Barra superior fica verde quando associa.

### 4.1 IP atribuído

DHCP por padrão. IP visível na barra superior + em Configurações → Rede.

**Anotar este IP** — será usado para portal web e FTP.

### 4.2 IP fixo (opcional, recomendado para produção)

FitaDigital **v2.0.0 não suporta IP fixo no menu** (DHCP only).
**Solução:** reservar IP no router por MAC address. MAC visível em Configurações → Rede → MAC.

---

## 5. Configuração NTP (data/hora)

1. Configurações → NTP.
2. **Ativar NTP**: ON.
3. **Servidor NTP**: `pool.ntp.org` (default) ou IP do servidor NTP local (ex.: `10.0.0.1`).
4. **Fuso horário (UTC±h)**: ex. `-3` para Brasília, `0` para Lisboa inverno.
5. Salvar.

> ⚠️ **Conhecido (v2.0.0):** sync NTP no boot pode passar com timestamp do RTC interno mesmo sem ter recebido pacote NTP real. Após ~1 min com Wi-Fi up confirme em Configurações → Sobre que hora bate com a real. Se diferir, reiniciar.

---

## 6. Validar instalação

### 6.1 Captura RS485
1. Acionar 1 ciclo no equipamento de origem (autoclave, etc.).
2. No FitaDigital tela principal: arquivo `AAAAMMDD.txt` deve aparecer/atualizar.
3. Abrir arquivo: conteúdo do ciclo visível.

**Se não chegar nada:** ver Seção 8.

### 6.2 Portal web
1. PC mesma rede: abrir `http://<IP-do-aparelho>/`.
2. Login: `admin` + PIN (default `1234`).
3. Tab **Ficheiros** → navegar `/CICLOS/AAAA/MM/` → confirmar arquivo presente.
4. Download para PC.

### 6.3 FTP
1. Cliente FTP (FileZilla recomendado):
   - Host: `<IP>` | Porta: `21` | User: `esp32` | Pass: `esp32`
2. Conectar → ver pasta `/CICLOS/`.

### 6.4 Health check
- Endpoint sem auth: `http://<IP>/api/health`
- Deve retornar: `{"online":true,"uptime_s":NNN}`

---

## 7. Configurações de segurança recomendadas

### 7.1 Trocar PIN do portal
1. Configurações → Sistema → PIN.
2. Digite novo PIN (4 dígitos numéricos).
3. Anotar! Não há reset remoto.

### 7.2 Trocar credenciais FTP
- Configurações → FTP.
- Alterar usuário/senha. Documente em local seguro.

### 7.3 Rede isolada (ideal)
- Colocar FitaDigital em **VLAN/subnet isolada** sem acesso à internet (exceto NTP se necessário).
- Permitir apenas PCs autorizados a aceder porta 21 (FTP) e 80 (HTTP).

---

## 8. Troubleshooting de instalação

### 8.1 Boot fica preso em "SD" ou "SD" vermelho
- Cartão não detetado / corrupto.
- Solução: trocar SD (FAT32, ≤ 32 GB, marca confiável).

### 8.2 Wi-Fi não conecta
- Verificar SSID/senha (case-sensitive).
- 5 GHz não suportado → confirmar rede em **2.4 GHz**.
- RSSI fraco → aproximar do router ou usar repetidor.
- Caracteres especiais na senha podem causar problema — testar senha simples primeiro.

### 8.3 RS485 sem dados
- Inverter A↔B (causa mais comum).
- Confirmar baud rate (Configurações → RS485) = baud do equipamento.
- Multímetro: medir entre A e B → deve ter sinal alternante (-7 V a +7 V) quando equipamento transmite.
- Cabo blindado preferível em ambiente com motores/inversores próximos.
- GND comum recomendado se equipamentos em terras diferentes.

### 8.4 Portal web não abre
- Confirmar IP correto (barra superior do FitaDigital).
- Ping do PC: `ping <IP>` deve responder.
- Firewall do Windows pode bloquear — testar em incógnito ou outro PC.

### 8.5 Hora errada após boot
- NTP não sincronizou — verificar internet ou apontar para NTP local.
- Bateria do RTC pode estar fraca — contactar assistência (Manual de Serviço).

---

## 9. Manutenção pelo instalador

- Verificação anual: confirmar conexões RS485 firmes (parafusos do borne).
- Limpeza do conector RS485 com contact cleaner se ambiente sujo.
- Atualização firmware: ver **Manual de Serviço**.

---

## 10. Anexo — Especificações técnicas

| Item | Valor |
|------|-------|
| MCU | ESP32-S3 (240 MHz dual core, 512 KB RAM + 16 MB PSRAM) |
| Flash | 16 MB QIO |
| Display | LCD 4.3" 800×480 capacitivo touchscreen |
| Wi-Fi | 802.11 b/g/n 2.4 GHz |
| RS485 | Half-duplex, isolado, 9600 baud default (configurável 1200-115200) |
| SD | microSD até 32 GB FAT32 |
| Alimentação | USB-C 5 V / 1 A (~ 3 W) |
| Temperatura operação | 0-50 °C |
| Dimensões | 130 × 90 × 25 mm (com case) |
