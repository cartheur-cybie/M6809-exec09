## Multicomputer emulator 6x09

An extendable emulator of the 6x09 architecture system where the numeric _09_ represents the architecture family.

### Prerequisites

It is a requisite to have the following packages installed.

`apt install make gcc autoconf automake libreadline-dev libtool`

The optional package `libreadline-dev` is very handy for debugging development post-install. 

### Code configuration to build _exec-09_

While in a terminal at the root directory of this project, generate the configure for your system and generate buildable sources:

```
autoreconf -fi
```

### Makefile tools

When using VS Code, install this extension. Besides providing the Intellisense support, it has a `--dry-run` feature that will aid in preparing the codebase as changes are introduced. Setting this to `--always-make` keeps the generated build objects in a current state.

### Build and install (Debian)

Run:

```
./configure
make
```
Depending on your system, it might be required to run `chmod +x configure`. There is one executable, `m6809-run`. You can install it `make install` or simply reference it explicitly. To see configurable options:

`./configure --help`

Enable readline libraries if they are installed; this will allow use of command-line recall and other shortcuts at the debugger command-line:

```
./configure --enable-readline
make
```

### Clean-up

When finished, clean-up the generated files:

`git clean -fX`

Clean-up the generated directories:

`git clean -fd`

----

### Machines

The emulator now has the notion of different _machines_, which says what types of I/O devices are mapped into the 6809's address space and how they can be accessed.  Adding support for a new machine is fairly easy.

There are five built-in machine types at present:

* `simple` - assumes 64KB of RAM, minus some input/output functions mapped at `$FF00` - see I/O below. If you compile a program with `gcc6809` with no special linker option, you'll get an S-record file that is suitable for running on this machine.  The S-record file will include a vector table at `$FFF0`, with a reset vector that points to a `_start` function, which will call your `main()` function.  When main returns, `_start` writes to an 'exit register' at `$FF01`, which the emulator interprets and causes it to stop.

`gcc6809` also has a built-in notion of which addresses are used for text and data.  The simple machine enforces this and will _fault_ on invalid accesses.

* `wpc` - an emulation of the _Williams Pinball Controller_ which was the impetus for working on the compiler in the first place.

* `eon` and `eon2` is a moniker for _Eight-O-Nine_.  It is similar to `simple` but has some more advanced I/O capabilities, like a larger memory space that can be paged in/out, and a disk emulation for programs that want to have persistence. See eon.c for details.

* `multicomp09` - see miscsbc.c for details
* `smii` - see miscsbc.c for details
* `kipper1` - see miscsbc.c for details
* `coco` - see coco.c for details (under development)

### Command-line options

* Use `-help` to see the command-line options.

### Debugging

