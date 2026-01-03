PREFIX    = /usr/local
MANPREFIX = ${PREFIX}/share/man

INCS      = -I/usr/include
LIBS      = -L/usr/lib -lX11 -lXrandr

CPPFLAGS  = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS    = -std=c99 -pedantic -Wall -Wextra -Os ${INCS} ${CPPFLAGS}
LDFLAGS   = ${LIBS}

CC        = cc

SRC       = eowm.c
OBJ       = ${SRC:.c=.o}

all: eowm

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

eowm: ${OBJ}
	@echo LD $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f eowm ${OBJ}

install: all
	@echo installing executable to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f eowm ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/eowm

uninstall:
	@echo removing executable from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/eowm

.PHONY: all clean install uninstall
