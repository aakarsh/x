x: x.c
	gcc -DDEBUG -Wall  x.c -o x -lm -lncurses
	global -u

