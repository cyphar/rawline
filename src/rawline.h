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

/* Opaque external raw_t structure. */
typedef struct raw_t raw_t;

/* Create new and free raw_t structures. */
raw_t *raw_new(char *);
void raw_free(raw_t *);

/* Set history, and add last input (or any arbitrary string) */
int raw_hist(raw_t *, bool, int); /* returns a negative int if an error occured */
void raw_hist_add(raw_t *);
void raw_hist_add_str(raw_t *, char *);

/* Set completion (including callback) */
int raw_comp(raw_t *, bool, char **(*callback)(char *), void (*cleanup)(char **)); /* returns a negative int if an error occured */

/* Returns a string taken from input, with emacs-like line editing (using give prompt). */
char *raw_input(raw_t *, char*);
#endif
