DESTDIR ?= /usr/bin

INSTALL ?= install
CC ?= gcc

CFLAGS ?= -O2 -g
override CFLAGS += -Wall -Wundef -fno-common \
			-Werror-implicit-function-declaration

LIBS ~=

OBJS = main.o
override PROG = ubnt-image

ifeq ($(V),1)
  Q =
  NQ = true
else
  Q = @
  NQ = echo
endif

.PHONY: all install clean

all: $(PROG)

%.o: %.c
	@$(NQ) " CC  " $@
	$(Q)$(CC) $(CFLAGS) $(DBG) -c -o $@ $<

$(PROG): $(OBJS) $(HDRS)
	@$(NQ) " LD  " $(PROG)
	$(Q)$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $(PROG)

install: ubntext
	$(NQ) " INST " $(PROG)
	$(Q)$(INSTALL) -m 755 $(PROG) $(DESTDIR)

clean:
	$(Q)rm -f $(PROG) *.o *~
