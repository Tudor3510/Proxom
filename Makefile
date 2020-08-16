
proxom.exe: proxom.c icon.res
	x86_64-w64-mingw32-gcc -I path/to/SFML/include -o proxom.exe proxom.c icon.res -L path/to/SFML/lib/gcc -lcsfml-network -lwsock32

icon.res: icon.rc
	windres icon.rc -O coff icon.res