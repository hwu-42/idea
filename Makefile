all:
	cc concised_memSafe.c -o mini
clean:
	rm -f mini
re: clean all