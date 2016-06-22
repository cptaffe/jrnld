/* Copyright 2016 Connor Taffe */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "jrnl.h"

int jrnld(void);
int jrnld_listen_handler(struct jrnl *j, int sock);

int jrnld_listen_handler(struct jrnl *j, int sock) {
  return 0;
}

int jrnld() {
  struct jrnl j;
  assert(umask(0) > 0);
  jrnl_init(&j);
  /* set new sid */
  if (setsid() < 0) {
    jrnl_logf(&j, "setsid(): %s", strerror(errno));
    return -1;
  }
  /* chdir to root */
  if (chdir("/") < 0) {
    jrnl_logf(&j, "chdir(/): %s", strerror(errno));
    return -1;
  }
  /* close standard fds */
  int fds[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
  for (size_t i = 0; i < (sizeof(fds)/sizeof(int)); i++) {
    if (close(fds[i]) < 0) {
      jrnl_logf(&j, "close(%d): %s", fds[i], strerror(errno));
      return -1;
    }
  }
  jrnl_logf(&j, "jrnld here");
  jrnl_listen(&j, jrnld_listen_handler);
  jrnl_fini(&j);
  return 0;
}

int main() {
  pid_t p = fork();
  assert(p >= 0);
  if (p > 0)
    exit(0);
  exit(jrnld() < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
}
