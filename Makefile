PROGS = kbd2jackmix

all: $(PROGS)

CC = gcc

LFLAGS = -lm $(shell pkg-config --libs jack)

CFLAGS = -Wall -c

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

clean:
	rm -f ${OBJS} $(PROGS:%=%.o)
