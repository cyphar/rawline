# rawline: A small line editing library
# Copyright (c) 2013 Cyphar

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

CC			?= gcc
NAME		?= rawl

SRC_DIR		?= src
TEST_DIR	?= tests
INCLUDE_DIR	?= src

SRC			?= $(wildcard $(SRC_DIR)/*.c)
TEST		?= $(wildcard $(TEST_DIR)/*.c)
INCLUDE		?= $(wildcard $(SRC_DIR)/*.h)

CFLAGS		?= -ansi -I$(INCLUDE_DIR)/
LFLAGS		?=
WARNINGS	?= -Wall -Wextra -Werror

$(NAME): $(SRC) $(INCLUDE) $(TEST)
	$(CC) $(CFLAGS) $(SRC) $(TEST) $(LFLAGS) $(WARNINGS) -o $(NAME)
	strip $(NAME)

debug: $(SRC) $(INCLUDE) $(TEST)
	$(CC) $(CFLAGS) -ggdb -O0 $(SRC) $(TEST) $(LFLAGS) $(WARNINGS) -o $(NAME)

clean:
	rm -f $(NAME)
