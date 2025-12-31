ALL: build

build: fat32

fat32: fat32.c
	clang -Wall -Wpedantic -Wextra -Werror fat32.c -o fat32

clean:
	rm fat32