The emulator supports interactive debugging, similar to that provided by `gdb`, using the following commands:
```
b <expr>
	Set a breakpoint on EXECUTION at the given address. Eg:
	break 0xf003
	- break next time
	break 0xf000 ignore 4
	- ignore 4 times then break on 5th time
	break 0x1200 if <expr>
	break 0x1200 if $s==02:0x0043
	- break if the S register has the value shown.

wa <expr>
	Set a watchpoint on WRITE to the given address. Eg:
	wa 0xf003
	- break and report on each write to this address.
	wa 0xf003 print
	- on each write to this address report but do not stop
	  execution.
	wa 0xf003 mask 0x10
	- break and report on each write to this address but
	  only if the write data ANDed with the mask value is
	  non-zero.

rwa <expr>
	Set a watchpoint on READ from the given address. See
	examples above for 'wa'.

awa <expr>
	Set a watchpoint on ACCESS (read or write) at the given
	address. See examples above for 'wa'.

bl
	List all breakpoints/watchpoints.

d <num>
	Delete a breakpoint/watchpoint.

c
	Continue execution.

di <expr>
	Add a display expression. The value of the expression
	is displayed any time that the CPU breaks. Eg:
	di $d $x $y
	- print current value of D X and Y registers each time
	  the CPU breaks.

dump
	Save machine-specific state information to a file,
	typically named <machine>.dmp. The dump might be
	in readable or in binary format.

dumpi <1 | 0>
	Turn instruction dump on or off. With no argument, report
	the current instruction dump state (on or off).
	With instruction dump off, the 'c', 'n', 's' commands
        will report the last instruction that was executed before
	control returned to the prompt. With instruction dump on,
	those commands report each instruction as it is executed.
	Instruction dump is OFF by default.

h or ?
	Display help.

l <expr>
	List (disassemble) CPU instructions. Default is to start
	at the current value of the PC.

me <expr>
	Measure the amount of time that a function named by
	<expr> takes.

n
	Continue execution until the Next instruction is reached.
	If the current instruction is a call, then the debugger
	resumes after control returns.

p [format] <expr>
	Print the value of an expression. See 'Expressions'
	below. Eg:
	p/f <expr>
	- f specifies format. See below.
	p <expr>
	- use the previous format.

	f is the display format (default x for hex). Options:
	  x X d u o a c s (match printf).

        TODO: the code supports /u (unit) as well, but the b w
        options don't make any obvious difference to the output.

pc <expr>
	Set the CPU program counter to <expr> and list
	(disassemble) CPU instructions at that address.

q
	Quit the emulator.

regs
	Display the current value of the CPU registers.


re
	Reset the CPU/machine.

restore
	Restore machine state from a 'dump' file. NOT CURRENTLY
	IMPLEMENTED.

runfor <number> [units]
	Continue but break after a certain period of (simulated)
	time. The time can be expressed in ms (default) or s.
	The 'runfor' command cannot be nested; issuing a
        'runfor' cancels any 'runfor that is in progress. Eg:
	runfor 100 ms
	runfor 1 s
	runfor 0      -- give 'bad time value' error, but still
	                 cancels a 'runfor' in progress.

s  <expr>
	Step for a certain number of CPU instructions (1 by
	default).

set <expr>
set var <expr>
	Sets the value of a location in target memory or the
	value of an internal variable.
	See 'Expressions' below for details on the syntax.

so <file>
	Run a set of debugger commands from another file.
	The commands may start/stop the CPU.  When the commands
	are finished, control returns to the previous input
	source (the file that called it, or the keyboard.)

sym <file>
	Load a symbol table file named file.map -- Currently,
	the only format	supported is an lwlink map file.

td
	Trace Dump. Display the last 256 instructions that were
	executed.

vars
	Show all program variables.

serrx <byte> [byte...]
	Inject one or more bytes into the emulated serial RX queue.
	Useful for monitor-driven protocol testing (for example, iCybie
	serial command streams). Byte values may be decimal or hex:
	serrx 0x55 0xAA 3 4

serstat
	Show serial monitor state (pending RX bytes and TX trace mode).

sertx [on|off|1|0]
	Enable or disable monitor-side logging of serial TX bytes.
	With no argument, reports current state.

x [format] <expr>
	Examine target memory at the address  <expr>. Eg:
	x/nfu addr
	- nfu specifies number, format, unit respectively. See
          below.
	x/nfu
	- continue from previous address, in new format.
	x addr
	- use previous format, continue from previous address.
	x
	- use previous format, continue from previous address.

	n is the repeat count (default 1). It specifies how much
	  memory (counting by units u to display. Display is
	  formated multi-line if necessary.
	f is the display format (default x for hex). Options:
	  x X d u o a s (match printf) and i (instructions).
	u is the unit size (default b for byte). Options:
	  b (byte) w (word: 2 bytes)

	The addr can be specified as an expression. Eg:
	x $pc
	x $pc+8

	- see 'Expressions' below.

info
        Describe machine, devices and address mapping.
```

### Symbol Tables

Exec09 maintains variables, in three separate symbol tables:

- The program table. This is loaded automatically at startup or   using the `sym` command. The contents of this table is displayed by the `vars` command. Entries in the program table are annotated onto `list`, `step` and `x` output.

* The auto table. The variables in this table are pre-defined. The following variables refer to CPU registers:
	* `pc a y u s d a b dp cc`
* The following variables refer to emulator state:
	* `cycles` - number of cycles since reset.
	* `et` - number of cycles elapsed since `et` was last inspected.
	* `irqload` - the average number of cycles spent in IRQ.
* The internal table. Variables are added to this table  using the `set var` command. The three groups of variables behave in different ways.

Entries in the program table:

- converted to absolute address values when they are loaded.
- cannot be modified.
- new entries cannot be created interactively
- return either an address (their value) or the data at the addressed location, depending upon the context.

For example, consider a variable `start` referring to address `0x200`. The byte at address `0x200` is `0xab`.
```
set start=5           -- changes the byte at address 0x200 from 0xab to 0x05
print start           -- displays byte from location 0x200
print &start          -- displays 0x200
break start           -- breakpoint at 0x200
list  start           -- list at 0x200
examine start         -- read/display memory at 0x200
```
Entries in the auto table:

