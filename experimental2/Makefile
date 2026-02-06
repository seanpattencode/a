CC = cc
SQLITE_INC = $(HOME)/micromamba/include
SQLITE_LIB = $(HOME)/micromamba/lib
CFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-result -I$(SQLITE_INC)
LDFLAGS = -L$(SQLITE_LIB) -lsqlite3 -Wl,-rpath,$(SQLITE_LIB)

ac2: ac2.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f ac2

.PHONY: clean
