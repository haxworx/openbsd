
default:
	-mkdir $(HOME)/bin
	$(CC) -lm *.c -o $(HOME)/Applications/.bin/sensors
	cp tmux.conf $(HOME)/.tmux.conf
	cp cwmrc $(HOME)/.cwmrc
	cp profile $(HOME)/.profile
	cp volctl t web $(HOME)/Applications/.bin
	chmod +x $(HOME)/Applications/.bin/*	
