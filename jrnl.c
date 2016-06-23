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
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "jrnl.h"

static const char *JRNL_SOCKET_PATH = "/var/run/jrnl.sock";
static const char *JRNL_LOG_PATH = "/var/log/jrnl.log";

/* set up unix socket server */
static void jrnl_server_init(struct jrnl *j) {
  struct sockaddr_un jrnls;
  assert(j != NULL);

  /* create socket */
  j->sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (j->sock == -1)
    jrnl_logf(j, "couldn't create socket: %s", strerror(errno));

  /* bind socket */
  memset(&jrnls, 0, sizeof(jrnls));
  jrnls.sun_family = AF_UNIX;
  strncpy(jrnls.sun_path, "/var/run/jrnl.sock", sizeof(jrnls.sun_path)-1);
  if (bind(j->sock, (struct sockaddr *)&jrnls, sizeof(jrnls)) == -1)
    jrnl_logf(j, "couldn't bind socket: %s", strerror(errno));

  /* listen on socket */
  if (listen(j->sock, 64) == -1)
    jrnl_logf(j, "couldn't listen on socket: %s", strerror(errno));
}

static void jrnl_log_init(struct jrnl *j) {
  assert(j != NULL);

  /* open */
  j->log = open(JRNL_LOG_PATH, O_CREAT | O_WRONLY);
  if (j->log < 0)
    err(1, "couldn't open log");
}

void jrnl_init(struct jrnl *j) {
  assert(j != NULL);
  memset(j, 0, sizeof(struct jrnl));
  j->logger = jrnl_time_logger;
  jrnl_log_init(j);
}

void jrnl_fini(struct jrnl *j) {
  assert(j != NULL);

  /* unlink socket */
  if (unlink(JRNL_SOCKET_PATH) == -1)
    jrnl_logf(j, "couldn't close socket: %s", strerror(errno));

  /* close log */
  if (close(j->log) == -1)
    warn("couldn't close log");
}

/* handler returns an allocated string */
ssize_t jrnl_time_logger(struct jrnl *j __attribute__((unused)), const char *msg, char **out) {
  time_t raw;
  int sbsz;
  char tbuf[26] /* specified in manual */,
    *sbuf;

  /* time */
  if (time(&raw) == -1)
    return -1;
  if (ctime_r(&raw, tbuf) == NULL)
    return -1;

  /* fmt */
  sbsz = snprintf(NULL, 0, "%s: %s\n", tbuf, msg);
  if (sbsz < 0)
    return -1;
  sbuf = calloc(sizeof(char), (size_t)sbsz+1);
  if (sbuf == NULL)
    return -1;
  if (snprintf(sbuf, (size_t)sbsz+1, "%s: %s\n", tbuf, msg) != sbsz) {
    free(sbuf);
    return -1;
  }
  /* result */
  *out = sbuf;
  return (ssize_t)sbsz;
}

enum jrnl_log_level {
  JRNL_LOG_WARN,
  JRNL_LOG_ERR,
  JRNL_LOG_FATAL,
};

/* write string to log */
static void jrnl_log(struct jrnl *j, enum jrnl_log_level lvl, const char *str) {
  char *buf, *prefix = NULL;
  size_t bufsz, psz;
  assert(str != NULL);

  /* generate prefix */
  switch(lvl) {
  case JRNL_LOG_WARN:
    prefix = "warning: ";
    break;
  case JRNL_LOG_ERR:
    prefix = "error: ";
    break;
  case JRNL_LOG_FATAL:
    prefix = "fatal: ";
    break;
  }
  assert(prefix != NULL);

  /* generate buffer */
  psz = strlen(prefix);
  bufsz = psz+strlen(str);
  buf = calloc(sizeof(char), bufsz+1);
  assert(buf != NULL);
  strncpy(buf, prefix, bufsz);
  strncpy(&buf[psz], str, bufsz-psz);

  /* write */
  assert(write(j->log, buf, bufsz) == (ssize_t)bufsz);

  /* clean up */
  free(buf);

  /* fatal */
  if (lvl == JRNL_LOG_FATAL)
    exit(EXIT_FAILURE);
}

__attribute__((__format__(__printf__, 2, 0)))
void jrnl_vlogf(struct jrnl *j, const char *fmt, va_list ap) {
  int bsz;
  ssize_t sbsz;
  char *buf, *sbuf = NULL;

  /* fmt */
  bsz = vsnprintf(NULL, 0, fmt, ap);
  if (bsz == -1)
    jrnl_log(j, JRNL_LOG_FATAL, "couldn't format log entry");
  buf = calloc(sizeof(char), (size_t)bsz+1);
  if (buf == NULL)
    jrnl_log(j, JRNL_LOG_FATAL, "couldn't allocate memory");
  if (vsnprintf(buf, (size_t)bsz+1, fmt, ap) != bsz)
    jrnl_log(j, JRNL_LOG_FATAL, "couldn't format log entry");

  /* logger */
  sbsz = j->logger(j, buf, &sbuf);
  if (sbsz == -1 || sbuf == NULL)
    jrnl_log(j, JRNL_LOG_FATAL, "logger couldn't format entry");

  /* write */
  if (write(j->log, sbuf, (size_t)sbsz) != (ssize_t)sbsz)
    jrnl_log(j, JRNL_LOG_FATAL, "couldn't write to log");

  /* cleanup */
  free(buf);
  free(sbuf);
}

__attribute__((__format__(__printf__, 2, 3)))
void jrnl_logf(struct jrnl *j, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  jrnl_vlogf(j, fmt, ap);
  va_end(ap);
}

void jrnl_listen(struct jrnl *j, int (*handler)(struct jrnl *j, int sock)) {
  struct sockaddr_un pa;
  socklen_t pasz = sizeof(pa);
  int peer;

  /* start listening */
  jrnl_server_init(j);

  /* accept loop */
  for (;;) {
    pid_t child;

    /* accept connection */
    peer = accept(j->sock, (struct sockaddr *) &pa, &pasz);
    if (peer == -1) {
      jrnl_logf(j, "couldn't accept connection: %s", strerror(errno));
    }

    /* fork to worker */
    child = fork();
    if (child == -1) {
      jrnl_logf(j, "couldn't fork worker: %s", strerror(errno));
    } else if (child == 0) {
      handler(j, peer);
    }
  }
}
