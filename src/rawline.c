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
#	define assert(cond, desc) do { if(!cond) { fprintf(stderr, "rawline: %s: condition '%s' failed -- '%s'\n", __func__, #cond, desc); abort(); } } while(0)
#endif

/* Convert bool-ish ints to bools. */
#define BOOL(b) (!!b)

/* VT100 control codes used by rawline. It is assumed the code using these printf-style
 * control code formats knows the amount of args (or things to be subbed in), so we don't
 * need to use functions for these. */

#define C_BELL				"\x7"		/* BEL -- Ring the terminal bell. */
#define C_CUR_MOVE_COL		"\x1b[%dG"	/* CHA -- Move to absolute column <n> */
#define C_LN_CLEAR_END		"\x1b[0K"	/* EL(0) -- Clear from cursor to EOL */

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

struct _raw_hist {
	char **history; /* entire history (stored in reverse, where history[0] is the latest history item) */
	char *original; /* original input (position history[-1]) */
	char *buffer; /* stores buffer of serialised history */

	int len; /* size of history */
	int max; /* maximum size of history */
	int index; /* history index of current line (-1 if line not in history) */
};

struct _raw_comp {
	char **(*callback)(char *input); /* a callback function to fill a search table for completion */
	void (*cleanup)(char **table); /* optional cleanup function to free memory given from output of callback() */
};

struct _raw_set {
	bool history; /* is history enabled? */
	bool completion; /* is completion enabled? */
};

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

/* Internal Error Types */
enum {
	SUCCESS, /* no errors to report */
	SILENT, /* ignorable error */
	BELL /* ring the terminal bell. */
};

/* Static functions only used internally. These functions are never exposed outside of the library,
 * and are not required to be used by external programs. They should never be used by anything outside
 * of this library, because they contain very specific functionality not required for everyday use. */

static char *_raw_strdup(char *str) {
	if(!str)
		return NULL;

	int len = strlen(str);

	char *ret = malloc(len + 1);
	memcpy(ret, str, len);

	ret[len] = '\0';
	return ret;
} /* _raw_strdup() */

static int _raw_strnchr(char *str, char ch) {
	int ret = 0, i, len = strlen(str);
	for(i = 0; i < len; i++)
		if(str[i] == ch)
			ret++;
	return ret;
} /* _raw_strnchr() */

