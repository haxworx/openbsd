
default:
	-mkdir $(HOME)/bin
	$(CC) -lm *.c -o $(HOME)/bin/sensors
	cp tmux.conf $(HOME)/.tmux.conf
	cp cwmrc $(HOME)/.cwmrc
	cp xinitrc $(HOME)/.xinitrc
	cp profile $(HOME)/.profile
