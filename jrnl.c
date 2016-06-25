/* Copyright 2016 Connor Taffe */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

#include "jrnl.h"

/* constants */
static const char *JRNL_SOCKET_PATH = "/jrnl.sock"; /* relative root */

struct jrnl_connection {
  int sock;
  struct jrnl *jrnl;
  jrnl_connection_handler handler;
};

static int jrnl_connection_worker(void *obj)
    __attribute__((noreturn, nonnull(1)));
static void jrnl_server_init(struct jrnl *j) __attribute__((nonnull(1)));

/* set up unix socket server */
void jrnl_server_init(struct jrnl *j) {
  struct sockaddr_un jrnls;
  assert(j != NULL);

  /* unlink socket if exists (crash event, etc.) */
  assert(unlink(JRNL_SOCKET_PATH) != -1 || errno == ENOENT);

  /* create socket */
  j->sock = socket(AF_UNIX, SOCK_STREAM, 0);
  assert(j->sock != -1);

  /* bind socket */
  memset(&jrnls, 0, sizeof(jrnls));
  jrnls.sun_family = AF_UNIX;
  strncpy(jrnls.sun_path, JRNL_SOCKET_PATH, sizeof(jrnls.sun_path) - 1);
  assert(bind(j->sock, (struct sockaddr *)&jrnls, sizeof(jrnls)) != -1);
}

void jrnl_init(struct jrnl *j) {
  assert(j != NULL);
  memset(j, 0, sizeof(struct jrnl));
  jrnl_server_init(j);
}

void jrnl_fini(struct jrnl *j) {
  assert(j != NULL);

  /* unlink socket */
  assert(unlink(JRNL_SOCKET_PATH) != -1);
}

int jrnl_connection_worker(void *obj) {
  struct jrnl_connection *conn;

  assert(obj != NULL);
  conn = (struct jrnl_connection *)obj;
  conn->handler(conn->jrnl, conn->sock);
  exit(0);
}

void jrnl_listen(struct jrnl *j, jrnl_connection_handler handler) {
  struct sockaddr_un pa;
  socklen_t pasz = sizeof(pa);

  assert(j != NULL);
  assert(handler != NULL);

  /* listen on socket */
  assert(listen(j->sock, 64) != -1);

  /* accept loop */
  for (;;) {
    struct jrnl_connection conn;
    int pid;

    memset(&conn, 0, sizeof(conn));
    conn.jrnl = j;
    conn.handler = handler;

    /* accept connection */
    conn.sock = accept(j->sock, (struct sockaddr *)&pa, &pasz);
    if (conn.sock == -1) {
      syslog(LOG_ERR, "couldn't accept connection: %s", strerror(errno));
      continue;
    }

    pid = fork();
    assert(pid != -1);
    if (pid == 0) {
      jrnl_connection_worker((void *)&conn);
    }
  }
}
