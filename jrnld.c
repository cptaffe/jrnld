/* Copyright 2016 Connor Taffe */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "daemon.h"

static const struct {
  const char *daemon_short, *daemon_long;
} flags = {"-d", "--daemon"};

static void usage(void) __attribute__((noreturn));

static struct jrnld_config check_args(int argc, char *argv[])
    __attribute__((nonnull(2)));

void usage() {
  fprintf(stderr, "Usage: jrnld [OPTION]...\n"
                  "Start the jrnl daemon and listen for connections\n"
                  "from jrnl clients.\n\n"
                  "  %s %s  detach and runs as a daemon\n\n"
                  "Copyright (c) 2016 Connor Taffe. All rights reserved.\n",
          flags.daemon_short, flags.daemon_long);
  exit(1);
}

struct jrnld_config check_args(int argc, char *argv[]) {
  struct jrnld_config config;
  int i;

  /* parse flags */
  memset(&config, 0, sizeof(config));
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], flags.daemon_short) == 0 ||
        strcmp(argv[i], flags.daemon_long) == 0) {
      /* flags no longer supported */
      fprintf(stderr, "Flag [ %s | %s ] no longer supported\n",
              flags.daemon_short, flags.daemon_long);
    } else {
      fprintf(stderr, "Invalid flag: '%s'\n", argv[i]);
      usage();
    }
  }

  /* return */
  return config;
}

int main(int argc, char *argv[]) { jrnld(check_args(argc, argv)); }
