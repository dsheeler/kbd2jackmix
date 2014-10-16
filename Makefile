
PROGS = kbd2jackmix

PREFIX=/usr/local

all: $(PROGS)

CC = gcc

LFLAGS = -lm $(shell pkg-config --libs jack)\
         $(shell pkg-config --libs xmms2-client)

CFLAGS = -Wall -c $(shell pkg-config --cflags xmms2-client)

LIBS =

SRCS = kbd2jackmix.c
OBJS = $(SRCS:.c=.o)
HDRS =

.SUFFIXES:

.SUFFIXES: .c

%.o : %.c
	$(CC) ${CFLAGS} $<

kbd2jackmix: ${OBJS}
	${CC} -o $@ ${OBJS} ${LFLAGS}

install: $(PROGS)
	install -m 0755 kbd2jackmix $(PREFIX)/bin
	install -m 0755 kbd2jackmix_launcher.py $(PREFIX)/bin

install-launcher:
	ln -sf $(PREFIX)/bin/kbd2jackmix_launcher.py $(HOME)/.config/xmms2/startup.d

.PHONY: install install-launcher

clean:
	rm -f ${OBJS} $(PROGS:%=%.o)
