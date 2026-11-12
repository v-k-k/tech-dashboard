# Tech dashboard

Single-page dashboard: exchange rates (NBU) and crypto (CoinGecko), charts,
Monte Carlo and ETS/GARCH forecasts, and a "Tech general" tab with live data
over different protocols.

## Components
- web/index.html: static page, vanilla JS + Chart.js
- forecast/forecast.py: daily forecast, writes forecast.json next to the page
- cgi-bin/tech.sh: bash CGI that builds HTML blocks
- src/*.c: small clients in C (MQTT, GTFS-Realtime, GraphQL, gRPC)

## Requirements
- Debian/Ubuntu packages: build-essential libcurl4-openssl-dev libmosquitto-dev jq fcgiwrap
- Python 3 (optional: `pip install -r requirements.txt` in a venv)
- A web server that serves web/ and runs CGI. The page expects these routes
  (configuration is not part of this repository):
  - /api/nbu/  -> proxy to https://bank.gov.ua/
  - /api/cg/   -> proxy to https://api.coingecko.com/
  - /cgi-bin/  -> executes cgi-bin/ scripts

## Build and install
    make
    sudo make install
    sudo crontab -e   # see deploy/crontab.example

## Data sources
NBU, CoinGecko, EMSC SeismicPortal, HSL (Helsinki), ZTP Krakow,
Digitraffic (Finland), grpcb.in. Check each source's terms of use and
attribution requirements before publishing.

