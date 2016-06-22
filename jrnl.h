/* Copyright 2016 Connor Taffe */
#ifndef JRNL_H_
#define JRNL_H_

struct jrnl {
  int log; /* log fd */
};

void jrnl_init(struct jrnl *j);
void jrnl_fini(struct jrnl *j);
void jrnl_logf(struct jrnl *j, char *fmt, ...);

#endif // JRNL_H_
