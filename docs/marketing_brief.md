# Brief de marketing — FitaDigital

Este documento e' para passar ao Claude Design (ou equivalente) para produzir
pecas de comunicacao de vendas do produto FitaDigital.

---

## 1. Produto em uma frase

FitaDigital e' um substituto digital da impressora termica de ciclos em
equipamentos industriais e hospitalares: captura o relatorio via RS485 e
guarda automaticamente no SD, com visualizacao em ecra tactil 4,3", acesso
remoto por Wi-Fi (FTP + portal web + QR code) e monitorizacao de ligacao
em tempo real.

## 2. Problema que resolve

Autoclaves, seladoras, estufas, termoformadoras e outros equipamentos
industriais/hospitalares emitem relatorio de cada ciclo numa **impressora
termica antiga** (ou matricial). Esse papel:

- Desbota em 6-24 meses (perda de rastreabilidade regulatoria).
- Custa dinheiro continuo (rolos, manutencao da cabeca termica, jams).
- Nao e' digital: o tecnico tem de arquivar, fotografar ou transcrever.
- Nao viaja: se o equipamento esta num andar, o responsavel no escritorio
  nao ve o ciclo sem deslocar-se.

FitaDigital elimina estes atritos sem tocar no equipamento existente —
basta interceptar o cabo serie da impressora.

## 3. Publico-alvo

**Primario:**
- Gestores/engenheiros de manutencao em industrias reguladas (farmaceutica,
  alimentar, hospitalar, plasticos, embalagem).
- Responsaveis de esterilizacao em hospitais/clinicas (CSSD).

**Secundario:**
- Empresas de assistencia tecnica que mantem frotas de autoclaves.
- Fabricantes OEM que querem "modernizar" a sua linha sem redesenhar o
  firmware do equipamento.

## 4. Beneficios principais (usar como pilares de comunicacao)

1. **Zero papel.** Sem rolos, sem manutencao da impressora, sem arquivo
   fisico. Compatibilidade com equipamento existente — basta substituir a
   ligacao da impressora pelo FitaDigital.
2. **Rastreabilidade permanente.** Ciclos guardados em `.txt` no SD com
   timestamp FAT32 valido; backup instantaneo via FTP ou portal web.
3. **Acesso remoto imediato.** QR code no ecra -> smartphone/tablet abre
   o ficheiro sem cabos nem software. Portal web no browser em qualquer
   rede.
4. **Visibilidade em tempo real.** Dashboard no ecra mostra captura ativa,
   linhas gravadas hoje, ultima gravacao, estado da rede e de um IP de
   referencia (ping monitor).
5. **Instalacao em minutos.** Liga-se em serie RS485 (ou RS232 via
   conversor), configura-se a rede Wi-Fi no ecra tactil e esta feito.

## 5. Caracteristicas tecnicas (para material detalhado)

- Ecra tactil IPS 4,3" 800x480.
- ESP32-S3 dual-core 240 MHz, Wi-Fi 2.4 GHz.
- RS485 configuravel (300-921600 baud, 8N1 a 7N2).
- SD ate 32 GB, FAT32 (MBR).
- Portal HTTP + servidor FTP integrados.
- QR code automatico para partilha de qualquer ciclo.
- Monitorizacao de conectividade por ICMP ping a um IP configuravel.
- Sincronizacao de hora via NTP (RTC externo mantem tempo sem rede).
- Caixa compacta para montagem em rack 19" ou parede (dimensoes
  finais a confirmar).

## 6. Tom de voz desejado

- **Pragmatico e tecnico**, sem hype. Publico-alvo e' engenharia, nao
  consumidor final.
- **Centrado no ganho operacional** (tempo poupado, risco regulatorio
  evitado) mais do que em "revolucao" ou "IoT".
- **Portugues europeu** tecnico (pt-PT ou pt-BR neutro, sem calao).

## 7. O que pedir ao Claude Design

1. **Pagina de produto** (web) de 1 scroll: hero com ecra de dashboard +
   headline + 3 a 5 cartoes de beneficio + CTA "Pedir demonstracao".
2. **One-pager PDF** A4 para equipa comercial levar a reunioes:
   problema → solucao → especificacoes → contacto.
3. **5 posts LinkedIn** (texto + sugestao de visual) para engenheiros de
   manutencao em industrias reguladas.
4. **Hero graphic:** mockup do ecra com dashboard real (ver screenshot
   incluido na pasta do produto quando existir).
5. **Casos de uso ilustrados** (2 ou 3): autoclave hospitalar,
   termoformadora alimentar, estufa laboratorial. Cada um com mini
   storyline de 3 passos.

## 8. Diferenciacao vs. alternativas

- Vs. impressora termica (status quo): mais barato por ciclo, sem
  consumiveis, acesso remoto nativo.
- Vs. data loggers dedicados: instalacao nao-intrusiva (nao requer
  reconfigurar o equipamento), visualizacao direta no ecra.
- Vs. solucoes cloud-first: funciona offline, dados ficam localmente
  (requisito comum em farmaceutica/hospitalar).

## 9. O que NAO dizer (pelo menos agora)

- Nao prometer certificacao FDA, CE Class II ou integracao SAP/ERP por
  defeito — sao pedidos custom.
- Nao posicionar como "IoT" genericamente: o valor e' manter o processo
  existente, nao reinventa-lo.
- Nao garantir latencia de "tempo real" abaixo de 1 s (o refresh da UI
  e' 500 ms por design para nao sobrecarregar o SD).

## 10. Assets de apoio a incluir

- Screenshots do dashboard (captura RS485, SD, Wi-Fi).
- Fotos de autoclave hospitalar / estufa / termoformadora.
- Diagrama de ligacao "impressora -> FitaDigital" em 3 blocos.

---

**Notas finais:**
- Nome do produto: **FitaDigital** (uma palavra, maiuscula inicial).
- Versao atual do firmware: **1.02**.
- Logo e cor primaria: verde institucional `#449D48` (AFR).
- Manter a proposta simples: "troca a impressora, mantem os ciclos".
