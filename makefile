all:
	cc -std=c99 -Wall repl.c mpc/mpc.c -ledit -lm -o repl
