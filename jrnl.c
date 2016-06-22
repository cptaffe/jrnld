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
  int lsz;
  char *log;
  assert(j != NULL);
  lsz = snprintf(NULL, 0, "/var/log/jrnld.%x.log", getpid());
  if (lsz < 0) {
    err(1, "couldn't format path");
  }
  log = calloc(sizeof(char), (size_t)lsz+1);
  if (log == NULL) {
    err(1, "couldn't allocate memory");
  }
  memset(j, sizeof(struct jrnl), 0); /* zero jrnl */
  if (snprintf(log, (size_t)lsz+1, "/var/log/jrnld.%x.log", getpid()) != lsz) {
    err(1, "couldn't format path");
  }
  j->log = open(log, O_CREAT | O_WRONLY);
  if (j->log < 0) {
    err(1, "couldn't open log");
  }
  free(log);
  j->logger = jrnl_time_logger;
}

void jrnl_fini(struct jrnl *j) {
  assert(j != NULL);
  if (close(j->log) < 0) {
    warn("couldn't close log");
  }
}

/* handler returns an allocated string */
ssize_t jrnl_time_logger(struct jrnl *j, const char *msg, char **out) {
  time_t raw;
  struct tm *gmt;
  int sbsz;
  size_t tbsz;
  char *tbuf, *sbuf;
  /* fmt time to tbuf */
  time(&raw);
  gmt = gmtime(&raw);
  if (gmt == NULL) {
    return -1;
  }
  tbsz = 200; /* 200 is taken from the man page */
  tbuf = calloc(sizeof(char), tbsz);
  if (tbuf == NULL) {
    return -1;
  }
  strftime(tbuf, tbsz, "%c", gmtime(&raw));
  /* format tbuf and buf to sbuf */
  sbsz = snprintf(NULL, 0, "%s: %s\n", tbuf, msg);
  if (sbsz < 0) {
    return -1;
  }
  sbuf = calloc(sizeof(char), (size_t)sbsz+1);
  if (sbuf == NULL) {
    return -1;
  }
  if (snprintf(sbuf, (size_t)sbsz+1, "%s: %s\n", tbuf, msg) != sbsz) {
    return -1;
  }
  free(tbuf); /* cleanup */
  *out = sbuf;
  return (ssize_t)sbsz;
}

/* jrnl_logf: printf-style formatted logging  */
void jrnl_logf(struct jrnl *j, char *fmt, ...) {
  va_list ap;
  int bsz;
  ssize_t sbsz;
  char *buf, *sbuf = NULL;
  va_start(ap, fmt);
  /* fmt to buf */
  bsz = vsnprintf(NULL, 0, fmt, ap);
  if (bsz < 0) {
    err(1, "couldn't format log entry");
  }
  buf = calloc(sizeof(char), (size_t)bsz+1);
  if (buf == NULL) {
    err(1, "couldn't allocate memory");
  }
  if (vsnprintf(buf, (size_t)bsz+1, fmt, ap) != bsz) {
    err(1, "couldn't format log entry");
  }
  va_end(ap);
  /* logger formats message */
  sbsz = j->logger(j, buf, &sbuf);
  if (sbsz < 0 || sbuf == NULL) {
    err(1, "logger couldn't format entry");
  }
  free(buf); /* cleanup */
  /* write to log */
  if (write(j->log, sbuf, (size_t)sbsz) != (ssize_t)sbsz) {
    err(1, "couldn't write log entry");
  }
  free(sbuf); /* cleanup */
}

void jrnl_listen(struct jrnl *j, int (*handler)(struct jrnl *j, int sock)) {
  jrnl_logf(j, "listening...");
}
