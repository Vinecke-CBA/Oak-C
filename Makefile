CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2
SRCS = src/oak.c src/lex.c src/parse.c src/typecheck.c src/alias.c src/codegen.c src/main.c

.PHONY: all clean test

all: oakc

oakc: $(SRCS) src/oak.h
	$(CC) $(CFLAGS) -o oakc $(SRCS)

clean:
	rm -f oakc oakc.exe a.out a.exe *.oak.c
	rm -f examples/*.oak.c
	rm -f hello bump copy_ok control_flow strings bad_twice demo_spin bench
	rm -f hello.exe bump.exe copy_ok.exe control_flow.exe strings.exe bad_twice.exe
	rm -f examples/raylib/demo_spin.exe examples/bench/bench.exe

test: oakc
	./oakc examples/hello.oak -o hello
	./hello
	./oakc examples/bump.oak -o bump
	./bump
	./oakc examples/copy_ok.oak -o copy_ok
	./copy_ok
	./oakc examples/control_flow.oak -o control_flow
	./control_flow
	./oakc examples/strings.oak -o strings
	./strings
	-./oakc examples/bad_twice.oak -o bad_twice
