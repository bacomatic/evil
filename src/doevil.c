/*
	Copyright (c) 2006, David DeHaven
	All rights reserved.
	
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
		*	Redistributions of source code must retain the above copyright 
			notice, this list of conditions and the following disclaimer.
		*	Redistributions in binary form must reproduce the above copyright
			notice, this list of conditions and the following disclaimer in the
			documentation and/or other materials provided with the distribution.
		*	Neither the name of the author nor the names of its
			contributors may be used to endorse or promote products derived from
			this software without specific prior written permission.
	
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/	

/*
	Implementation of the evil programming language
	
	http://www1.pacific.edu/~twrensch/evil/index.html
	
	The evil VM is pretty simple:
	
	Each "cell" contains one byte of data, or 8 bit character (UTF8?).
	
	Three memory pools:
		pool 1 - source code, loaded into memory
		pool 2 - wheel, starts at 1 cell, grows as necessary, circular
		pool 3 - pental, like wheel except fixed at 5 cells
	
	One cell register, named A
	
	Internal state variables:
		mark state - alternate or normal
		Sp - source pointer
		Wp - wheel pointer
		Pp - pental pointer
		P = value stored in Pp
		W = value stored in Wp
	
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
				z: A = 0		- 0x00
				a: A++			- 0x01
				e: weave(A)		- 0x04 (0 -> 2)
				e: weave(A)		- 0x10 (2 -> 4)
	
	Command reference:
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
	
	Capital letters are reserved for future use.
	
	Command execution begins at Sp = 0
	Invalid characters are skipped
	Valid characters are executed as defined above
	Execution stops when the end of the source pool is reached

	${buildcmd}: gcc -o doevil doevil.c
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <termios.h>

/* reduce allocations by allocating in chunks */
#define kWheelChunkSize 128

unsigned char regA = 0;

/* memory pools */
unsigned char *sourcePool = NULL;
unsigned char *wheelPool = NULL;
unsigned char pental[5] = {0,0,0,0,0};

/* 0 = normal mark state, 1 = alternate mark state */
int markerState = 0;
size_t sourceSize = 0;
size_t wheelSize = 0;
size_t wheelHeap = 0;

size_t Sp = 0;
size_t Wp = 0;
int Pp = 0;

struct termios savedTerm;

void exitHandler()
{
	tcsetattr(0, TCSANOW, &savedTerm);
}

/* yeesh, this could be optimized */
void weave()
{
	unsigned char temp, result = 0;
	
	// << 1
		// 1 -> 0
	temp = regA << 1;
	result |= temp & (1<<7);
	
	// << 2
		// 0 -> 2
		// 2 -> 4
		// 4 -> 6
	temp <<= 1;
	result |= temp & ((1<<2) | (1<<4) | (1<<6));
	
	// >> 1
		// 6 -> 7
	temp = regA >> 1;
	result |= temp & (1<<0);
	
	// >> 2
		// 3 -> 1
		// 5 -> 3
		// 7 -> 5
	temp >>= 1;
	result |= temp & ((1<<1) | (1<<3) | (1<<5));
	
//	fprintf(stderr, "w:%02x -> %02x\n", regA, result);
	regA = result;
}

void swapPools()
{
	unsigned char *tpool = sourcePool;
	size_t Tp = Sp;
	
	sourcePool = wheelPool;
	wheelPool = tpool;
	
	Sp = Wp;
	Wp = Tp;
}

/*
	dir = 0 -> backwards
	dir = 1 -> forward
*/
void jumpToMarker(int dir)
{
	/* search forwards/backwards for the desired marker or until Sp hits an end */
	int done = 0;
	char marker = (markerState == 0) ? 'm' : 'j';
	
	while(!done) {
		if(sourcePool[Sp] == marker) break;
		
		if(dir) {
			Sp++;
			if(Sp >= sourceSize) done = 1;
		} else {
			Sp--;
			if(Sp == 0) done = 1;
		}
	}
}

void insertWheelCell()
{
	size_t index;
	
	/* insert cell before the current cell (push cells up), Wp should point at new cell */
	
	/* allocate more space if necessary */
	if(wheelSize >= wheelHeap - 1) {
		wheelHeap += kWheelChunkSize;
		wheelPool = (unsigned char*)realloc(wheelPool, wheelHeap);
	}
	
	/* push old cells up, including the current cell */
	for(index = wheelSize; index > Wp; index--)
		wheelPool[index] = wheelPool[index-1];
	
	/* clear the current cell and increment wheel size */
	wheelPool[Wp] = 0;
	wheelSize++;
}

