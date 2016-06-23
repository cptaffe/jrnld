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

void jrnld(struct jrnld_config config) {
  struct jrnl j;
  size_t i;
  int fds[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};

  /* fork */
  if (config.daemon) {
    pid_t p;
    p = fork();
    assert(p >= 0);
    if (p > 0)
      exit(0);
  }

  /* daemonize */
  umask(0);
  if (config.daemon)
    assert(setsid() != -1);
  assert(chdir("/") != -1);

  /* close standard fds */
  for (i = 0; i < (sizeof(fds)/sizeof(int)); i++)
    assert(close(fds[i]) != -1);

  /* signal handlers */
  signal(SIGHUP, jrnld_signal_handler);
  signal(SIGTERM, jrnld_signal_handler);

  /* open log */
  openlog("jrnl", 0, LOG_DAEMON);

  /* initialize jrnl */
  jrnl_init(&j);
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
