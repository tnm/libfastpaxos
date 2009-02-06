BDB_DIR		= $(HOME)/bdb/build_unix/
LIBEVENT_DIR= $(HOME)/libevent/


OBJS		= $(subst .c,.o,$(SRCS))
CC			= gcc
CFLAGS 		= -O2 -Wall -g -Wshadow 
CPPFLAGS	= -I../include/ -I../ -I$(BDB_DIR) -I$(LIBEVENT_DIR)
ARFLAGS		= rc
DEPS 		= $(patsubst %.c,.deps/%.d,$(SRCS))

QUIET		= @


all: $(OBJS) $(LIBS) $(PROGRAMS) aux
	@echo Done.

-include $(DEPS)


%.a:
	$(QUIET) $(AR) $(ARFLAGS) $@ $^
	$(QUIET) ranlib $@

%: %.o
	$(QUIET) $(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(QUIET) test -d .deps || mkdir .deps;
	$(QUIET) gcc $(CPPFLAGS) $(CFLAGS) $< -MT $@ -MM -MP -MF .deps/$*.d
	$(QUIET) $(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	$(QUIET) rm -f $(PROGRAMS) $(LIBS) $(OBJS) $(AUX_FILES)
	$(QUIET) rm -drf  .deps
	
.PHONY: clean $(AUX_ALL) aux
