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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rawline.h"

#if !defined(assert)
#	define assert(cond) do { if(!cond) { fprintf(stderr, "rawline: %s: condition '%s' failed\n", __func__, #cond); abort(); } } while(0)
#endif

/* VT100 control codes used by rawline. It is assumed the code using these printf-style
 * control code formats knows the amount of args (or things to be subbed in), so we don't
 * need to use functions for these. */

/* direction(n) -> move by <n> steps in direction */
#define C_CUR_FORWARD		"\x1b[%dC" /* CUF -- Move forward <n> */
#define C_CUR_BACKWARD		"\x1b[%dD" /* CUB -- Move back <n> */
#define C_CUR_MOVE_COL		"\x1b[%dG" /* CHA -- Move to absolute column <n> */

/* clear() -> clear the line as specified */
#define C_LN_CLEAR_END		"\x1b[0K" /* EL(0) -- Clear from cursor to EOL */
#define C_LN_CLEAR_START	"\x1b[1K" /* EL(1) -- Clear from BOL to cursor */
#define C_LN_CLEAR_LINE		"\x1b[2K" /* EL(2) -- Clear entire line */

/* Internal Error Types */
enum {
	SUCCESS, /* no errors to report */
	SILENT, /* ignorable error */
	BELL /* ring the terminal bell. */
};

/* Static functions only used internally. These functions are never exposed outside of the library,
 * and are not required to be used by external programs. They should never be used by anything outside
 * of this library, because they contain very specific functionality not required for everyday use. */

static void _raw_error(int err) {
	switch(err) {
		case BELL:
			printf("\x7");
			fflush(stdout);
		case SUCCESS:
		case SILENT:
		default:
			break;
	}
} /* _raw_error() */

static void raw_mode_on(raw_t *raw) {
	assert(raw->safe);

	/* get original settings */
	struct termios new;
	new = raw->settings->original;

	/* disable buffered io and echo */
	new.c_lflag &= ~ICANON;
	new.c_lflag &= ~ECHO;

	/* set new settings */
	tcsetattr(0, TCSANOW, &new);
} /* raw_mode_on() */

static void raw_mode_off(raw_t *raw) {
	assert(raw->safe);

	/* get original settings */
	struct termios new;
	new = raw->settings->original;

	/* set original settings */
	tcsetattr(0, TCSANOW, &new);
} /* raw_mode_off() */

static int _raw_del_char(raw_t *raw) {
	assert(raw->safe);

	/* deletion is invalid if there is no input string
	 * or the cursor is past the end of the input */
	if(!raw->line->line->len || raw->line->cursor > raw->line->line->len)
		return BELL;

	/* update len and make shorthand variables */
	raw->line->line->len--;
	int cur = raw->line->cursor, len = raw->line->line->len;

	/* create a copy of the string */
	char *cpy = malloc(len + 1);

	/* delete char */
	memcpy(cpy, raw->line->line->str, cur); /* copy before cursor */
	memcpy(cpy + cur, raw->line->line->str + cur + 1, len - cur); /* copy after cursor */
	cpy[len] = '\0';

	/* copy over copy to correct line */
	raw->line->line->str = realloc(raw->line->line->str, len + 1);
	memcpy(raw->line->line->str, cpy, len + 1);
	free(cpy);

	return SUCCESS;
} /* _raw_del_char() */

static int raw_backspace(raw_t *raw) {
	assert(raw->safe);

	/* backspace is invalid if there is no input
	 * or the cursor is at the start of the string */
	if(!raw->line->line->len || raw->line->cursor < 1)
		return BELL;

	/* move cursor one to the left and do a delete */
	raw->line->cursor--;
	return _raw_del_char(raw);
} /* raw_backspace() */

static int raw_delete(raw_t *raw) {
	assert(raw->safe);

	/* just wrap _raw_del_char, as they both do the same thing */
	return _raw_del_char(raw);
} /* raw_delete() */

static int _raw_add_char(raw_t *raw, char ch) {
	assert(raw->safe);

	/* update len and make shorthand variables */
	raw->line->line->len++;
	int cur = raw->line->cursor, len = raw->line->line->len;

	/* create a copy of the string */
	char *cpy = malloc(len + 1);

	/* add char */
	memcpy(cpy, raw->line->line->str, cur); /* copy before cursor */
	cpy[cur] = ch;

	memcpy(cpy + cur + 1, raw->line->line->str + cur, len - cur - 1); /* copy after cursor */
	cpy[len] = '\0';

	/* copy over copy to correct line */
	raw->line->line->str = realloc(raw->line->line->str, len + 1);
	memcpy(raw->line->line->str, cpy, len + 1);
	free(cpy);

	/* update cursor */
	raw->line->cursor++;
	return SUCCESS;
} /* _raw_add_char() */

