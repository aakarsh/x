x: x.c
	gcc x.c -o x -lm -lncurses
	global -u
	cp x ~/bin
