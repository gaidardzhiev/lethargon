# Lethargon

A compiled systems language for ARM 32 bit Linux. No libc, linker or runtime.

The compiler reads `.lt` source and writes a self contained ELF executable directly. It runs on armv8l 32bit Arch Linux. It targets ARMv7 EABI5 and produces correct position dependent executables with no external dependencies at any stage.

The end goal is for `Lethargon` to compile a complete, reproducible version of its own compiler. Not only as a demonstration of cleverness but as a proof that the language and its code generator are correct enough, expressive enough, and complete enough to be trusted as a system tool. Self compilation is the only test that cannot be faked.

## The language

One type: `int`. A variable holding a string literal holds its address. Pointer variables hold addresses of other variables. The compiler knows the difference.

[fact.lt](src/fact.lt):
```c
int fact(int n) {
	if (n == 0) { return 1; }
	return n * fact(n - 1);
}

putint(fact(10));
putstr("\n");
```

[ptr.lt](src/ptr.lt):
```c
int x = 99;
int *p = &x;
putint(*p);
putstr("\n");
*p = 42;
putint(x);
putstr("\n");
```
Compile the compiler:
```sh
make
/usr/sbin/musl-gcc -o lethargon lethargon.c -static -no-pie -g -Wall -Wextra
```

Compile [fact.lt](src/fact.lt) source code to `fact.out` ELF executable:
```sh
./lethargon src/fact.lt -o fact.out
wrote ARM32 ELF to fact.out (4098 bytes, entry 0x101b8)
```

Execute `fact.out`:
```sh
./fact.out
3628800
```

Analyze with strace or use the [elfdump.sh](src/elfdump.sh) POSIX shell script:
```sh
strace ./fact.out
execve("./fact.out", ["./fact.out"], 0xffb9bd20 /* 24 vars */) = 0
write(1, "3628800", 73628800)                  = 7
write(1, "\n", 1
)                       = 1
exit(0)                                 = ?
+++ exited with 0 +++
```

Keywords: `int`, `if`, `else`, `while`, `return`. Everything else is a function call or an operator. `putint`, `putstr`, `getc`, `bload`, `bstore`, and `balloc` are built-in intrinsics. There is no standard library. There is no preprocessor. There is no separate compilation.

Operators: `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`, unary `-`, `&` (address-of), `*` (dereference), `[]` (index).

Pointer operations:

```c
int *p = &x;     /* address of local or global */
*p               /* dereference: load through pointer */
*p = v;          /* store through pointer */
p[i]             /* index: equivalent to *(p + i*4) */
```

Byte and heap operations:

```c
bload(p, i)      /* load byte at p[i] */
bstore(p, i, v)  /* store byte v at p[i] */
balloc(n)        /* bump allocate n bytes, returns pointer, arena lifetime */
```

Conditions are integers. Zero is false. Anything else is true. Exactly as in C.

`else if` chains parse naturally. No special token needed.

What is absent: structs, multiple types, `for`, `break`, `continue`, `switch`, type checking. The language is minimal by design, not by accident. Each addition will be made when it is needed and not before.

## The compiler

[lethargon.c](./lethargon.c) and [lethargon.h](./lethargon.h) compiled with any C89 compatible compiler and `make` build system.

```
make
./lethargon src/fact.lt -o hello.out
./hello.out
```

The compiler is a recursive descent parser feeding directly into an ARM32 code emitter. There is no intermediate representation. No AST transformation passes. No silly optimizations. The parser produces an AST, the code generator walks it once and emits ARM instructions into a buffer, the buffer is written as an ELF file. Three segments: text at `0x10094`, read only data at `0x500000`, BSS at `0x600000`. The entry point is recorded after the runtime helpers are emitted.

Forward calls including recursion are patched in a single pass at the end of codegen. All calling convention logic lives in three functions: `emit_fn_entry`, `emit_fn_exit`, `emit_call`. They do not duplicate each other.

The runtime provides three internal functions: `__itoa`, `__out`, `__putstr`. They are registered in the function table and called via `BL` like any other function.

## Why

ARM 32 bit is the architecture of the device this is written on. The constraint is real, not chosen for aesthetics. A language that runs on the machine in your pocket, compiled by a compiler that fits in two files, with no toolchain beyond a C compiler, is useful in a way that most [modern software](https://harmful.cat-v.org/software/) is not.

The self hosting target is not distant. The language has pointers, array indexing, byte access, and a bump allocator. What remains is structs or a workable substitute to express the compiler's own data structures. When that exists, the compiler can be written in `Lethargon` and the bootstrap chain becomes fully auditable from source to binary on a single device.

## Self hosting

Self hosting means the compiler compiles its own source and produces a binary that can compile that same source again. The output must be bit-identical across generations. That is the only test that cannot be faked and the one this project is working toward.

The bootstrap is built in three stages inside the [stage1/](stage1/) directory. [lex.lt](stage1/lex.lt) implements the lexer: a single `lx_one` function that consumes one token at a time from a byte buffer, identifies keywords via `streqn` against null terminated keyword strings, and returns a 20-byte token allocated from the bump arena. [parse.lt](stage1/parse.lt) contains the full lexer plus a recursive descent parser that produces a heap allocated AST. Each node is a fixed 232 byte block with fields for type, a numeric value, a string pointer, three child slots, a variable length child array capped at 16, and a parameter list capped at 16. No dynamic resizing. [codegen.lt](stage1/codegen.lt) will contain the ARM32 ELF emitter written in `Lethargon`, at which point the three files are concatenated with their test drivers stripped and a single entry point added at the bottom. The resulting [compiler.lt](stage1/compiler.lt) is compiled by the C host to produce `stage1/compiler.out`, which then compiles [compiler.lt](stage1/compiler.lt) itself. That is stage2. If stage2 output matches stage1 output byte for byte, self hosting is complete.

There is no linker and none is needed. `Lethargon` has no separate compilation. The top level of a source file is sequential: globals and functions declared earlier are visible to everything that follows. Concatenation in dependency order is the link step. The only tool required is `cat`.

One constraint shaped the design of all [stage1/](stage1/) code and must be respected in any `Lethargon` source: all local variables must be declared at the top of a function before the first `if` or `while`. The C code generator tracks frame size by accumulating a counter at parse time. A local declared inside a conditional branch inflates that counter for all subsequent returns, including returns that never executed the branch. The stack unwind on those returns over adjusts `sp`, the `POP` loads garbage into `pc`, and the process segfaults. The fix in [lethargon.c](lethargon.c) is one instruction: replace `ADD sp, sp, #frame_sz` in `emit_fn_exit` with `MOV sp, fp`. That fix is deferred until the self hosting milestone is reached, at which point it will be one of the first things the self hosted compiler corrects in itself.

## Status

- [hello.lt](src/hello.lt): working
- [vars.lt](src/vars.lt): working
- [loop.lt](src/loop.lt): working (while loop, global state)
- [fact.lt](src/fact.lt): working (recursive factorial, the correctness baseline)
- [ptr.lt](src/ptr.lt): working (address-of, dereference, store through pointer)
- [arr.lt](src/arr.lt): working (array indexing through pointer, scaled by 4)
- [bytes.lt](src/bytes.lt): working (byte-level read via bload)
- [alloc.lt](src/alloc.lt): working (bump allocator, 64KB arena in BSS)
- [stage1/](stage1/) self hosting in progress...

## License

This project is provided under the [GPL3 License](./COPYING) Copyright (C) 2026 Ivan Gaydardzhiev
