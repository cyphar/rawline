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

		input = raw_input(raw, ">>> ");

		if(strlen(input)) {
			printf(format, input);
			raw_hist_add(raw);
		}

	} while(strcmp(input, "exit"));

	fprintf(stderr, "\n--Recent commands--\n%s\n", raw_hist_get(raw));
	raw_free(raw);
	return 0;
} /* main() */
