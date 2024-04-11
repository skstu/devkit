#include "stdafx.h"

static void spawn_exit_cb(uv_process_t *req, int64_t exit_status,
                          int term_signal) {
  // fprintf(stderr, "Process exited with status %" PRId64 ", signal %d\n",
  //         exit_status, term_signal);
  uv_close((uv_handle_t *)req, NULL);
}

int sk_spawn(const char *file, const char **args, size_t argc, int wait) {
  int r = SK_NO;
  do {
    uv_loop_t *loop = sk__malloc(sizeof(uv_loop_t));
    r = uv_loop_init(loop);
    if (r)
      break;
    uv_process_options_t *pro_opt = sk__malloc(sizeof(uv_process_options_t));
    pro_opt->exit_cb = spawn_exit_cb;
    pro_opt->file = file;
    pro_opt->args = (char **)args;
    pro_opt->flags = UV_PROCESS_DETACHED;

    uv_process_t *req = sk__malloc(sizeof(uv_process_t));
    req->data = pro_opt;
    if (0 != (r = uv_spawn(loop, req, pro_opt)))
      break;
    // 启用 uv_unref 不执行回调直接退出
    // 减少对子进程的引用，以允许程序退出
    uv_unref((uv_handle_t *)req);
    r = uv_run(loop, UV_RUN_DEFAULT);
    sk__free(req->data);
    sk__free(req);
    uv_loop_close(loop);
    sk__free(loop);
  } while (0);

  return r;
}