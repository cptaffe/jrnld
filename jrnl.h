/* Copyright 2016 Connor Taffe */
#ifndef JRNL_H_
#define JRNL_H_

struct jrnl {
  int log; /* log fd */
  ssize_t (*logger)(struct jrnl *j, const char *msg, char **out);
};

void jrnl_init(struct jrnl *j);
void jrnl_fini(struct jrnl *j);
void jrnl_logf(struct jrnl *j, char *fmt, ...);
void jrnl_listen(struct jrnl *j, int (*handler)(struct jrnl *j, int sock));
ssize_t jrnl_time_logger(struct jrnl *j, const char *msg, char **out);
#endif // JRNL_H_
