/**
 * @file board_i2c0_bus_lock.h
 * @brief Mutex do barramento I2C_NUM_0 na Waveshare 4.3B (CH422G + RTC PCF85063A).
 *
 * O ESP_Panel inicializa o I2C0; o CS do TF passa pelo CH422G (`waveshare_sd_cs`) e o
 * `net_time` acede ao RTC no mesmo porto. O mutex serializa estes dois percursos na app.
 * Touch/outros no I2C0 podem ainda competir com a stack Espressif; opcionalmente use
 * `board_i2c0_bus_quiet_ioexpander_serial_logs()` para nao inundar a Serial com ESP_LOGE.
 */
#pragma once

#include <stdint.h>

void board_i2c0_bus_lock_init(void);
void board_i2c0_bus_lock(void);
void board_i2c0_bus_unlock(void);

/**
 * Numero acumulado de timeouts ao tomar o lock I2C0 (> 0 indica contencao
 * grave ou task presa no bus — util para telemetria e diagnostico).
 */
uint32_t board_i2c0_bus_lock_timeouts(void);

/**
 * Silencia na Serial os ESP_LOGE das libs `ESP_IOExpander` / `ch422g` (ruido por I2C0
 * partilhado com touch/stack Espressif). Nao altera o nivel de outros componentes.
 */
void board_i2c0_bus_quiet_ioexpander_serial_logs(void);
