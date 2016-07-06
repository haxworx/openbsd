default:
	$(CC) -lm *.c -o ../bin/sensors
	cp tmux.conf ../.tmux.conf
	cp cwmrc ../.cwmrc