static void _raw_error(int err) {
	switch(err) {
		case BELL:
			fprintf(stderr, C_BELL);
			fflush(stderr);
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
	assert(raw->safe, "raw_t structure not allocated");

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

/* == Line Editing == */

static int _raw_del_char(raw_t *raw) {
	assert(raw->safe, "raw_t structure not allocated");

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
	assert(raw->safe, "raw_t structure not allocated");

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
	assert(raw->safe, "raw_t structure not allocated");

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
	assert(raw->safe, "raw_t structure not allocated");

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

static void _raw_redraw(raw_t *raw, bool change) {
	assert(raw->safe, "raw_t structure not allocated");

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

/* == History == */

static struct _raw_hist *_raw_hist_new(int size) {
	struct _raw_hist *hist = malloc(sizeof(struct _raw_hist));

	hist->max = size + 1;
	hist->len = 0;
	hist->index = -1;

	hist->history = malloc(sizeof(char *) * hist->max);
	memset(hist->history, 0, sizeof(char *) * hist->max);

	hist->buffer = NULL;
	hist->original = NULL;

	return hist;
} /* _raw_hist_new() */

static void _raw_set_line(raw_t *raw, char *str, int cursor) {
	assert(raw->safe, "raw_t structure not allocated");

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

static void _raw_hist_free(struct _raw_hist *hist) {
	int i;
	for(i = 0; i < hist->max; i++)
		free(hist->history[i]);
	free(hist->history);

	free(hist->buffer);
	free(hist->original);

	hist->len = 0;
	hist->max = 0;
} /* _raw_hist_free() */

static void _raw_hist_add_str(raw_t *raw, char *str) {
	assert(raw->safe, "raw_t structure not allocated");
	assert(raw->settings->history, "raw_t history not enabled");

	/* This sentence explans the next section of code: "memmove(3) is magical". */

	if(raw->hist->index < 0) {
		/* free the last item in the history (if the history is full) */
		if(raw->hist->len >= raw->hist->max)
			free(raw->hist->history[raw->hist->max - 1]);

		/* memmove(3) the entire history */
		memmove(raw->hist->history + 1, raw->hist->history, sizeof(char *) * (raw->hist->max - 1));
		raw->hist->index = 0;

		/* update length */
		raw->hist->len++;
		if(raw->hist->len > raw->hist->max)
			raw->hist->len = raw->hist->max;

		raw->hist->history[0] = NULL;
	}

	/* modify (or add) history item */
	if(raw->hist->index > -1)
		free(raw->hist->history[raw->hist->index]);

	raw->hist->history[raw->hist->index] = _raw_strdup(str);
} /* _raw_hist_add_str() */

#define _RAW_HIST_PREV 1
#define _RAW_HIST_NEXT -1

static int _raw_hist_move(raw_t *raw, int move) {
	assert(raw->safe, "raw_t structure not allocated");
	assert(raw->settings->history, "raw_t history not enabled");

	/* movement is invalid if movement will be "out of bounds" on the array */
	if(raw->hist->index + move < -1 || raw->hist->index + move >= raw->hist->len)
		return BELL;

	/* copy over the line before getting the history */
	if(raw->hist->index < 0) {
		free(raw->hist->original);
		raw->hist->original = _raw_strdup(raw->line->line->str);
	}

	/* free current line data */
	free(raw->line->line->str);
	raw->hist->index += move;

	if(raw->hist->index < 0)
		/* get original line */
		raw->line->line->str = _raw_strdup(raw->hist->original);
	else
		/* move position and copy over the history entry */
		raw->line->line->str = _raw_strdup(raw->hist->history[raw->hist->index]);

	raw->line->line->len = strlen(raw->line->line->str);
	return SUCCESS;
} /* _raw_hist_move() */

static char *_raw_hist_to_serial(raw_t *raw) {
	char *ret = NULL;

	int i, len = 0, itemlen = 0;
	for(i = 0; i < raw->hist->len; i++) {
		itemlen = strlen(raw->hist->history[i]) + 1;

		ret = realloc(ret, len + itemlen);
		memcpy(ret + len, raw->hist->history[i], itemlen);

		len += itemlen;
		ret[len - 1] = '\n'; /* the seperator */
	}

	if(!ret)
		return NULL;

	/* null terminate string */
	ret[len - 1] = '\0';
	return ret;
} /* _raw_hist_to_serial() */

static int _raw_hist_from_serial(raw_t *raw, char *str) {
	/* no string given */
	if(!str)
		return -1;

	/* eradicate the old history. */
	int max = raw->hist->max - 1;
	_raw_hist_free(raw->hist);
	free(raw->hist);

	/* get length of serialised history */
	int len = _raw_strnchr(str, '\n');

	/* length is upper limit */
	if(len > max)
		max = len;

	/* make a new history */
	raw->hist = _raw_hist_new(max);

	/* fill up the history with the tokens */
	str = _raw_strdup(str);
	char *tok = strtok(str, "\n");

	if(tok) {
		do {
			raw->hist->index = -1;
			_raw_hist_add_str(raw, tok);
		} while((tok = strtok(NULL, "\n")) != NULL);
	}

	free(str);
	return 0;
} /* _raw_hist_from_serial() */

/* == Completion == */

static char **_raw_comp_filter(raw_t *raw, char *str) {
	assert(raw->safe, "raw_t structure not allocated");
	assert(raw->settings->completion, "raw_t completion not enabled");
	assert(raw->comp->callback, "raw_t completion callback not defined");

	/* get search table */
	char **table = raw->comp->callback(str);
	char **search = NULL;

	if(!table)
		return NULL;

	/* filter table with string */
	int i, searchlen = 0, lenstr = strlen(str);
	for(i = 0; table[i] != NULL; i++) {
		/* valid entries for consideration must start with input string */
		if(!strncmp(str, table[i], lenstr)) {
			searchlen++;

			/* append the string to the search table */
			search = realloc(search, searchlen * sizeof(char *));
			search[searchlen - 1] = _raw_strdup(table[i]);
		}
	}

	/* null terminate search table */
	search = realloc(search, ++searchlen * sizeof(char *));
	search[searchlen - 1] = NULL;

	/* call cleanup function (if defined) */
	if(raw->comp->cleanup)
		raw->comp->cleanup(table);

	return search;
} /* _raw_comp_filter() */

static char *_raw_comp_get(raw_t *raw, char *str) {
	assert(raw->safe, "raw_t structure not allocated");
	assert(raw->settings->completion, "raw_t completion not enabled");

	char **search = _raw_comp_filter(raw, str);

	/* no matches */
	if(!search || !search[0]) {
		if(search) {
			int i;
			for(i = 0; search[i] != NULL; i++)
				free(search[i]);
			free(search);
		}

		return _raw_strdup(str);
	}

	/* output comparison */
	char *comp = NULL;
	bool same = true;
	int complen = 0, j = 0;


	/* Get the largest common "prefix" for the entire search table. This
	 * is to mimic the bash-like completion, where the longest common prefix
	 * is matched, and the rest is left to the user. */

	do {
		char ch = search[0][j];

		/* add current char to */
		comp = realloc(comp, complen + 1);
		comp[complen] = ch;
		complen++;

		/* Check if the character in that position is the same in every item in
		 * the search table. If so, we can use it in the chosen input. */
		int i;
		for(i = 1; search[i] != NULL && same; i++)
			if(ch != search[i][j])
				same = false;

		/* quit if we hit the null terminator */
		if(ch == '\0')
			break;

		j++;
	} while(same);

	/* null terminate */
	comp[complen - 1] = '\0';

	/* clean up search table */
	int i;
	for(i = 0; search[i] != NULL; i++)
		free(search[i]);
	free(search);

	/* give prefix */
	return comp;
} /* _raw_comp_get() */

/* Functions exposed as an API, for external use. These functions are the only functions which outside
 * programs will ever need to use. They handle *ALL* memory management, and rawline structures aren't
 * to be allocated by the user and are opaque. */

raw_t *raw_new(char *atexit) {
	/* alloc main structure */
	raw_t *raw = malloc(sizeof(raw_t));

	/* set up blank input line */
	raw->line = malloc(sizeof(struct _raw_line));
	raw->line->prompt = malloc(sizeof(struct _raw_str));
	raw->line->line = malloc(sizeof(struct _raw_str));

	/* set the line to "" */
	raw->line->line->str = _raw_strdup("");
	raw->line->line->len = 0;
	raw->line->cursor = 0;

	/* set up standard settings */
	raw->settings = malloc(sizeof(struct _raw_set));
	raw->settings->history = false;
	raw->settings->completion = false;

	/* set up terminal settings */
	raw->term = malloc(sizeof(struct _raw_term));
	raw->term->fd = STDIN_FILENO;
	raw->term->mode = false;
	tcgetattr(0, &raw->term->original);

	/* history is off by default */
	raw->hist = NULL;

	/* completion is off by default */
	raw->comp = NULL;

	/* input needs to be from a terminal */
	assert(isatty(raw->term->fd), "input is not from a tty");

	/* everything else */
	raw->buffer = NULL;
	raw->safe = true;
	raw->atexit = _raw_strdup(atexit);

	return raw;
} /* raw_new() */

int raw_hist(raw_t *raw, bool set, int size) {
	assert(raw->safe, "raw_t structure not allocated");

	/* size *must* be at least 1 */
	if(size <= 0)
		return -1;

	/* ignore re-setting of history */
	if(raw->settings->history == BOOL(set))
		return -2;

	raw->settings->history = BOOL(set);

	if(set) {
		raw->hist = _raw_hist_new(size);
	}
	else {
		_raw_hist_free(raw->hist);
		free(raw->hist);
	}

	return 0;
} /* raw_hist() */

void raw_hist_add_str(raw_t *raw, char *str) {
	assert(raw->safe, "raw_t structure not allocated");
	assert(raw->settings->history, "raw_t history is not enabled");

	_raw_hist_add_str(raw, str);
} /* raw_hist_add_str() */

void raw_hist_add(raw_t *raw) {
	assert(raw->safe, "raw_t structure not allocated");
	assert(raw->settings->history, "raw_t history is not enabled");
	assert(raw->buffer, "no previous input stored in raw_t structure");

	_raw_hist_add_str(raw, raw->buffer);
} /* raw_hist_add() */

char *raw_hist_get(raw_t *raw) {
	assert(raw->safe, "raw_t structure not allocated");
	assert(raw->settings->history, "raw_t history is not enabled");

	char *serial = _raw_hist_to_serial(raw);

	/* update buffer */
	free(raw->hist->buffer);
	raw->hist->buffer = serial;

	return raw->hist->buffer;
} /* raw_hist_get() */

int raw_hist_set(raw_t *raw, char *str) {
	assert(raw->safe, "raw_t structure not allocated");
	assert(raw->settings->history, "raw_t history is not enabled");

	return _raw_hist_from_serial(raw, str);
} /* raw_hist_set() */

int raw_comp(raw_t *raw, bool set, char **(*callback)(char *), void (*cleanup)(char **)) {
	assert(raw->safe, "raw_t structure not allocated");

	/* callback() is required */
	if(!callback)
		return -1;

	/* ignore re-setting of completion */
	if(raw->settings->completion == BOOL(set))
		return -2;

	raw->settings->completion = BOOL(set);

	if(set) {
		raw->comp = malloc(sizeof(struct _raw_comp));
		raw->comp->callback = callback;
		raw->comp->cleanup = cleanup;
	}
	else {
		free(raw->comp);
	}

	return 0;
} /* raw_comp() */

void raw_free(raw_t *raw) {
	assert(raw->safe, "raw_t structure not allocated");

	/* completely clear out line */
	free(raw->line->line->str);
	free(raw->line->line);
	free(raw->line->prompt);
	free(raw->line);

	/* clear out history */
	if(raw->settings->history) {
		raw->settings->history = false;
		_raw_hist_free(raw->hist);
		free(raw->hist);
	}

	/* clear out completion */
	if(raw->settings->completion)
		free(raw->comp);

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
	assert(raw->safe, "raw_t structure not allocated");

	/* erase old line information */
	_raw_set_line(raw, "", 0);
	if(raw->settings->history)
		raw->hist->index = -1;

	/* get prompt string and print it */
	raw->line->prompt->str = prompt;
	raw->line->prompt->len = strlen(raw->line->prompt->str);

	/* make a copy of the history */
	struct _raw_hist *hist = NULL;

	if(raw->settings->history) {
		hist = _raw_hist_new(raw->hist->max - 1);

		int i;
		for(i = 0; i < raw->hist->len; i++)
			hist->history[i] = _raw_strdup(raw->hist->history[i]);

		hist->len = raw->hist->len;
	}

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

					/* free temporary history */
					if(raw->settings->history) {
						_raw_hist_free(hist);
						free(hist);
					}

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
				case 9: /* tab */
					if(raw->settings->completion) {
						char *comp = _raw_comp_get(raw, raw->line->line->str);

						if(!strcmp(comp, raw->line->line->str)) {
							err = BELL;
						}

						else {
							_raw_set_line(raw, comp, 0);
							raw->line->cursor = raw->line->line->len;
						}

						free(comp);
					}
					else {
						err = BELL;
					}
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
								case 65: /* up arrow */
								case 66: /* down arrow */
									if(raw->settings->history) {
										int dir = seq[1] == 65 ? _RAW_HIST_PREV : _RAW_HIST_NEXT;

										err = _raw_hist_move(raw, dir);
										raw->line->cursor = raw->line->line->len;
									}
									else {
										err = BELL;
									}
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
								case 70: /* end */
									raw->line->cursor = raw->line->line->len;
									move = true;
									break;
								case 72: /* home */
									raw->line->cursor = 0;
									move = true;
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

		/* add current line status to temporary history */
		if(raw->hist->index >= 0)
			_raw_hist_add_str(raw, raw->line->line->str);

	} while(!enter);

	/* disable raw mode */
	_raw_mode(raw, false);

	/* print the enter newline */
	printf("\n");

	/* free "temporary" history and point raw-> to it */
	if(raw->settings->history) {
		_raw_hist_free(raw->hist);
		free(raw->hist);
		raw->hist = hist;
	}

	/* copy over input to buffer */
	free(raw->buffer);
	raw->buffer = _raw_strdup(raw->line->line->str);

	/* return buffer */
	return raw->buffer;
} /* raw_input() */
