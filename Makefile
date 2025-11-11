TARGET = eowm
CC ?= cc
CFLAGS ?= -O2 -Wall
PREFIX ?= /usr/local

$(TARGET): src/config.h
	$(CC) $(CFLAGS) src/eowm.c -o $@ -lX11

src/config.h:
	cp src/def.config.h src/config.h

.PHONY: install uninstall clean

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET) src/config.h