/* Copyright 2016 Connor Taffe */
#ifndef JRNLD_H_
#define JRNLD_H_

struct jrnld_config {
  bool daemon;
};

extern void
jrnld(struct jrnld_config config)
  __attribute__((noreturn));

#endif /* JRNLD_H_ */
