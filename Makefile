all:
	cc ms2_concised.c -o mini
clean:
	rm -f mini
re: clean all