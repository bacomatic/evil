# evil
Implementation of the [_evil_](http://www1.pacific.edu/~twrensch/evil/index.html) programming language

The evil VM is pretty simple:

Each "cell" contains one byte of data, or 8 bit character (UTF8?).

Three memory pools:

 1. Source code, loaded into memory
 2. Wheel, starts at 1 cell, grows as necessary, circular
 3. Pental, like wheel except fixed at 5 cells

One cell register, named A

Internal state variables:

- mark state - alternate or normal
- Sp - source pointer
- Wp - wheel pointer
- Pp - pental pointer
- P = value stored in Pp
- W = value stored in Wp

Built-in functions:

 swap source and wheel pools

 weave(A) (from the spec):

- Bit 0 is moved to bit 2
- Bit 1 is moved to bit 0
- Bit 2 is moved to bit 4
- Bit 3 is moved to bit 1
- Bit 4 is moved to bit 6
- Bit 5 is moved to bit 3
- Bit 6 is moved to bit 7
- Bit 7 is moved to bit 5

for example, "zaee":
~~~~
	z: A = 0		- 0x00
	a: A++			- 0x01
	e: weave(A)		- 0x04 (0 -> 2)
	e: weave(A)		- 0x10 (2 -> 4)
~~~~

Command reference:

~~~~
	a - A++
	b - jump to last valid marker (using marker state)
	c - insert wheel cell before current cell, Wp = new cell
	d - delete wheel cell at Wp (Wp points at the next cell)
	e - A = weave(A)
	f - jump to next valid market (using marker state)
	g - A = P
	h - Pp++
	i - Wp++
	j - alternate mark character
	k - P = A
	l - swap A and W
	m - standard mark character
	n - Pp--
	o - Wp--
	p - A = W
	q - swap Wp and Sp, swap source pool with wheel pool
	r - read char from stdin, store in A
	s - skip next char if A == 0 (if (A == 0) Sp++)
	t - skip next char if A != 0 (if (A != 0) Sp++)
	u - A--
	v - swap A and P
	w - write A (as char) to stdout
	x - swap mark state (standard to alt & vice versa)
	y - W = A
	z - A = 0
~~~~

Capital letters are reserved for future use.

Command execution begins at Sp = 0  
Invalid characters are skipped  
Valid characters are executed as defined above  
Execution stops when the end of the source pool is reached  
