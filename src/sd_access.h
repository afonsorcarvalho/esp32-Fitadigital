/**
 * @file sd_access.h
 * @brief Acesso exclusivo ao cartao SD: fila + tarefa unica (sem concorrencia com FTP/LVGL/web).
 *
 * Qualquer operacao que use SD.open / SD.mkdir / FatFs deve passar por sd_access_sync()
 * ou correr na propria tarefa sd_io (reentrancia detetada automaticamente).
 */
#pragma once

#include <functional>
#include <stdint.h>
#include <time.h>

/** Chamado apos SD.begin com sucesso (ou false se falhou). */
void sd_access_set_mounted(bool mounted);

/** Indica se o volume foi montado no arranque (sem tocar no hardware). */
bool sd_access_is_mounted(void);

/**
 * Inicia a tarefa FreeRTOS "sd_io" (fila + processamento em serie).
 * Invocar depois de SD.begin e antes de net_services / espelho journal / logs em segundo plano.
 */
void sd_access_start_task(void);

/**
 * Executa o callback na tarefa sd_io (unica que acede ao SD).
 * Bloqueia a tarefa chamadora ate concluir. Seguro entre tarefas.
 * Se ja estiver na tarefa sd_io, executa inline (evita deadlock).
 */
void sd_access_sync(const std::function<void()> &fn);

/** Variante async: enfileira sem bloquear; a ordem e preservada. */
void sd_access_async(const std::function<void()> &fn);

/**
 * Regista que o conteudo do SD foi alterado (escrita, delete, mkdir, upload FTP, etc.).
 * Thread-safe: pode ser chamado de qualquer task. Atualiza o timestamp de modificacao.
 */
void sd_access_notify_changed(void);

/**
 * Timestamp (millis) da ultima modificacao conhecida no SD.
 * A UI compara este valor com o seu proprio timestamp de refresh para decidir se atualiza.
 */
uint32_t sd_access_last_modified_ms(void);

/**
 * Obtem a data/hora de modificacao de um ficheiro/pasta directamente do FAT32 (fdate/ftime).
 * @param vfs_path Caminho VFS completo (ex: "/sd/teste/foto.jpg") OU relativo (ex: "/teste/foto.jpg").
 * @return time_t (hora local conforme gravada no FAT) ou 0 se nao for possivel obter.
 *
 * IMPORTANTE: chamar APENAS dentro de sd_access_sync() ou da tarefa sd_io,
 * pois f_stat nao e' thread-safe e partilha o mutex do volume FatFs.
 */
time_t sd_access_fat_mtime(const char *vfs_path);
