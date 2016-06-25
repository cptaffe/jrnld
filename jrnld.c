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
#include <signal.h>

#include "jrnl.h"
#include "jrnld.h"

extern char **environ;

/* constants */
static const char JRNL_ROOT_PATH[] = "/var/jrnl";
static const char JRNL_PID_PATH[] = "/jrnl.pid"; /* relative root */

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

/* globals */
static struct jrnld_gret {
  struct jrnl_daemon *daemon;
  char *msg;
} g_state = {
  NULL,
  NULL
};

/* prototypes */
static void
jrnld_signal_handler(int sig);

static void
jrnld_parent_signal_handler(int sig);

static int
jrnld_listen_handler(struct jrnl *j, int sock)
  __attribute__((nonnull(1)));

static void
jrnld_worker(struct jrnl *j)
  __attribute__((noreturn, nonnull(1)));

static int
jrnl_daemon(void *obj)
  __attribute__((noreturn, nonnull(1)));

static void
jrnld_parent(struct jrnl_daemon *daemon)
  __attribute__((noreturn, nonnull(1)));

static void
jrnld_drop_privileges(void);

static void
jrnld_signal(struct jrnl *j)
  __attribute__((nonnull(1)));

void jrnld_signal_handler(int signal) {
  struct jrnl *j;

  j = g_state.daemon->jrnl;
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

int jrnld_listen_handler(struct jrnl *j __attribute__((unused)), int sock) {
  syslog(LOG_INFO, "handler with %d!", sock);
  return 0;
}

void jrnld_drop_privileges() {
  uid_t uid;
  gid_t gid;

  uid = getuid();
  assert(setuid(uid) != -1);
  gid = getgid();
  assert(setgid(gid) != -1);
}

void jrnld_signal(struct jrnl *j) {
  assert(j != NULL);

  assert(signal(SIGHUP, jrnld_signal_handler) != SIG_ERR);
  assert(signal(SIGTERM, jrnld_signal_handler) != SIG_ERR);
}

/* handles new process */
int jrnl_daemon(void *obj) {
  int i, pidf;
  struct jrnl_daemon *daemon;
  const char buf[] = "Started successfully :)";
  size_t bufsz = sizeof(buf);

  assert(obj != NULL);
  daemon = (struct jrnl_daemon *)obj;

  /* parent kept for printing */
  assert(close(1) != -1);

  /* establish channel */
  assert(close(daemon->chan[0]) != -1);

  /* session/process group leader */
  assert(setsid() != -1);

  umask(0);

  /* replace in/out/err */
  for (i = 0; i < 3; i++)
    assert(open("/dev/null", O_RDWR) != -1);

  /* disable set-uid & root */
  prctl(PR_SET_SECUREBITS, SECBIT_NOROOT_LOCKED, 0, 0, 0);

  /* chroot, only this process can change the file */
  assert(mkdir(JRNL_ROOT_PATH, 0) != -1 || errno == EEXIST);
  assert(chroot(JRNL_ROOT_PATH) != -1);
  assert(chdir("/") != -1);

  /* pid file */
  pidf = open(JRNL_PID_PATH, O_RDWR | O_CREAT | O_TRUNC);
  assert(pidf != -1);
  assert(dprintf(pidf, "%d\n", getpid()) != -1);
  assert(close(pidf) != -1);

  openlog("jrnl", 0, LOG_DAEMON);
  jrnl_init(daemon->jrnl);
  jrnld_drop_privileges();
  jrnld_signal(daemon->jrnl);

  /* signal success */
  assert(write(g_state.daemon->chan[1], &bufsz, sizeof(bufsz)) == (ssize_t)sizeof(bufsz));
  assert(write(g_state.daemon->chan[1], buf, bufsz) == (ssize_t)bufsz);
  assert(close(g_state.daemon->chan[1]) != -1);

  daemon->handler(daemon->jrnl);
}

void jrnld_parent_signal_handler(int sig) {
  switch (sig) {
  case SIGCHLD:
    printf("Jrnld crashed\n");
    exit(1);
  }
}

/* daemonize process */
void jrnld_parent(struct jrnl_daemon *daemon) {
  int dir, i, pid;
  struct dirent dent;
  size_t bufsz;
  uint8_t *buf;

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

  /* store globally */
  g_state.daemon = daemon;

  /* fork
     TODO: clone() with glibc causes numerous issues, try musl or -ffreestanding */
  pid = fork();
  assert(pid != -1);
  if (pid == 0) {
    jrnl_daemon((void *)daemon);
  }

  /* wait for daemon to initialize */
  assert(close(daemon->chan[1]) != -1);

  /* set up signal handlers */
  assert(signal(SIGCHLD, jrnld_parent_signal_handler) != SIG_ERR);

  /* block until signal */
  assert(read(g_state.daemon->chan[0], &bufsz, sizeof(bufsz)) == (ssize_t)sizeof(bufsz));
  buf = calloc(sizeof(uint8_t), bufsz+1);
  assert(buf != NULL);
  assert(read(g_state.daemon->chan[0], buf, bufsz) == (ssize_t)bufsz);
  assert(close(g_state.daemon->chan[0]) != -1);
  printf("Jrnld: %s\n", buf);
  free(buf);
  exit(0);
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

    jrnld_parent(&d); /* returns in daemon process */
  }

  printf("Non-daemon mode deprecated, use -d or --daemon\n");
  exit(1);
}
