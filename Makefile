LIBPAXOS	= libfastpaxos.a
MODULES 	= common lib tests
AR			= ar
ARFLAGS		= rc
QUIET		= @

BDB_DIR		= $(HOME)/bdb/build_unix/
LIBEVENT_DIR= $(HOME)/libevent


.PHONY: $(MODULES) $(BDB_DIR) $(LIBEVENT_DIR)

all: $(LIBPAXOS) $(MODULES)
	@echo "Done."

$(MODULES):	
	$(QUIET) $(MAKE) QUIET=$(QUIET) BDB_DIR=$(BDB_DIR) --directory=$@ || exit 1;
	
$(LIBPAXOS): $(BDB_DIR) $(LIBEVENT_DIR) common lib 
	$(QUIET) $(AR) $(ARFLAGS) $@ $(addsuffix /*.o, $^)
	$(QUIET) ranlib $@
	@echo "Libfastpaxos done."
	
BDB_ERROR_MSG="Warning: Berkeley DB was not found!! BDB is required for the acceptor permanent storage, please download and build BDB and update the BDB_DIR variable in Makefile"
$(BDB_DIR):
	$(QUIET) test -e $(BDB_DIR) || \
	( echo "$(BDB_ERROR_MSG)" && exit 1 )


LE_ERROR_MSG="Please download and build libevent or correct the LIBEVENT_DIR variable in Makefile"
$(LIBEVENT_DIR):
	$(QUIET) if [ ! -e $(LIBEVENT_DIR)/event.h ]; then echo "Error: $(LIBEVENT_DIR)/event.h not found" && echo "$(LE_ERROR_MSG)" && exit 1; fi
	$(QUIET) if [ ! -e $(LIBEVENT_DIR)/libevent.a ]; then echo "Error: $(LIBEVENT_DIR)/libevent.a not found" && echo "$(LE_ERROR_MSG)" && exit 1; fi

clean:
	$(QUIET) rm -f $(LIBPAXOS)
	$(QUIET) for m in $(MODULES); do \
		make clean --directory=$$m; \
	done
	
