
proxom.exe: proxom.c
	x86_64-w64-mingw32-gcc -I path/to/SFML/include -o proxom.exe proxom.c -L path/to/SFML/lib/gcc -lcsfml-network -lwsock32