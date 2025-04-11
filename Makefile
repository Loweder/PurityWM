VERSION = 1.0

CC = gcc

INCS =
LIBS = -lxcb -lxcb-icccm
CPPFLAGS = -DVERSION=\"${VERSION}\" -D_XOPEN_SOURCE=700L -D_DEFAULT_SOURCE
CFLAGS = -std=c99 -pedantic -Wall -O0 ${INCS} ${CPPFLAGS}
LDFLAGS = ${LIBS} -Wl,-export-dynamic

SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,build/%.o,${SRC})

puritywm: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

build/%.o: src/%.c
	${CC} -c ${CFLAGS} -o $@ $<

clean:
	rm -f puritywm ${OBJ}

.PHONY: clean
