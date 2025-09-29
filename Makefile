TARGET = eowm
CC ?= cc
CFLAGS ?= -O2 -Wall
PREFIX ?= /usr/local

$(TARGET):
	$(CC) $(CFLAGS) eowm.c -o $@ -lX11

.PHONY: install uninstall clean

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)