void deleteWheelCell()
{
	size_t index;
	
	/* enforce minimum wheel size (is a zero size wheel valid???) */
	if(wheelSize == 1) return;
	
	/* just shift higher cells down and decrement wheelSize */
	for(index = Wp; index < wheelSize; index++)
		wheelPool[index] = wheelPool[index+1];
	wheelSize--;
}

void run()
{
	/* allocate/init wheel pool */
	wheelSize = 1;
	wheelHeap = kWheelChunkSize;
	wheelPool = (unsigned char*)malloc(wheelSize);
	wheelPool[0] = 0;
	
	/* clear pental */
	pental[0] = pental[1] = pental[2] = pental[3] = pental[4] = 0;
	
	/* initialize state variables */
	regA = 0;
	Wp = 0;
	Sp = 0;
	Pp = 0;
	
	while(Sp < sourceSize) {
		unsigned char cmd = sourcePool[Sp];
		unsigned char temp;
		
		switch(cmd) {
			case 'a':
				regA++;
				break;
			
			case 'b':
				jumpToMarker(0);
				break;
			
			case 'c':
				insertWheelCell();
				break;
			
			case 'd':
				deleteWheelCell();
				break;
			
			case 'e':
				weave();
				break;
			
			case 'f':
				jumpToMarker(1);
				break;
			
			case 'g':
				regA = pental[Pp];
				break;
			
			case 'h':
				Pp = (Pp + 1) % 5;
				break;
			
			case 'i':
				Wp = (Wp + 1) % wheelSize;
				break;
			
			case 'k':
				pental[Pp] = regA;
				break;
			
			case 'l':
				temp = regA;
				regA = wheelPool[Wp];
				wheelPool[Wp] = temp;
				break;
			
			case 'n':
				if(Pp == 0) Pp = 5;
				Pp--;
				break;
			
			case 'o':
				if(Wp == 0) Wp = wheelSize;
				Wp--;
				break;
			
			case 'p':
				regA = wheelPool[Wp];
				break;
			
			case 'q':
				swapPools();
				break;
			
			case 'r':
				regA = getchar();
				break;
			
			case 's':
				if(regA == 0) Sp++; /* overflow check will be done later */
				break;
			
			case 't':
				if(regA != 0) Sp++;
				break;
			
			case 'u':
				regA--;
				break;
			
			case 'v':
				temp = regA;
				regA = pental[Pp];
				pental[Pp] = temp;
				break;
			
			case 'w':
				putchar((int)regA);
//				fflush(stdout); //  might be necessary for some programs, like the factors sample
				break;
			
			case 'x':
				markerState = !markerState;
				break;
			
			case 'y':
				wheelPool[Wp] = regA;
				break;
			
			case 'z':
				regA = 0;
				break;
			
			case 'j':
			case 'm':
			default:
				break;
		}
		
		Sp++;
	}
	
	if(wheelPool) free(wheelPool);
}

/*
	We take one argument, the program file to load and execute
*/
int main(int argc, char **argv)
{
	struct termios newTerm;
	int rval = 0;
	FILE *fp = NULL;
	
	if(argc != 2) {
		fprintf(stderr, "Usage: %s <evil file>\n", argv[0]);
		return -1;
	}
	
	/* set term settings to unbuffered, no local echo and non-canonicalized input */
	tcgetattr(0, &savedTerm);
	atexit(exitHandler);
	
	tcgetattr(0, &newTerm);
	newTerm.c_lflag &= ~(ECHO | ICANON);
	tcsetattr(0, TCSANOW, &newTerm);
	setvbuf(stdin, NULL, _IONBF, 0);
	
	/* git'er done! */
	fp = fopen(argv[1],"r");
	if(fp) {
		struct stat fps;
		
		if(fstat(fileno(fp), &fps) == 0) {
			sourceSize = (size_t)fps.st_size;
			sourcePool = (unsigned char*)malloc(sourceSize);
			
			if(fread(sourcePool, sourceSize, 1, fp) != 1) {
				fprintf(stderr, "Error reading from file %s\n", argv[1]);
				rval = -1;
			} else {
				run();
			}
		} else {
			fprintf(stderr, "Error 'stat'ing file %s: %d - %s\n", argv[1], errno, strerror(errno));
			rval = -1;
		}
		fclose(fp);
	} else {
		fprintf(stderr, "Unable to open file %s\n", argv[1]);
		rval = -1;
	}
	
	if(sourcePool) free(sourcePool);
	
	return 0;
}
