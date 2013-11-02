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

/* Main raw_t structure. */
struct raw_t {
	bool safe; /* has everything been allocated? */

	struct _raw_line *line; /* current line state */
	struct _raw_set *settings; /* settings of line editing */
	struct _raw_term *term; /* terminal state / settings */
	struct _raw_hist *hist; /* history data */
	struct _raw_comp *comp; /* completion data */

	char *atexit; /* the line to return if input is abruptly exited (if NULL, delete current character [if possible] else return current input) */
	char *buffer; /* "output buffer", used to hold latest line to keep all memory management in rawline */
};

/* Create new and free raw_t structures. */
struct raw_t *raw_new(char *);
void raw_free(struct raw_t *);

/* Set history, and add last input (or any arbitrary string) */
int raw_hist(struct raw_t *, bool, int); /* returns a negative int if an error occured */
void raw_hist_add(struct raw_t *);
void raw_hist_add_str(struct raw_t *, char *);
char *raw_hist_get(struct raw_t *);
int raw_hist_set(struct raw_t *, char *); /* returns a negative int if an error occured */

/* Set completion (including callback) */
int raw_comp(struct raw_t *, bool, char **(*callback)(char *), void (*cleanup)(char **)); /* returns a negative int if an error occured */

/* Returns a string taken from input, with emacs-like line editing (using give prompt). */
char *raw_input(struct raw_t *, char*);
#endif
