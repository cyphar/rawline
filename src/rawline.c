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
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include "rawline.h"

#if !defined(assert)
#	define assert(cond) do { if(!cond) { fprintf(stderr, "rawline: %s: condition '%s' failed\n", __func__, #cond); abort(); } } while(0)
#endif

/* VT100 control codes used by rawline. It is assumed the code using these printf-style
 * control code formats knows the amount of args (or things to be subbed in), so we don't
 * need to use functions for these. */

#define C_BELL				"\x7"

/* direction(n) -> move by <n> steps in direction */
#define C_CUR_FORWARD		"\x1b[%dC" /* CUF -- Move forward <n> */
#define C_CUR_BACKWARD		"\x1b[%dD" /* CUB -- Move back <n> */
#define C_CUR_MOVE_COL		"\x1b[%dG" /* CHA -- Move to absolute column <n> */

/* clear() -> clear the line as specified */
#define C_LN_CLEAR_END		"\x1b[0K" /* EL(0) -- Clear from cursor to EOL */
#define C_LN_CLEAR_START	"\x1b[1K" /* EL(1) -- Clear from BOL to cursor */
#define C_LN_CLEAR_LINE		"\x1b[2K" /* EL(2) -- Clear entire line */

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

struct _raw_term {
	int fd; /* terminal file descriptor */
	bool mode; /* is the terminal in raw mode? */
	struct termios original; /* original terminal settings */
};

struct _raw_set {
	/* ... */
};

typedef struct raw_t {
	bool safe; /* has everything been allocated? */

	struct _raw_line *line; /* current line state */
	struct _raw_set *settings; /* settings of line editing */
	struct _raw_term *term; /* terminal state / settings */

	char *atexit; /* the line to return if input is abruptly exited (if NULL, delete current character [if possible] else return current input) */
	char *buffer; /* "output buffer", used to hold latest line to keep all memory management in rawline */
} raw_t;

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
			printf(C_BELL);
			fflush(stdout);
		case SUCCESS:
		case SILENT:
		default:
			break;
	}
} /* _raw_error() */

/* Raw mode is a mode where the terminal will give EVERY character with 0 timeout, no buffering and no
 * console output. It also disables signal characters, the conversion of characters or output control.
 * Essentially, undo all of the hard work of terminal developers and send the terminal back in time,
 * to the 1960s. ;) */

static void _raw_mode(raw_t *raw, bool state) {
	assert(raw->safe);

	/* get original settings */
	struct termios new;
	new = raw->term->original;

	if(state) {
		/* input modes: disable(break | CR to NL | parity | strip | control) */
		new.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

		/* output modes: disable(post processing) */
		new.c_oflag &= ~OPOST;

		/* control modes: enable(8bit chars) */
		new.c_cflag |= CS8;

		/* local modes: disable(echoing | buffered io | extended functions | signals) */
		new.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

		/* control chars: ensure that we get *every* byte, with no timeout. waiting is for suckers. */
		new.c_cc[VMIN] = 1; /* one char only */
		new.c_cc[VTIME] = 0; /* don't wait */
	}

	/* set new settings and flush out terminal */
	tcsetattr(0, TCSAFLUSH, &new);
	raw->term->mode = state;
} /* _raw_mode() */

static int _raw_del_char(raw_t *raw) {
	assert(raw->safe);

	/* deletion is invalid if there is no input string
	 * or the cursor is past the end of the input */
	if(!raw->line->line->len || raw->line->cursor >= raw->line->line->len)
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

	if(raw->line->cursor > raw->line->line->len)
		raw->line->cursor = raw->line->line->len;

	return SUCCESS;
} /* _raw_del_char() */

static int _raw_backspace(raw_t *raw) {
	assert(raw->safe);

	/* backspace is invalid if there is no input
	 * or the cursor is at the start of the string */
	if(!raw->line->line->len || raw->line->cursor < 1)
		return BELL;

	/* move cursor one to the left and do a delete */
	raw->line->cursor--;
	return _raw_del_char(raw);
} /* _raw_backspace() */

#define _raw_delete(raw) _raw_del_char(raw)

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

#define _raw_insert(raw, ch) _raw_add_char(raw, ch)

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

#define _raw_left(raw) _raw_move_cur(raw, -1)
#define _raw_right(raw) _raw_move_cur(raw, 1)

void _raw_redraw(raw_t *raw, bool change) {
	assert(raw->safe);

	/* redraw input string */
	if(change) {
		printf(C_CUR_MOVE_COL, raw->line->prompt->len + 1);
		printf(C_LN_CLEAR_END);
		printf("%s", raw->line->line->str);
	}

	/* update the cursor position */
	printf(C_CUR_MOVE_COL, raw->line->cursor + raw->line->prompt->len + 1);
	fflush(stdout);
} /* _raw_redraw() */

void _raw_set_buffer(raw_t *raw, char *str) {
	int len = strlen(str);

	raw->buffer = realloc(raw->buffer, len + 1);
	memcpy(raw->buffer, str, len);

	raw->buffer[len] = '\0';
} /* _raw_set_buffer() */

#define _raw_update_buffer(raw) _raw_set_buffer(raw, raw->line->line->str)

char *_raw_strdup(char *str) {
	if(!str)
		return NULL;

	int len = strlen(str);

	char *ret = malloc(len + 1);
	memcpy(ret, str, len);

	ret[len] = '\0';
	return ret;
} /* _raw_strdup() */

