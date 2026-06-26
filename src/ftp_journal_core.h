/**
 * @file ftp_journal_core.h
 * @brief Logica pura do cliente FTP-upload (sem dependencias Arduino):
 *        journal (relpath->size), deteccao de mudanca, mapeamento de caminhos remotos.
 *        Testavel no host (env:native).
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ftpj {

using JournalMap = std::map<std::string, long>;  // relpath (relativo a /CICLOS) -> tamanho

/** Faz parse de linhas "relpath|size"; ignora vazias/malformadas. */
JournalMap parse_journal(const std::string &text);

/** Serializa o mapa de volta para texto "relpath|size\n" (ordem por chave). */
std::string serialize_journal(const JournalMap &m);

/** True se o ficheiro precisa de (re)upload: ausente no journal OU tamanho diferente. */
bool needs_upload(const JournalMap &j, const std::string &relpath, long cur_size);

/** Junta base + relpath em caminho remoto com '/' unico (sem barra dupla, sem barra final). */
std::string remote_path(const std::string &base, const std::string &relpath);

/** Lista ordenada de directorios a criar (MKD) para o relpath, incluindo a base.
 *  Ex.: base="/up", relpath="2026/06/a.txt" -> {"/up","/up/2026","/up/2026/06"}. */
std::vector<std::string> remote_dir_components(const std::string &base, const std::string &relpath);

}  // namespace ftpj
