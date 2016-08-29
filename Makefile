
default:
	-mkdir $(HOME)/bin
	$(CC) -lm *.c -o $(HOME)/bin
	cp tmux.conf $(HOME)/.tmux.conf
	cp cwmrc $(HOME)/.cwmrc
	cp profile $(HOME)/.profile
	cp volctl t web $(HOME)/bin
	chmod +x $(HOME)/bin/*	
