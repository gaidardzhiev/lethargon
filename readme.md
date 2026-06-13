# Lethargon

A compiled systems language for ARM 32 bit Linux. No libc, linker or runtime.

The compiler reads `.lt` source and writes a self contained ELF executable directly. It runs on armv8l 32bit Arch Linux. It targets ARMv7 EABI5 and produces correct position dependent executables with no external dependencies at any stage.

The end goal is for Lethargon to compile a complete, reproducible version of its own compiler. Not only as a demonstration of cleverness but as a proof that the language and its code generator are correct enough, expressive enough, and complete enough to be trusted as a system tool. Self compilation is the only test that cannot be faked.

## The language

One type: `int`. A variable holding a string literal holds its address. Pointer variables hold addresses of other variables. The compiler knows the difference.

[fact.lt](src/fact.lt):
```c
int fact(int n) {
	if (n == 0) { return 1; }
	return n * fact(n - 1);
}

putint(fact(10));
putint("\n");
```

[ptr.lt](src/ptr.lt):
```c
int x = 99;
int *p = &x;
putint(*p);
putint("\n");
*p = 42;
putint(x);
putint("\n");
```
Compile the compiler:
```sh
make
/usr/sbin/musl-gcc -o lethargon lethargon.c -static -no-pie -g -Wall -Wextra
```

Compile [fact](src/fact.lt) to `fact.out` ELF executable:
```sh
./lethargon src/fact.lt -o fact.out
wrote ARM32 ELF to fact.out (4098 bytes, entry 0x101b8)
```

Execute `fact.out`:
```sh
./fact.out
3628800
```

Analyze with strace:
```sh
strace ./fact.out
execve("./fact.out", ["./fact.out"], 0xffb9bd20 /* 24 vars */) = 0
write(1, "3628800", 73628800)                  = 7
write(1, "\n", 1
)                       = 1
exit(0)                                 = ?
+++ exited with 0 +++
```

Keywords: `int`, `if`, `else`, `while`, `return`. Everything else is a function call or an operator. `putint` and `getc` are the two built-in I/O intrinsics. There is no standard library. There is no preprocessor. There is no separate compilation.

Operators: `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`, unary `-`, `&` (address-of), `*` (dereference), `[]` (index).

Pointer operations:

```c
int *p = &x;     /* address of local or global */
*p               /* dereference: load through pointer */
*p = v;          /* store through pointer */
p[i]             /* index: equivalent to *(p + i*4) */
```

Conditions are integers. Zero is false. Anything else is true. Exactly as in C.

`else if` chains parse naturally. No special token needed.

What is absent: structs, multiple types, `for`, `break`, `continue`, `switch`, a heap allocator, byte-level access, type checking, optimization. The language is minimal by design, not by accident. Each addition will be made when it is needed and not before.

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

The self hosting target is not distant. The language has pointers and array indexing. What remains is a static allocator and byte-level access. When those exist, the compiler can be written in Lethargon and the bootstrap chain becomes fully auditable from source to binary on a single device.

## Status

- [hello.lt](src/hello.lt): working
- [vars.lt](src/vars.lt): working
- [loop.lt](src/loop.lt): working (while loop, global state)
- [fact.lt](src/fact.lt): working (recursive factorial, the correctness baseline)
- [ptr.lt](src/ptr.lt): working (address-of, dereference, store through pointer)
- [arr.lt](src/arr.lt): working (array indexing through pointer, scaled by 4)
- self hosting: in progress...

## License

This project is provided under the [GPL3 License](./COPYING) Copyright (C) 2026 Ivan Gaydardzhiev
