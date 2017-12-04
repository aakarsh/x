x: x.cc
	g++  -DDEBUG -std=c++11  -Wno-write-strings  -fpermissive -Wall x.cc -o x -lm -lncurses -pthread 
	global -u
	cp x ~/bin/x

