PREFIX=/usr/local
BIN_PREFIX=${PREFIX}/bin

CFLAGS=-g -W -Wall -Wextra -Werror
LDFLAGS=-lm

all: bench

test: bench test.sh
	./test.sh

install: bench bench-git
	cp bench ${BIN_PREFIX}
	sed 's#BENCH=./bench#BENCH=bench#' < bench-git > ${BIN_PREFIX}/bench-git
	chmod +x ${BIN_PREFIX}/bench-git
	ln ${BIN_PREFIX}/bench-git ${BIN_PREFIX}/bench-svn
