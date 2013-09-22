/* rawline: A small line editing library
 * Copyright (c) 2013 Cyphar
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be included in
 *    all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __RAWLINE_H__
#define __RAWLINE_H__

#include <termios.h>

/* Define bools. */
#if !defined(bool)
#	define bool int
#	define true 1
#	define false 0
#endif

/* Structures used both externally and internally by rawline. External structures end with _t
 * and are typedef'd. Internal structures begin with a single '_' and aren't *ever* typedef'd. */

struct _raw_str {
	char *str; /* string representation */
	int len; /* length of string (no more strlen!) */
};

struct _raw_line {
	struct _raw_str *prompt; /* prompt "string" */
	struct _raw_str *line; /* input line */
	int cursor; /* cursor position in line (relative to end of prompt) */
};

struct _raw_set {
	struct termios original; /* original terminal settings */
};

typedef struct raw_t {
	bool safe; /* has everything been allocated? */

	struct _raw_line *line; /* current line state */
	struct _raw_set *settings; /* settings of line editing */

	char *(*prompt)(void); /* function which returns null-terminated prompt string */
	char *buffer; /* "output buffer", used to hold latest line to keep all memory management in rawline */
} raw_t;

/* initialise and free the raw_t structures*/
void raw_init(raw_t *, char *(*)(void));
void raw_free(raw_t *);

/* returns a string taken from input, with emacs-like line editing */
char *raw_input(raw_t *);
#endif
