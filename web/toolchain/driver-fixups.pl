# clang's driver re-raises a child's signal on Unix; WASI has no signals (perl -0pi, see build-wasm.sh)
s/#if LLVM_ON_UNIX\n(  \/\/ On Unix, signals are represented)/#if LLVM_ON_UNIX && !defined(__wasi__)\n$1/;
s/#include <signal.h>\n/#ifndef __wasi__\n#include <signal.h>\n#endif\n/;
