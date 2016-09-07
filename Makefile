all: httpd client
LIBS = -lpthread
httpd: httpd.c
	gcc -w -W -Wall -o $@ $< $(LIBS)

client: simpleclient.c
	gcc -w -W -Wall  -o $@ $<
clean:
	rm httpd
