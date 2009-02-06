SRCS 		= leader_main.c learner_main.c proposer_main.c acceptor_main.c

PROGRAMS	= $(subst .c,,$(SRCS))

AUX_FILES = *.txt *.bdb out/*.log

SNAME		:= $(shell uname)
ifeq ($(strip $(SNAME)),Linux)
LDFLAGS		= -L../ -L$(LIBEVENT_DIR) -lfastpaxos -lpthread -levent -lrt
else
LDFLAGS		= -L../ -L$(LIBEVENT_DIR) -lfastpaxos -lpthread -levent
endif

include ../Makefile.inc
