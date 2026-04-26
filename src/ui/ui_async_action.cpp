#include "ui_async_action.h"

#include <atomic>

#include "ui_loading.h"
#include "../sd_access.h"

namespace {

struct Ctx {
  UiAsyncWorkFn work;
  UiAsyncFinishFn finish;
  void *user_data;
};

/* Single slot — UI e' single-thread (LVGL task), nao precisa pool. */
static Ctx s_ctx = {nullptr, nullptr, nullptr};
static std::atomic<bool> s_busy{false};

static void async_finish_trampoline(void * /*lv_async_user_data*/) {
  /* Corre na task LVGL com lock automatico (lv_async_call). */
  ui_loading_hide();
  Ctx local = s_ctx;
  s_ctx = {nullptr, nullptr, nullptr};
  s_busy.store(false, std::memory_order_release);
  if (local.finish != nullptr) {
    local.finish(local.user_data);
  }
}

static void async_work_trampoline(void) {
  /* Corre na task sd_io (sem lock LVGL). */
  if (s_ctx.work != nullptr) {
    s_ctx.work(s_ctx.user_data);
  }
  lv_async_call(async_finish_trampoline, nullptr);
}

} /* namespace */

bool ui_async_action_busy(void) {
  return s_busy.load(std::memory_order_acquire);
}

bool ui_async_action_run(lv_obj_t *overlay_parent,
                         const char *overlay_message,
                         uint32_t overlay_delay_ms,
                         UiAsyncWorkFn work_fn,
                         UiAsyncFinishFn finish_fn,
                         void *user_data) {
  bool expected = false;
  if (!s_busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    /* Outra acao em curso — rejeitar para evitar conflict. Caller deve evitar
     * disparar accoes concorrentes via UI (botoes desactivados, etc.). */
    return false;
  }
  s_ctx.work = work_fn;
  s_ctx.finish = finish_fn;
  s_ctx.user_data = user_data;

  ui_loading_show_delayed(overlay_parent, overlay_message, overlay_delay_ms);
  sd_access_async([] { async_work_trampoline(); });
  return true;
}
