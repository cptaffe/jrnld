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
static void jrnl_socket_init(struct jrnl *j) {
  assert(j != NULL);
  struct sockaddr_un jrnls;
  j->sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (j->sock == -1) {
    jrnl_logf(j, "couldn't create socket: %s", strerror(errno));
  }
  memset(&jrnls, sizeof(jrnls), 0);
  jrnls.sun_family = AF_UNIX;
  strncpy(jrnls.sun_path, "/var/run/jrnl.sock", sizeof(jrnls.sun_path)-1);
  if (bind(j->sock, (struct sockaddr *)&jrnls, sizeof(jrnls)) == -1) {
    jrnl_logf(j, "couldn't bind socket: %s", strerror(errno));
  }
  if (listen(j->sock, 64) == -1) {
    jrnl_logf(j, "couldn't listen on socket: %s", strerror(errno));
  }
}

static void jrnl_log_init(struct jrnl *j) {
  assert(j != NULL);
  j->log = open(JRNL_LOG_PATH, O_CREAT | O_WRONLY);
  if (j->log < 0) {
    err(1, "couldn't open log");
  }
}

void jrnl_init(struct jrnl *j) {
  assert(j != NULL);
  memset(j, sizeof(struct jrnl), 0); /* zero jrnl */
  j->logger = jrnl_time_logger;
  jrnl_log_init(j);
  jrnl_socket_init(j);
}

void jrnl_fini(struct jrnl *j) {
  assert(j != NULL);
  if (unlink(JRNL_SOCKET_PATH) == -1) {
    jrnl_logf(j, "couldn't close socket: %s", strerror(errno));
  }
  if (close(j->log) == -1) {
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

void jrnl_vlogf(struct jrnl *j, char *fmt, va_list ap) {
  int bsz;
  ssize_t sbsz;
  char *buf, *sbuf = NULL;
  /* fmt to buf */
  bsz = vsnprintf(NULL, 0, fmt, ap);
  if (bsz == -1) {
    err(1, "couldn't format log entry");
  }
  buf = calloc(sizeof(char), (size_t)bsz+1);
  if (buf == NULL) {
    err(1, "couldn't allocate memory");
  }
  if (vsnprintf(buf, (size_t)bsz+1, fmt, ap) != bsz) {
    err(1, "couldn't format log entry");
  }
  /* logger formats message */
  sbsz = j->logger(j, buf, &sbuf);
  if (sbsz == -1 || sbuf == NULL) {
    err(1, "logger couldn't format entry");
  }
  free(buf); /* cleanup */
  /* write to log */
  if (write(j->log, sbuf, (size_t)sbsz) != (ssize_t)sbsz) {
    err(1, "couldn't write log entry");
  }
  free(sbuf); /* cleanup */
}

/* jrnl_logf: printf-style formatted logging  */
void jrnl_logf(struct jrnl *j, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  jrnl_vlogf(j, fmt, ap);
  va_end(ap);
}

void jrnl_listen(struct jrnl *j, int (*handler)(struct jrnl *j, int sock)) {
  struct sockaddr_un pa;
  socklen_t pasz = sizeof(pa);
  int peer;
  for (;;) {
    pid_t child;
    peer = accept(j->sock, (struct sockaddr *) &pa, &pasz);
    if (peer == -1) {
      jrnl_logf(j, "couldn't accept on socket: %s", strerror(errno));
    }
    child = fork();
    if (child == -1) {
      jrnl_logf(j, "couldn't fork: %s", strerror(errno));
    } else if (child == 0) {
      handler(j, peer);
    }
  }
}
