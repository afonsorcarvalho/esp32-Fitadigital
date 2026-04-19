/**
 * @file net_services.h
 * @brief Wi-Fi (STA) e servidor FTP sobre o SD (SimpleFTPServer + STORAGE_SD).
 */
#pragma once

void net_wifi_begin(const char *ssid, const char *pass);
void net_wifi_begin_saved(void);

/**
 * Um ciclo de rede (Wi-Fi ligado, NTP, FTP, WireGuard). Corrido pela tarefa FreeRTOS
 * em net_services_start_background_task() (mesmo core que Arduino/LVGL — API Wi-Fi).
 */
void net_services_loop(void);

/** Cria a tarefa "net_svc" e deixa de ser necessario chamar net_services_loop no loop(). */
void net_services_start_background_task(void);

/**
 * Para o servidor FTP para aplicar novas credenciais; no proximo net_services_loop()
 * volta a iniciar com app_settings_ftp_* (buffers internos estaveis).
 */
void net_services_ftp_restart(void);

/**
 * Suspende/retoma o FTP sem afetar Wi-Fi/NTP/WireGuard.
 * Util para operacoes exclusivas no SD (ex.: formatacao).
 */
void net_services_set_ftp_suspended(bool suspended);

/**
 * Ciclo FTP (SimpleFTPServer) + sondagem SD para iniciar FTP.
 * Registado em sd_access via sd_access_register_tick() no app.cpp;
 * executado exclusivamente pela tarefa sd_io para evitar concorrencia.
 * Usa polling adaptativo: ~10 Hz sem cliente, frequencia maxima com cliente ativo.
 */
void net_services_sd_worker_tick(void);
