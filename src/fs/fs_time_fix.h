#pragma once

#include <stdint.h>
#include <time.h>

/**
 * @brief Reaplica um timestamp em todos os ficheiros/pastas sob /sd.
 *
 * Util para corrigir itens criados antes de uma base horaria valida.
 * @param epoch_ts Timestamp (epoch UTC) a aplicar em atime/mtime.
 * @return Quantidade de entradas atualizadas.
 */
uint32_t fs_time_fix_touch_all(const char *root_path, time_t epoch_ts);

