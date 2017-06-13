x: x.c
	gcc  -s -Wall  x.c -o x -lm -lncurses
	global -u
	cp x ~/bin
