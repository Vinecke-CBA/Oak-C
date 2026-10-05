CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2
SRCS = src/oak.c src/lex.c src/parse.c src/typecheck.c src/alias.c src/codegen.c src/main.c

.PHONY: all clean test test-tcc

all: oakc

oakc: $(SRCS) src/oak.h
	$(CC) $(CFLAGS) -o oakc $(SRCS)

clean:
	rm -f oakc oakc.exe a.out a.exe *.oak.c
	rm -f examples/*.oak.c
	rm -f hello bump copy_ok control_flow strings bad_twice demo_spin bench include_extern for_loops include_dir memory_ptr
	rm -f hello_tcc bump_tcc copy_ok_tcc control_flow_tcc strings_tcc features_tcc include_extern_tcc include_dir_tcc memory_ptr_tcc
	rm -f hello.exe bump.exe copy_ok.exe control_flow.exe strings.exe bad_twice.exe include_extern.exe for_loops.exe include_dir.exe memory_ptr.exe
	rm -f hello_tcc.exe bump_tcc.exe copy_ok_tcc.exe control_flow_tcc.exe strings_tcc.exe features_tcc.exe include_extern_tcc.exe include_dir_tcc.exe memory_ptr_tcc.exe
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
	./oakc examples/include_extern.oak -o include_extern
	./include_extern
	./oakc examples/for_loops.oak -o for_loops
	./for_loops
	./oakc examples/include_dir.oak -o include_dir
	./include_dir
	./oakc examples/memory_ptr.oak -o memory_ptr
	./memory_ptr
	-./oakc examples/bad_twice.oak -o bad_twice

# Same examples through the bundled TinyCC, to check the second backend.
# Needs dependencies/tcc/tcc.exe (see oak.cfg for the cc setting).
test-tcc: oakc
	./oakc examples/hello.oak -o hello_tcc --cc tcc
	./hello_tcc
	./oakc examples/bump.oak -o bump_tcc --cc tcc
	./bump_tcc
	./oakc examples/copy_ok.oak -o copy_ok_tcc --cc tcc
	./copy_ok_tcc
	./oakc examples/control_flow.oak -o control_flow_tcc --cc tcc
	./control_flow_tcc
	./oakc examples/strings.oak -o strings_tcc --cc tcc
	./strings_tcc
	./oakc examples/features.oak -o features_tcc --cc tcc
	./features_tcc
	./oakc examples/include_extern.oak -o include_extern_tcc --cc tcc
	./include_extern_tcc
	./oakc examples/include_dir.oak -o include_dir_tcc --cc tcc
	./include_dir_tcc
	./oakc examples/memory_ptr.oak -o memory_ptr_tcc --cc tcc
	./memory_ptr_tcc
