rawline
=======

A small line editing library in C, intended to replace other "huge" libraries such as libedit and readline.
Written in less than 1000 lines of MIT licensed code.

Features
--------

* Single line editing
* History
* Completion

Using rawline
-------------

Everything starts with a simple:
```
raw_t *raw_state = raw_new(<str>);

/* str is the string which raw_input() will return when
 * it recieves an abrubt return (ctrl-d, but not ctrl-c).
 * If str is NULL, then ctrl-d will act as a delete key. */

/* ... */

raw_free(raw_state);
```

`raw_state` is the state of a rawline instance. You can have as many instances you want. rawline will handle _all_ of
the memory management inside of `raw_state`. `raw_free` will free all of the memory associated to `raw_state`.

### Options ###

By default, all options (except line editing) are **disabled** by default. The first argument and second argument are always
the `raw_state` pointer and a `bool` reflecting whether you are enabling (`1`) or disabling (`0`) the option. When disabling
an option, everything about that option is __lost__, including any buffers. This is noticable when working with history. Also,
if you are disabling an option, the other arguments are **ignored**.

#### History ####

To (en/dis)able line history:
```
raw_hist(raw_state, <(en/dis)able>, <size of history buffer>);

/* If the number of lines of history is less than 1,
 * raw_hist will return -1, and nothing will change. */
```

#### Completion ####

Tab-completion requires a callback function, to give rawline a search table, based on input. There is no requirement for you to do any form of searching. Rawline uses a prefix completion search spec (see below).

To (en/dis)able tab-completion:
```
raw_comp(raw_state, <(en/dis)able>, search_callback, <search_cleanup or NULL>);

/* If search_cleanup is not defined, rawline will not attempt to free the pointer returned
 * by search_callback(input). If search_callback is not give (i.e. NULL), raw_comp() will
 * return -1 and nothing will change. */
```

Here are some sample `search_callback()` and `search_cleanup()` function definitions:

```

char **search_callback(char *input) {
	/* `input` is the input string when the user pressed <tab> */

	char **ret = NULL;

	/* If the returned search table is empty (the first item is NULL or
	 * the table itself is NULL), the input will be unaffected and the
	 * terminal will beep. */

	/* The search table *must* be terminated with a `NULL` pointer. This
	 * is used by rawline to find the end of the table (like '\0' terminators
	 * in C "strings". */

	/* ... */

	return ret;
}

```

##### Prefix completion? #####

Prefix completion is where the largest common "prefix" (starting from the input string) in a search table is matched, and no more. This is very similar to the technique bash uses. Here's some examples:

```
/* Assuming the following is the search table */
char **search_table = {
	"Hello, world!",
	"How many roads must a man walk down?",
	"How many streets must a man walk down?",
	"The only winning move is not to play."
	"This is a test",
	"This is a test of the emergency broadcast system.",
	NULL
}


/* <input-before-tab-completion> ==> <input-after-tab-completion>*/

"H"					==> "H" (beep)
"He"				==> "Hello, world!"

"Ho"				==> "How many "
"How many r"		==> "How many roads must a man walk down?"
"How many s"		==> "How many streets must a man walk down?"

"T"					==> "T" (beep)
"Th"				==> "Th" (beep)
"The only winn"		==> "The only winning move is not to play."
"This "				==> "This is a test"
"This is a test "	==> "This is a test of the emergency broadcast system."

/* Dummy matches aren't modified */

""					==> "" (beep)
"A strange game."	==> "A strange game." (beep)
```
