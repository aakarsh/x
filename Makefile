x: x.cc
	g++  -std=c++11 -DDEBUG -Wno-write-strings  -fpermissive -Wall x.cc -o x -lm -lncurses
	global -u
	cp x ~/bin/x

