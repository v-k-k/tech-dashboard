.RECIPEPREFIX = >
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
WEBROOT ?= /var/www/html
CGIDIR  ?= /var/www/cgi-bin
BINDIR  ?= /usr/local/bin
FCDIR   ?= /opt/forecast

# clients that share the libcurl wrapper
HTTP_BINS = bin/gql_trains bin/gtfsrt_count bin/grpc_hello
BINS      = bin/hfp_count $(HTTP_BINS)

all: $(BINS)

$(HTTP_BINS): bin/%: src/%.c src/http_client.c src/http_client.h
> mkdir -p bin
> $(CC) $(CFLAGS) -Isrc -o $@ src/$*.c src/http_client.c -lcurl

# MQTT client uses libmosquitto only
bin/hfp_count: src/hfp_count.c
> mkdir -p bin
> $(CC) $(CFLAGS) -o $@ $< -lmosquitto

install: all
> install -d $(BINDIR) $(CGIDIR) $(WEBROOT) $(FCDIR)
> install -m 755 $(BINS) $(BINDIR)
> install -m 755 cgi-bin/tech.sh $(CGIDIR)/tech.sh
> install -m 644 web/index.html $(WEBROOT)/index.html
> install -m 755 forecast/forecast.py $(FCDIR)/forecast.py

clean:
> rm -rf bin

.PHONY: all install clean