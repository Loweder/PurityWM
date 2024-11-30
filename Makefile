VERSION = 1.0

CC = gcc

INCS =
LIBS = -lxcb -lxcb-icccm
CPPFLAGS = -DVERSION=\"${VERSION}\" -D_XOPEN_SOURCE=700L -D_DEFAULT_SOURCE
CFLAGS = -std=c99 -pedantic -Wall -O0 ${INCS} ${CPPFLAGS}
LDFLAGS = ${LIBS} -Wl,-export-dynamic

SRC = core.c pwm_api.c
OBJ = $(SRC:.c=.o)

all: options puritywm

options:
	@echo "CC:      '${CC}'"
	@echo "CFLAGS:  '${CFLAGS}'"
	@echo "LDFLAGS: '${LDFLAGS}'"

.c.o:
	${CC} -c ${CFLAGS} $<

puritywm: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f puritywm ${OBJ}

.PHONY: all clean options
