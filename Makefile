all: httpd client
LIBS = -lpthread
httpd: httpd.c
	gcc -g -w -W -Wall -o $@ $< $(LIBS)

client: simpleclient.c
	gcc -g -w -W -Wall  -o $@ $<
clean:
	rm httpd
