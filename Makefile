eowm:
	cc eowm.c -o eowm -lX11

install: eowm
	mkdir -p /usr/local/bin
	install -m 755 eowm /usr/local/bin/eowm

deinstall:
	rm /usr/local/bin/eowm

clean: 
	rm eowm
