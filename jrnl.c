/* Copyright 2016 Connor Taffe */
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <err.h>

#include "jrnl.h"

void jrnl_init(struct jrnl *j) {
  assert(j != NULL);
  int lsz = snprintf(NULL, 0, "/var/log/jrnld.%x.log", getpid());
  if (lsz < 0) {
    err(1, "couldn't format path");
  }
  char *log = calloc(sizeof(char), (size_t)lsz+1);
  if (log == NULL) {
    err(1, "couldn't allocate memory");
  }
  memset(j, sizeof(struct jrnl), 0); /* zero jrnl */
  if (snprintf(log, sizeof(log), "/var/log/jrnld.%x.log", getpid()) < 0) {
    err(1, "couldn't format path");
  }
  j->log = open(log, O_CREAT | O_APPEND);
  if (j->log < 0) {
    err(1, "couldn't open log");
  }
}

void jrnl_fini(struct jrnl *j) {
  assert(j != NULL);
  if (close(j->log) < 0) {
    warn("couldn't close log");
  }
}

/* jrnl_logf: printf-style formatted logging  */
void jrnl_logf(struct jrnl *j, char *fmt, ...) {
  va_list ap;
  /* allocate buffer */
  int bsz = vsnprintf(NULL, 0, fmt, ap);
  if (bsz < 0) {
    err(1, "couldn't format log entry");
  }
  char *buf = calloc(sizeof(char), (size_t)bsz);
  if (buf == NULL) {
    err(1, "couldn't allocate memory");
  }
  /* print fmt to buf */
  va_start(ap, fmt);
  if (vsnprintf(buf, sizeof(buf), fmt, ap) < 0) {
    err(1, "couldn't format log entry");
  }
  va_end(ap);
  /* log to file with time */
  time_t rtime;
  time(&rtime);
  int sbsz = snprintf(NULL, 0, "%s: %s\n", ctime(&rtime), buf);
  if (sbsz < 0) {
    err(1, "couldn't format log entry");
  }
  char *sbuf = calloc(sizeof(char), (size_t)sbsz+1);
  if (sbuf == NULL) {
    err(1, "couldn't allocate memory");
  }
  if (snprintf(sbuf, (size_t)sbsz, "%s: %s\n", ctime(&rtime), buf) < 0) {
    err(1, "couldn't format log entry");
  }
  free(buf);
  if (write(j->log, sbuf, (size_t)sbsz) < 0) {
    err(1, "couldn't write log entry");
  }
  free(sbuf);
}
