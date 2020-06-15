PROG = yacc
SRCS = closure.c error.c lalr.c lr0.c main.c mkpar.c output.c reader.c \
	symtab.c verbose.c warshall.c graph.c mstring.c btyaccpar.c

YYPATCH := $(shell cat VERSION)

CPPFLAGS += -DYYBTYACC=1 -DYYPATCH=$(YYPATCH) -DMAXTABLE=INT_MAX
CFLAGS   += -Wwrite-strings

include ../prog.mk

check: $(_PROG)
	YACC=$(_PROG) $(MAKE) -C $(TESTDIR)/bin/yacc

.PHONY: check
