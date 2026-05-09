#include "fs_time_fix.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utime.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

static bool is_dot_name(const char *name) {
  return (strcmp(name, ".") == 0) || (strcmp(name, "..") == 0);
}

/*
 * Diretorios que nao precisam de mtime fix (conteudo transient/regenerado).
 * Saltar evita gastar segundos em FAT lookups + alimenta WDT em diretorios
 * grandes (ex.: /sd/screenshots pode ter centenas de JPEGs apos soak).
 */
static bool should_skip_subdir(const char *child_path) {
  return strstr(child_path, "/screenshots") != nullptr ||
         strstr(child_path, "/SCREENSHOTS") != nullptr;
}

/*
 * Limiar: ficheiros com mtime >= kValidEpoch ja' tinham hora valida quando
 * foram escritos — nao devem ser tocados para preservar o timestamp real.
 * 2020-01-01 00:00:00 UTC.
 */
static constexpr time_t kValidEpoch = (time_t)1577836800;

static uint32_t touch_tree_recursive(const char *path, const struct utimbuf *tb) {
  uint32_t touched = 0;

  DIR *dir = opendir(path);
  if (dir == nullptr) {
    return 0;
  }

  struct dirent *ent = nullptr;
  uint32_t iter = 0;
  while ((ent = readdir(dir)) != nullptr) {
    /* Yield a cada 16 entradas: stat() chama SPI sincrono na sd_io task,
     * uma traversal grande (ex.: /screenshots com centenas de ficheiros)
     * mantem CPU 1 ocupado >5s e dispara TWDT em async_tcp. */
    if ((++iter & 0x0FU) == 0U) {
      vTaskDelay(1);
    }

    if (is_dot_name(ent->d_name)) {
      continue;
    }

    char child[384];
    const int n = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
    if (n <= 0 || (size_t)n >= sizeof(child)) {
      continue;
    }

    struct stat st = {};
    if (stat(child, &st) != 0) {
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      if (should_skip_subdir(child)) {
        continue;
      }
      touched += touch_tree_recursive(child, tb);
      if (st.st_mtime < kValidEpoch && utime(child, tb) == 0) {
        touched++;
      }
    } else if (S_ISREG(st.st_mode)) {
      if (st.st_mtime < kValidEpoch && utime(child, tb) == 0) {
        touched++;
      }
    }
  }

  closedir(dir);
  return touched;
}

}  // namespace

uint32_t fs_time_fix_touch_all(const char *root_path, time_t epoch_ts) {
  if (root_path == nullptr || root_path[0] == '\0') {
    return 0;
  }

  if (epoch_ts <= (time_t)1577836800) {
    return 0;
  }

  struct utimbuf tb = {};
  tb.actime = epoch_ts;
  tb.modtime = epoch_ts;

  uint32_t touched = touch_tree_recursive(root_path, &tb);

  struct stat root_st = {};
  if (stat(root_path, &root_st) == 0 && root_st.st_mtime < kValidEpoch) {
    if (utime(root_path, &tb) == 0) {
      touched++;
    }
  }
  return touched;
}

