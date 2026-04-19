/**
 * @file board_pins.h
 * @brief Ligações do TF card na Waveshare ESP32-S3-Touch-LCD-4.3B.
 *
 * Fonte: tabela "TF Card Interface" em
 * https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B
 * (GPIO11 MOSI, GPIO12 SCK, GPIO13 MISO; SD_CS em CH422G EXIO4).
 *
 * Mapeamento EXIOx ↔ índice no driver: ESP32_IO_Expander `CH422G.h` (IO4 = índice 4).
 */
#pragma once

/** Pinos SPI do slot TF (ESP32-S3). */
#define BOARD_TF_SPI_MOSI   (11)
#define BOARD_TF_SPI_SCK    (12)
#define BOARD_TF_SPI_MISO   (13)

/** Chip select do SD no expansor CH422G (EXIO4 → índice 4, ativo em baixo). */
#define BOARD_TF_SD_CS_EXPANDER_PIN  (4)

/**
 * Buzzer / piezo (opcional): a Waveshare ESP32-S3-Touch-LCD-4.3B nao traz buzzer de serie
 * (docs Pinouts / Onboard Resources). Para bip ao toque, ligue buzzer activo ou piezo + transistor
 * a um GPIO livre e defina o numero aqui. -1 = desactivado.
 */
#define BOARD_UI_BEEP_GPIO (-1)

/**
 * RS485 (transceiver onboard): tabela "RS485 interface" na wiki Waveshare ESP32-S3-Touch-LCD-4.3B.
 * https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3B
 * Arduino de exemplo usa Serial1 com estes pinos.
 */
#define BOARD_RS485_RX_GPIO (43)
#define BOARD_RS485_TX_GPIO (44)
