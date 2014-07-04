PROGS = kbd2jackmix

all: $(PROGS)

CC = g++

LFLAGS = -ljack -lpthread -lglib-2.0 \
         $(shell pkg-config --libs gtkmm-3.0)\
				 -lncurses

CFLAGS = -Wall -c -I /usr/include/glib-2.0 \
				 -I /usr/lib/x86_64-linux-gnu/glib-2.0/include \
         -I/usr/include/json-glib-1.0

LIBS =

SRCS = kbd2jackmix.cpp
OBJS = $(SRCS:.cpp=.o)
HDRS =

.SUFFIXES:

.SUFFIXES: .cpp

%.o : %.cpp
	$(CC) ${CFLAGS} $<

kbd2jackmix: ${OBJS}
	${CC} -o $@ ${OBJS} ${LFLAGS}

clean:
	rm -f ${OBJS} $(PROGS:%=%.o)
