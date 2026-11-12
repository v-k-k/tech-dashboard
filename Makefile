.RECIPEPREFIX = >
CC      ?= gcc
CFLAGS  ?= -O2 -Wall
WEBROOT ?= /var/www/html
CGIDIR  ?= /var/www/cgi-bin
BINDIR  ?= /usr/local/bin
FCDIR   ?= /opt/forecast

BINS = bin/hfp_count bin/gtfsrt_count bin/gql_trains bin/grpc_hello
LIBS_hfp_count    = -lmosquitto
LIBS_gtfsrt_count = -lcurl
LIBS_gql_trains   = -lcurl
LIBS_grpc_hello   = -lcurl

all: $(BINS)

bin/%: src/%.c
> mkdir -p bin
> $(CC) $(CFLAGS) -o $@ $< $(LIBS_$*)

install: all
> install -d $(BINDIR) $(CGIDIR) $(WEBROOT) $(FCDIR)
> install -m 755 $(BINS) $(BINDIR)
> install -m 755 cgi-bin/tech.sh $(CGIDIR)/tech.sh
> install -m 644 web/index.html $(WEBROOT)/index.html
> install -m 755 forecast/forecast.py $(FCDIR)/forecast.py

clean:
> rm -rf bin

.PHONY: all install clean

