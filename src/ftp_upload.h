/**
 * @file ftp_upload.h
 * @brief Cliente FTP-upload: empurra /CICLOS para um servidor FTP remoto,
 *        com journal por tamanho e verificacao via comando SIZE.
 *        Task dedicada `ftp_up`; leitura do SD em chunks via sd_access (RS485-safe).
 */
#pragma once

/** Cria a task `ftp_up`. Chamar no boot, depois de sd_access_start_task(). */
void ftp_upload_init(void);

/** Pede uma passagem de upload imediata (acordar a task). Thread-safe. */
void ftp_upload_request_now(void);
