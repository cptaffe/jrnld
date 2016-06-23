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
#include <signal.h>
#include <syslog.h>
#include <sys/syscall.h>
#include <linux/types.h>
#include <dirent.h>
#include <linux/unistd.h>

#include "jrnl.h"

/* globals */
static struct jrnl *g_jrnl = NULL;

struct jrnld_config {
  bool daemon;
};

__attribute__((noreturn))
void jrnld(struct jrnld_config config);
static void jrnld_signal_handler(int sig);
static int jrnld_listen_handler(struct jrnl *j, int sock);
__attribute__((noreturn))
static void usage(void);
static struct jrnld_config check_args(int argc, char *argv[]);

static void jrnld_signal_handler(int signal) {
  struct jrnl *j;
  j = g_jrnl;
  assert(j != NULL);
  switch (signal) {
  case SIGHUP:
    /* TODO: rehash things */
    syslog(LOG_INFO, "SIGHUP heard");
  case SIGTERM:
    syslog(LOG_CRIT, "SIGTERM heard, terminating...");
    jrnl_fini(j);
    exit(0);
  }
}

static int jrnld_listen_handler(struct jrnl *j __attribute__((unused)), int sock) {
  syslog(LOG_INFO, "handler with %d!", sock);
  return 0;
}

extern char **environ;

/* init code needed by daemon before
   completing startup, and by non-daemon mode */
static void jinit(struct jrnl *j) {
  uint id;
  /* init jrnl before privilege drop */
  openlog("jrnl", 0, LOG_DAEMON);
  jrnl_init(j);

  /* drop privileges */
  id = getuid();
  assert(seteuid(id) != -1);
  assert(setuid(id) != -1);
  id = getgid();
  assert(setegid(id) != -1);
  assert(setgid(id) != -1);

  /* signal handlers */
  signal(SIGHUP, jrnld_signal_handler);
  signal(SIGTERM, jrnld_signal_handler);
}

/* daemonize process */
static void jdaemon(struct jrnl *j) {

  int dir, chan[2], i, pidf, ret;
  struct dirent dent;
  pid_t pid;

  /* close all entries in /proc/self/fd using getdents() */
  dir = open("/proc/self/fd", O_RDONLY | O_DIRECTORY);
  assert(dir != -1);
  /* get first dent, close, repeat */
  while (syscall(SYS_getdents, dir, &dent, sizeof(dent)) > 0)
    assert(close(atoi(dent.d_name)) != -1);

  /* reset signals */
  for (i = 0; i < _NSIG; i++)
    signal(i, SIG_DFL);

  /* reset sigprocmask */
  assert(sigprocmask(SIG_SETMASK, NULL, NULL) != -1);

  /* clear environment */
  for (i = 0; environ[i] != NULL; i++)
    environ[i] = NULL;

  /* create pipe */
  assert(pipe(chan) != -1);

  /* fork, finalize pipe */
  pid = fork();
  assert(pid != -1);
  if (pid != 0) {
    int r;
    /* wait for daemon to initialize */
    assert(close(chan[1]) != -1);
    assert(read(chan[0], &r, sizeof(r)) == sizeof(r));
    assert(close(chan[0]) != -1);
    exit(r);
  }
  assert(close(chan[0]) != -1);

  assert(setsid() != -1);

  /* fork */
  pid = fork();
  assert(pid != -1);
  if (pid != 0)
    close(chan[1]);
    exit(0);

  /* replace in/out/err */
  for (i = 0; i < 3; i++)
    assert(open("/dev/null", O_RDWR) > 0);

  umask(0);
  assert(chdir("/") != -1);

  /* pid file
     TODO(cptaffe): locking */
  pidf = open("/run/jrnl.pid", O_RDWR | O_CREAT | O_TRUNC);
  assert(pidf != -1);
  assert(dprintf(pidf, "%d\n", getpid()) > 0);

  jinit(j); /* drop privileges, syslog, init j */

  /* return success to parent */
  ret = 0;
  assert(write(chan[1], &ret, sizeof(ret)) == sizeof(ret));
  assert(close(chan[1]) != -1);
}

void jrnld(struct jrnld_config config) {
  struct jrnl j;

  /* init */
  if (config.daemon)
    jdaemon(&j); /* returns in daemon process */
  else
    jinit(&j);

  syslog(LOG_INFO, "jrnld has successfully started");

  /* set global jrnl */
  g_jrnl = &j;

  jrnl_listen(&j, jrnld_listen_handler);
}

struct {
  const char *daemon_short,
    *daemon_long;
} flags = {
  "-d",
  "--daemon"
};

static void usage() {
  fprintf(stderr,
    "Usage: jrnld [OPTION]...\n"
    "Start the jrnl daemon and listen for connections\n"
    "from jrnl clients.\n\n"
    "  %s %s  detach and runs as a daemon\n\n"
    "Copyright (c) 2016 Connor Taffe. All rights reserved.\n",
          flags.daemon_short,
          flags.daemon_long);
  exit(1);
}

static struct jrnld_config check_args(int argc, char *argv[]) {
  struct jrnld_config config;
  int i;

  /* parse flags */
  memset(&config, 0, sizeof(config));
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], flags.daemon_short) == 0
        || strcmp(argv[i], flags.daemon_long) == 0) {
      config.daemon = true;
    } else {
      fprintf(stderr, "Invalid flag: '%s'\n", argv[i]);
      usage();
    }
  }

  /* return */
  return config;
}

int main(int argc, char *argv[]) {
  jrnld(check_args(argc, argv));
  return 0;
}
