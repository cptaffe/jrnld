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
#include <syslog.h>

#include "jrnl.h"

static const char *JRNL_SOCKET_PATH = "/var/run/jrnl.sock";

/* set up unix socket server */
static int jrnl_server_init(struct jrnl *j) {
  struct sockaddr_un jrnls;
  assert(j != NULL);

  /* unlink socket if exists (crash event, etc.) */
  unlink(JRNL_SOCKET_PATH);

  /* create socket */
  j->sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (j->sock == -1) {
    syslog(LOG_ERR, "couldn't create socket: %s", strerror(errno));
    return -1;
  }

  /* bind socket */
  memset(&jrnls, 0, sizeof(jrnls));
  jrnls.sun_family = AF_UNIX;
  strncpy(jrnls.sun_path, "/var/run/jrnl.sock", sizeof(jrnls.sun_path)-1);
  if (bind(j->sock, (struct sockaddr *)&jrnls, sizeof(jrnls)) == -1) {
    syslog(LOG_ERR, "couldn't bind socket: %s", strerror(errno));
    return -1;
  }

  /* listen on socket */
  if (listen(j->sock, 64) == -1) {
    syslog(LOG_ERR, "couldn't listen on socket: %s", strerror(errno));
    return -1;
  }
  return 0;
}

void jrnl_init(struct jrnl *j) {
  assert(j != NULL);
  memset(j, 0, sizeof(struct jrnl));
}

void jrnl_fini(struct jrnl *j) {
  assert(j != NULL);

  /* unlink socket */
  if (unlink(JRNL_SOCKET_PATH) == -1)
    syslog(LOG_ERR, "couldn't close socket: %s", strerror(errno));
}

void jrnl_listen(struct jrnl *j, int (*handler)(struct jrnl *j, int sock)) {
  struct sockaddr_un pa;
  socklen_t pasz = sizeof(pa);
  int peer;

  /* start listening */
  assert(jrnl_server_init(j) != -1);

  /* accept loop */
  for (;;) {
    pid_t child;

    /* accept connection */
    peer = accept(j->sock, (struct sockaddr *) &pa, &pasz);
    if (peer == -1) {
      syslog(LOG_ERR, "couldn't accept connection: %s", strerror(errno));
      continue;
    }

    /* fork to worker */
    child = fork();
    if (child == -1) {
      syslog(LOG_ERR, "couldn't fork worker: %s", strerror(errno));
    } else if (child == 0) {
      handler(j, peer);
    }
  }
}