void _raw_set_line(raw_t *raw, char *str, int cursor) {
	assert(raw->safe);

	int len = strlen(str);

	/* copy over the string to line */
	raw->line->line->str = realloc(raw->line->line->str, len + 1);
	memcpy(raw->line->line->str, str, len);
	raw->line->line->str[len] = '\0';

	/* update len and cursor */
	raw->line->line->len = len;
	raw->line->cursor = cursor;

	/* if the given cursor position is illogical, move it to start */
	if(raw->line->cursor < 0 || raw->line->cursor > len)
		raw->line->cursor = 0;
} /* _raw_set_line() */

/* Functions exposed to external use. These functions are the only functions which outside programs
 * will ever need to use. They handle *ALL* memory management, and rawline structures aren't to be
 * allocated by the user and are opaque. */

raw_t *raw_new(char *atexit) {
	/* alloc main structure */
	raw_t *raw = malloc(sizeof(raw_t));

	/* set up blank input line */
	raw->line = malloc(sizeof(struct _raw_line));
	raw->line->prompt = malloc(sizeof(struct _raw_str));
	raw->line->line = malloc(sizeof(struct _raw_str));

	/* malloc the line with "" */
	raw->line->line->str = malloc(1);
	raw->line->line->str[0] = '\0';
	raw->line->line->len = 0;
	raw->line->cursor = 0;

	/* set up standard settings */
	raw->settings = malloc(sizeof(struct _raw_set));

	/* set up terminal settings */
	raw->term = malloc(sizeof(struct _raw_term));
	raw->term->fd = STDIN_FILENO;
	raw->term->mode = false;
	tcgetattr(0, &raw->term->original);

	/* input needs to be from a terminal */
	assert(isatty(raw->term->fd));

	/* everything else */
	raw->buffer = NULL;
	raw->safe = true;
	raw->atexit = _raw_strdup(atexit);

	return raw;
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

	/* clear out terminal settings */
	free(raw->term);

	/* clear out everything else */
	free(raw->buffer);
	free(raw->atexit);
	raw->safe = false;

	/* finally, free the structure itself */
	free(raw);
} /* raw_free() */

char *raw_input(raw_t *raw, char *prompt) {
	assert(raw->safe);

	/* erase old line */
	_raw_set_line(raw, "", 0);

	/* get prompt string and print it */
	raw->line->prompt->str = prompt;
	raw->line->prompt->len = strlen(raw->line->prompt->str);

	printf("%s", raw->line->prompt->str);
	fflush(stdout);

	/* set up state */
	int enter = false;

	/* enable raw mode */
	_raw_mode(raw, true);

	do {
		int err = SUCCESS, move = false;

		/* get first char */
		char ch;
		read(raw->term->fd, &ch, 1);


		/* simple printable chars */
		if(ch > 31 && ch < 127) {
			err = _raw_insert(raw, ch);
		}
		else {
			switch(ch) {
				case 3: /* ctrl-c */
					/* disable raw mode */
					_raw_mode(raw, false);

					/* raise the expected signal (return NULL to seal the deal [if there is a handler]) */
					raise(SIGINT);
					return NULL;
				case 4: /* ctrl-d */
					if(raw->atexit) {
						/* copy over abrupt input and act as enter */
						_raw_set_line(raw, raw->atexit, 0);
						enter = true;
					}

					/* act as combined delete and enter */
					else if(_raw_del_char(raw) != SUCCESS)
						/* cursor is at end, act like an enter */
						enter = true;
					break;
				case 13: /* enter */
					enter = true;
					break;
				case 127: /* ctrl-h (sometimes used as backspace) */
				case 8: /* backspace */
					err = _raw_backspace(raw);
					break;
				case 27: /* escape (start of sequence) */
					{
						/* get next two chars from the sequence */
						char seq[2];
						if(read(raw->term->fd, seq, 2) < 0)
							/* no extra characters */
							break;

						/* valid start of escape sequence */
						if(seq[0] == 91) {
							switch(seq[1]) {
								case 68:
									/* left arrow */
									move = true;
									err = _raw_left(raw);
									break;
								case 67:
									/* right arrow */
									move = true;
									err = _raw_right(raw);
									break;
								case 49:
								case 50:
								case 51:
								case 52:
								case 53:
								case 54:
									/* extended escape */
									{
										/* read next two byes of extended escape sequence */
										char eseq[2];
										if(read(raw->term->fd, eseq, 2) < 0)
											/* no extra characters */
											break;

										if(seq[1] == 51 && eseq[0] == 126)
											/* delete */
											err = _raw_delete(raw);
									}
									break;
								default:
									err = BELL;
									break;
							}
						}
					}
					break;
				default:
					err = BELL;
					break;
			}
		}

		/* was there an error? if so, act on it and don't update anything */
		if(err != SUCCESS) {
			_raw_error(err);
			continue;
		}

		/* redraw input */
		_raw_redraw(raw, !move);
	} while(!enter);

	/* disable raw mode */
	_raw_mode(raw, false);

	/* print the enter newline */
	printf("\n");

	/* copy over input to buffer */
	_raw_update_buffer(raw);

	/* return buffer */
	return raw->buffer;
} /* raw_input() */
