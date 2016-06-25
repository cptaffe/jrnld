/* Copyright 2016 Connor Taffe */

#ifndef JRNL_H_
#define JRNL_H_

struct jrnl {
  int sock;
};

typedef int (*jrnl_connection_handler)(struct jrnl *j, int sock)
    __attribute__((nonnull(1)));
extern void jrnl_init(struct jrnl *j) __attribute__((nonnull(1)));
extern void jrnl_fini(struct jrnl *j) __attribute__((nonnull(1)));
extern void jrnl_listen(struct jrnl *j, jrnl_connection_handler handler)
    __attribute__((noreturn, nonnull(1, 2)));

#endif /* JRNL_H_ */
