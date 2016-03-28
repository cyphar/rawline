/* rawline: A small line editing library
 * Copyright (c) 2013 Aleksa Sarai
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rawline.h"

char **callback(char *str) {
	char *table[] = {
		"hello",
		"hxllo",
		"this is a",
		"this is a test",
		NULL
	};

	if(!strcmp(str, "helll"))
		table[0] = "helllo";

	int tablelen = 1;
	int i;
	for(i = 0; table[i] != NULL; i++)
		tablelen++;

	char **ret = malloc(sizeof(char *) * tablelen);

	for(i = 0; i < tablelen; i++) {
		if(table[i]) {
			ret[i] = malloc(strlen(table[i]) + 1);
			strcpy(ret[i], table[i]);
		}
		else {
			ret[i] = NULL;
		}
	}

	return ret;
} /* callback() */

void cleanup(char **table) {
	int i;
	for(i = 0; table[i] != NULL; i++)
		free(table[i]);
	free(table);
} /* cleanup() */

#define EXAMPLE_HISTORY_SERIAL	"hello\n" \
								"this\n" \
								"is\n" \
								"a\n" \
								"test\n" \
								"of\n" \
								"the\n" \
								"emergency\n" \
								"broadcast\n" \
								"system\n"

int main(int argc, char **argv) {
	struct raw_t *raw;
   	raw = raw_new("exit");
	raw_hist(raw, true, 2);
	raw_comp(raw, true, callback, cleanup);

	raw_hist_set(raw, EXAMPLE_HISTORY_SERIAL);

	char *input = NULL, *format = "%s\n";

	if(argc > 1 && !strcmp(argv[1], "-n"))
		format = "%s";

	do {

		input = raw_input(raw, "\x1b[1;32m>>>\x1b[0m ");

		if(strlen(input)) {
			printf(format, input);
			raw_hist_add(raw);
		}

	} while(strcmp(input, "exit"));

	fprintf(stderr, "\n--Recent commands--\n%s\n", raw_hist_get(raw));
	raw_free(raw);
	return 0;
} /* main() */
