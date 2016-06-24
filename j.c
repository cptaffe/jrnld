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
#include <sched.h>
#include <sys/prctl.h>
#include <linux/securebits.h>

#include "jrnl.h"

extern char **environ;

/* constants */
static const char JRNL_ROOT_PATH[] = "/var/jrnl";
static const char JRNL_PID_PATH[] = "/jrnl.pid"; /* relative root */

static const struct {
  const char *daemon_short,
    *daemon_long;
} flags = {
  "-d",
  "--daemon"
};

/* config via flags */
struct jrnld_config {
  bool daemon;
};

/* handler function prototype */
typedef void
(*jrnl_daemon_handler)(struct jrnl *j)
  __attribute__((noreturn, nonnull(1)));

/* daemon information */
struct jrnl_daemon {
  const char *path;
  struct jrnl *jrnl;
  int chan[2];
  jrnl_daemon_handler handler;
};

/* startup return */
enum jrnld_state {
  JRNLD_OK,
  JRNLD_EEXIST /* jrnld already running */
};

/* globals */
static struct jrnl *g_jrnl = NULL;
static struct jrnld_gret {
  enum jrnld_state state;
  int schan;
} g_state = {
  JRNLD_OK,
  -1
};

/* prototypes */
extern void
jrnld(struct jrnld_config config)
  __attribute__((noreturn));

static void
jrnld_signal_handler(int sig);

static int
jrnld_listen_handler(struct jrnl *j, int sock)
  __attribute__((nonnull(1)));

static void
usage(void)
  __attribute__((noreturn));

static struct jrnld_config
check_args(int argc, char *argv[])
  __attribute__((nonnull(2)));

static void
jrnld_worker(struct jrnl *j)
  __attribute__((noreturn, nonnull(1)));

static void
jrnld_init(struct jrnl *j)
  __attribute__((nonnull(1)));

static int
jrnl_daemon(void *obj)
  __attribute__((noreturn, nonnull(1)));

static void
jrnld_daemonize(struct jrnl_daemon *daemon)
  __attribute__((noreturn, nonnull(1)));

void jrnld_signal_handler(int signal) {
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
    break;
  case SIGUSR1:
    /* send a response to the parent */
    assert(g_state.schan != -1);
    assert(write(g_state.schan, &g_state.state, sizeof(g_state.state)) == sizeof(g_state.state));
    assert(close(g_state.schan) != -1);
    g_state.schan = -1;
    break;
  }
}

int jrnld_listen_handler(struct jrnl *j __attribute__((unused)), int sock) {
  syslog(LOG_INFO, "handler with %d!", sock);
  return 0;
}

/* init code needed by daemon before
   completing startup, and by non-daemon mode */
void jrnld_init(struct jrnl *j) {
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

  /* global jrnl, used by handlers */
  g_jrnl = j;

  /* signal handlers */
  assert(signal(SIGHUP, jrnld_signal_handler) != SIG_ERR);
  assert(signal(SIGTERM, jrnld_signal_handler) != SIG_ERR);
  assert(signal(SIGUSR1, jrnld_signal_handler) != SIG_ERR);
}

/* handles new process */
int jrnl_daemon(void *obj) {
  int i, pidf;
  struct jrnl_daemon *daemon;

  assert(obj != NULL);
  daemon = (struct jrnl_daemon *)obj;

  /* parent kept for printing */
  assert(close(1) != -1);

  /* establish channel */
  assert(close(daemon->chan[0]) != -1);
  g_state.schan = daemon->chan[1];

  /* session/process group leader */
  assert(setsid() != -1);

  umask(0);

  /* replace in/out/err */
  for (i = 0; i < 3; i++)
    assert(open("/dev/null", O_RDWR) != -1);

  /* disable set-uid & root */
  prctl(PR_SET_SECUREBITS, SECBIT_NOROOT_LOCKED, 0, 0, 0);

  /* chroot, only this process can change the file */
  assert(mkdir(JRNL_ROOT_PATH, S_ISVTX) != -1 || errno == EEXIST);
  assert(chroot(JRNL_ROOT_PATH) != -1);
  assert(chdir("/") != -1);

  /* pid file */
  pidf = open(JRNL_PID_PATH, O_RDWR | O_CREAT | O_TRUNC);
  assert(pidf != -1);
  assert(dprintf(pidf, "%d\n", getpid()) > 0);
  assert(close(pidf) != -1);

  jrnld_init(daemon->jrnl); /* drop privileges, syslog, init j */

  raise(SIGUSR1); /* success */

  daemon->handler(daemon->jrnl);
}

/* daemonize process */
void jrnld_daemonize(struct jrnl_daemon *daemon) {
  int dir, i, pid;
  struct dirent dent;
  enum jrnld_state ret;

  assert(daemon != NULL);

  /* close all entries in /proc/self/fd using getdents() */
  dir = open("/proc/self/fd", O_RDONLY | O_DIRECTORY);
  assert(dir != -1);
  /* get first dent, close, repeat */
  while (syscall(SYS_getdents, dir, &dent, sizeof(dent)) > 0)
    if (strcmp(dent.d_name, "1") != 0) /* keep stdin */
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
  assert(pipe(daemon->chan) != -1);

  /* fork
     TODO: clone() with glibc causes numerous issues, try musl or -ffreestanding */
  pid = fork();
  assert(pid != -1);
  if (pid == 0) {
    jrnl_daemon((void *)daemon);
  }

  /* wait for daemon to initialize */
  assert(close(daemon->chan[1]) != -1);
  assert(read(daemon->chan[0], &ret, sizeof(ret)) == sizeof(ret));
  assert(close(daemon->chan[0]) != -1);
  switch (ret) {
  case JRNLD_OK:
    printf("Jrnld started correctly\n");
    break;
  default:
    printf("Jrnld encountered an error\n");
    break;
  }
  exit(ret);
}

/* post jrnld init */
void jrnld_worker(struct jrnl *j) {
  syslog(LOG_INFO, "jrnld has successfully started");
  jrnl_listen(j, jrnld_listen_handler);
}

/* start jrnld */
void jrnld(struct jrnld_config config) {
  struct jrnl j;

  if (geteuid() != 0) {
    printf("Jrnld must be run with euid=0, root\n");
    exit(1);
  }

  memset(&j, 0, sizeof(j));
  
  /* init */
  if (config.daemon) {
    struct jrnl_daemon d;

    memset(&d, 0, sizeof(d));

    d.path = JRNL_ROOT_PATH;
    d.jrnl = &j;
    d.handler = jrnld_worker;

    jrnld_daemonize(&d); /* returns in daemon process */
  } else {
    /* non-daemon mode */
    jrnl_init(&j);
    jrnld_worker(&j);
  }
}

void usage() {
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

struct jrnld_config check_args(int argc, char *argv[]) {
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
}
