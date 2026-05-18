# FitaDigital — Manual do Usuário

**Versão firmware:** v2.0.0
**Público-alvo:** operador / enfermeiro / técnico de chão de fábrica

---

## 1. Para que serve

FitaDigital substitui a impressora de papel térmico das autoclaves e equipamentos hospitalares/industriais. Cada ciclo do equipamento que normalmente seria impresso em papel é **capturado pelo cabo RS485 e gravado em arquivo `.txt`** no cartão SD interno. Esses arquivos podem ser visualizados na tela do próprio aparelho, descarregados pela rede ou acessados via FTP.

**Vantagens:**
- Sem consumo de papel / tinta
- Histórico ilimitado (cartão SD 16+ GB)
- Acesso remoto pela rede do estabelecimento
- Backup automático organizado por data

---

## 2. Componentes do aparelho

| Item | Descrição |
|------|-----------|
| Tela LCD 4.3" touchscreen | Mostra status + permite navegar nos arquivos |
| Porta USB-C (atrás) | Alimentação (5 V / 1 A) |
| Conector RS485 (atrás) | Cabo que vai da autoclave/equipamento ao FitaDigital |
| Cartão microSD (lateral) | Onde os arquivos `.txt` são gravados |
| LEDs internos | Indicação visual de boot/atividade |

---

## 3. Ligar o aparelho

1. Conecte o cabo USB-C de alimentação. **Não há botão liga/desliga** — basta dar energia.
2. Após ~10 segundos a tela mostra **boot screen** com cada etapa: SD, Wi-Fi, NTP, FTP, Portal.
3. Quando boot terminar, aparece a **tela principal** com:
   - Barra superior: ícone Wi-Fi + força sinal + data/hora
   - Lista de arquivos do SD (mais recentes em cima)
   - Botão **Configurações** (canto)

**Tudo verde na barra = funcionando.** Vermelho ou amarelo = ver Seção 6.

---

## 4. Uso diário

### 4.1 Ver o último ciclo capturado

1. Na tela principal, o arquivo do dia (ex.: `20260518.txt`) aparece no topo da lista.
2. Toque no arquivo → abre **viewer** com o conteúdo do(s) ciclo(s) do dia.
3. Linhas novas (recém capturadas) ficam **destacadas a piscar** alguns segundos.
4. Use 2 dedos para fazer scroll ou os botões de seta.
5. Botão **voltar** retorna à lista.

### 4.2 Mudar o tamanho da fonte do viewer

Configurações → Interface → **Tamanho de fonte (0-3)**:
- 0 = pequena (mais linhas na tela)
- 3 = grande (mais legível à distância)

### 4.3 Navegar entre dias anteriores

Lista principal mostra os últimos arquivos. Para histórico antigo:
- Acesse pelo **portal web** (Seção 5) → tab Ficheiros → pasta `/CICLOS/AAAA/MM/`
- Ou pelo **FTP** (Seção 5.2)

---

## 5. Acesso remoto

Cada arquivo gravado fica disponível pela rede do estabelecimento.

### 5.1 Pelo navegador (portal web)

1. Veja o **IP do aparelho** na barra superior da tela.
2. Em qualquer PC/tablet/celular da mesma rede, abra: `http://<IP>/`
3. Login: usuário `admin`, senha = **PIN configurado** (default `1234`).
4. Você verá 4 abas:
   - **Config** — ajustar Wi-Fi, NTP, fonte (uso técnico)
   - **Logs** — diagnóstico
   - **Ficheiros** — navegar SD, baixar `.txt` dos ciclos
   - **OTA** — atualização firmware (uso técnico)

### 5.2 Pelo cliente FTP

Programas como FileZilla, WinSCP, Total Commander:
- Host: IP do aparelho
- Porta: 21
- Usuário: `esp32` (default)
- Senha: `esp32` (default)
- Protocolo: FTP simples (não FTPS)

Os arquivos ficam em `/CICLOS/AAAA/MM/AAAAMMDD.txt`.

---

## 6. Indicadores de status

### Barra superior
| Cor / símbolo | Significado |
|---------------|-------------|
| Wi-Fi verde + força | Conectado, RSSI bom |
| Wi-Fi amarelo | Conectado, sinal fraco |
| Wi-Fi vermelho ou ausente | Desconectado — ver Seção 7.1 |
| Relógio piscando ou em 2020 | NTP não sincronizado — ver 7.4 |

### Tela principal
- **Novos arquivos** aparecem no topo automaticamente.
- **Linha nova destacada** = ciclo a chegar agora pela RS485.

---

## 7. Problemas comuns

### 7.1 Wi-Fi vermelho / não associa

- Verificar que SSID e senha foram digitados corretamente em Configurações → Rede.
- Aproximar o aparelho do router (RSSI ≥ -75 dBm desejável).
- Reiniciar (desconectar/conectar USB) — geralmente reassocia.

### 7.2 Não vejo arquivo novo após ciclo do equipamento

- Confirmar **cabo RS485** bem conectado nos 2 lados (A↔A, B↔B).
- Verificar baud rate (default 9600 8N1) corresponde ao equipamento — Configurações → RS485.
- Tela principal: a lista atualiza automática. Se nada chega, ver Logs no portal web.

### 7.3 Tela congelada / preta

- Aguardar 30 s (pode estar em boot pesado).
- Se persistir: desligar USB, esperar 5 s, religar.

### 7.4 Data/hora errada

- Aparelho precisa de Wi-Fi + NTP para sincronizar relógio.
- Configurações → NTP → confirmar servidor (default `pool.ntp.org`).
- Após ~1 min de Wi-Fi up o relógio acerta.

### 7.5 Cartão SD cheio / não detetado

- Trocar por SD ≥ 16 GB, FAT32 (não exFAT, não NTFS).
- Após formatar, ligar de novo — sistema cria `/CICLOS/` automaticamente.

---

## 8. Manutenção

- **Limpeza:** pano macio levemente humedecido. Nunca solventes na tela.
- **SD:** copiar arquivos para PC mensalmente (segurança). FitaDigital não apaga sozinho.
- **Reboot preventivo:** desligar/ligar USB 1× por semana = boa prática.
- **NÃO abrir o aparelho.** Não há partes internas para manutenção pelo usuário.

---

## 9. Suporte

Em caso de falha persistente:
1. Anotar **versão firmware** (Configurações → Sobre): `v2.0.0`
2. Tirar foto da tela com erro / situação
3. Contactar suporte técnico do fornecedor com: IP do aparelho, versão, descrição

**Não tente atualizar firmware sozinho** — peça ao técnico de serviço (Manual de Serviço).