static int raw_insert(raw_t *raw, char ch) {
	assert(raw->safe);

	/* just wrap _raw_add_char, as they both do the same thing */
	return _raw_add_char(raw, ch);
} /* raw_insert() */

static int _raw_move_cur(raw_t *raw, int offset) {
	assert(raw->safe);

	int new_cursor = raw->line->cursor + offset;

	/* movement is invalid if cursor position would be before string
	 * or more than one past the end of the string. */
	if(new_cursor < 0 || new_cursor > raw->line->line->len)
		return SILENT;

	raw->line->cursor += offset;
	return SUCCESS;
} /* _raw_move_cur() */

static int raw_left(raw_t *raw) {
	assert(raw->safe);

	return _raw_move_cur(raw, -1);
} /* raw_left() */

static int raw_right(raw_t *raw) {
	assert(raw->safe);

	return _raw_move_cur(raw, 1);
} /* raw_right() */

/* Functions exposed to external use. These functions are the only functions which outside programs
 * will ever need to use. They handle *ALL* memory management, and rawline structures aren't to be
 * allocated by the user and are opaque. */

void raw_init(raw_t *raw, char *(*prompt)(void)) {
	/* set up blank input line */
	raw->line = malloc(sizeof(struct _raw_line));
	raw->line->prompt = malloc(sizeof(struct _raw_str));
	raw->line->line = malloc(sizeof(struct _raw_str));

	raw->line->prompt->str = prompt();

	/* no prompt? set it to "" */
	if(!raw->line->prompt->str)
		raw->line->prompt->str = "";

	raw->line->prompt->len = strlen(raw->line->prompt->str);

	/* malloc the line with "" */
	raw->line->line->str = malloc(1);
	raw->line->line->str[0] = '\0';
	raw->line->line->len = 0;
	raw->line->cursor = 0;

	/* set up standard settings */
	raw->settings = malloc(sizeof(struct _raw_set));
	tcgetattr(0, &raw->settings->original);

	raw->buffer = NULL;
	raw->prompt = prompt;
	raw->safe = true;
} /* raw_init() */

void raw_free(raw_t *raw) {
	assert(raw->safe);

	/* completely clear out line */
	free(raw->line->line->str);
	free(raw->line->line);
	free(raw->line->prompt);
	free(raw->line);

	/* clear out settings */
	free(raw->settings);

	/* clear out everything else */
	free(raw->buffer);
	raw->prompt = NULL;
	raw->safe = false;
} /* raw_free() */

char *raw_input(raw_t *raw) {
	assert(raw->safe);

	/* erase old line */
	raw->line->line->str = realloc(raw->line->line->str, 1);
	raw->line->line->str[0] = '\0';
	raw->line->line->len = 0;
	raw->line->cursor = 0;

	/* get prompt string and print it */
	raw->line->prompt->str = raw->prompt();
	raw->line->prompt->len = strlen(raw->line->prompt->str);
	printf("%s", raw->line->prompt->str);

	/* set up state */
	int enter = false;

	/* enable raw mode */
	raw_mode_on(raw);

	do {
		char ch = getchar();
		int err = SUCCESS, move = false;

		/* simple printable chars */
		if(ch > 31 && ch < 127)
			err = raw_insert(raw, ch);

		switch(ch) {
			case '\n':
				enter = true;
				break;
			case 127:
				/* backspace */
				err = raw_backspace(raw);
				break;
			case 27:
				/* escape sequence */
				{
					char seq[2];
					seq[0] = getchar();
					seq[1] = getchar();

					/* valid start sequence*/
					if(seq[0] == 91) {
						switch(seq[1]) {
							case 68:
								/* left arrow */
								move = true;
								err = raw_left(raw);
								break;
							case 67:
								/* right arrow */
								move = true;
								err = raw_right(raw);
								break;
							case 51:
								/* extended */
								{
									char eseq = getchar();
									if(eseq == 126)
										err = raw_delete(raw);
								}
								break;
							default:
								break;
						}
					}
				}
				break;
		}

		/* was there an error? if so, act on it */
		if(err != SUCCESS) {
			_raw_error(err);
			continue;
		}

		if(!move) {
			printf(C_CUR_MOVE_COL, raw->line->prompt->len + 1);
			printf(C_LN_CLEAR_END);
			printf("%s", raw->line->line->str);
		}

		/* update the cursor position */
		printf(C_CUR_MOVE_COL, raw->line->cursor + raw->line->prompt->len + 1);
		fflush(stdout);
	} while(!enter);

	printf("\n");

	/* disable raw mode */
	raw_mode_off(raw);

	/* copy over input to buffer */
	int len = raw->line->line->len;
	raw->buffer = realloc(raw->buffer, len + 1);
	memcpy(raw->buffer, raw->line->line->str, len);
	raw->buffer[len] = '\0';

	/* return buffer */
	return raw->buffer;
} /* raw_input() */
