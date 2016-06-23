/* Copyright 2016 Connor Taffe */

#ifndef JRNL_H_
#define JRNL_H_

struct jrnl {
  int sock;
};

void jrnl_init(struct jrnl *j);
void jrnl_fini(struct jrnl *j);
__attribute__((noreturn))
void jrnl_listen(struct jrnl *j, int (*handler)(struct jrnl *j, int sock));

#endif /* JRNL_H_ */
