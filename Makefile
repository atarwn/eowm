TARGET = eowm
CC ?= cc
CFLAGS ?= -O2 -Wall
PREFIX ?= /usr/local

$(TARGET): config.h
	$(CC) $(CFLAGS) eowm.c -o $@ -lX11

config.h:
	cp def.config.h config.h

.PHONY: install uninstall clean

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)