- can be modified
- new entries cannot be created interactively
- a print always returns the value
- a set always changes the value (no memory read/write)

Entries in the internal table:

- can be created interactively using `set var`
- can be modified
- are not converted to absolute address values
- return either an address (their value) or the data at the addressed location, depending upon the context.

For example, consider the creation of a variable `aeon` from a pre-existing program variable `start`:

`set var aeon=&start+2`

`aeon` can now be used in exactly the same way as the examples above showed for `start`.

### Expressions

Many of the debug commands accept an expression. An expression is one or more numbers or variables, combined using operators, like `$pc+8`.

Depending on the context, an expression might be an `rvalue` or might be of the form `lvalue=rvalue`. In both cases, no white-space is allowed: the expression is silently truncated at the white-space, so that `$pc + 8` is treated the same way as `$pc`.

The `print` command evalutes and displays the value of an `rvalue` and simultaneously creates an auto-table entry of the form `$<num>` where `<num>` increments for each successive command.
```
$1 = 0x12
(dbg) print 2 + 4
$2 = 0x02
(dbg) print $1
$3 = 0x12
```
The values of these auto-table entries (`$1`, `$2`, `$3`, etc.) are static.
```
(dbg) print $pc
$4 = 0xE012
(dbg) step
02:0x0013 6EB1                  JMP   [,Y++]
(dbg) print $pc
$5 = 0xE013
(dbg) print $4
$6 = 0xE012
```
In this example, the `print $4` returned the original value calculated, not the value resulting from the update of `pc`.

There are also two short-hand forms for referring to `$<num>` values:

* `$`   refers to the most recent entry
* `$$n` refers to the value from n entries ago.
* `$`   is equivalent to `$$0`.

As:
```
(dbg) print 5
$0 = 0x05
(dbg) print 6
$1 = 0x06
(dbg) print 7
$2 = 0x07
(dbg) print 8
$3 = 0x08
(dbg) print $$2
$4 = 0x06
(dbg) print $
$5 = 0x06
```
The `print` command also accepts an expression of the form `lvalue=rvalue` in which case it performs a memory store - like `set` - but also displays the value written.
```
(dbg) print 0=0xab
$60 = 0x00ab
(dbg) x/4X 0
01:0x0000                    : 0xAB 0xE5 0x1D 0x5A
```
The `set` command can change memory and can create/change entries in the internal symbol table; e.g.:
```
(dbg) x/4 0
01:0x0000                    : 0x00 0x00 0x00 0x00
(dbg) set 0=0x12
(dbg) set 3=0x1b
(dbg) x 0
01:0x0000                    : 0x12 0x00 0x00 0x1B
(dbg) set var aeon=0x10000001
(dbg) x 0
01:0x0000                    : 0x12 0x00 0x00 0x1B
(dbg) print aeon
$0 = 0x00
(dbg) set aeon=0xab
(dbg) x 0
01:0x0000                    : 0x12 0xAB 0x00 0x1B
(dbg) print &aeon
$1 = 0x10000001
```

If the `lvalue` is numeric, it is treated as an address and the result is a byte write to target memory. If the `lvalue`
is a variable prefixed with `&` the result is an update to the variable's value.

Numbers are implicitly decimal. A leading `0` indicates an octal number and a leading `0x` indicates a hex number.

The following operators are supported:

`unary +`, `unary -`, `+`, `-`, `*`, `/ == != &`

Evaluation of expressions follows the usual rule that `*`, `/` bind more tightly than `+`, `-` so that `4+3*7` evaluates to the same result as `3*7+4`.

The result of `==`, `!=`  is `1` (true) or `0` (false).

The `&` is an indirection operator. `&4` is the contents of address `4` in the target machine; however, this is of limited use because only byte access is supported.

The following expression errors are detected and reported:

- bad operator in expression
- bad `lvalue`
- bad `rvalue`
- non-existent symbol
- non-existent auto symbol
- bad numeric literal
- unrecognised `$symbol` in assignment
- missing assignment

However, the error-checking is not rigorous. In particular:

- an expression is silently truncated at first white-space - an unrecognised operator is silently ignored - set `3+1=9` does a write to location `4`, but you cannot use arithmetic on the `LHS` with variables.
- an error in an assignment is reported but the assignment   may have happened; e.g., to the wrong location or with the wrong value.
