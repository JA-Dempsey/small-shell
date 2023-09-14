smallsh:	smallsh.c	replace.c
	gcc -std=c99 -Wall -Werror -pedantic -o smallsh smallsh.c replace.c -I.
