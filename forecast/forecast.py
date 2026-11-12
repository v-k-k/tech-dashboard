#!/usr/bin/env python3
# =============================================================================
# forecast.py - 30-day price forecast with uncertainty bands and a backtest
#
# ALGORITHM
#
# 1. Data
#    One year of daily history: NBU rates for fiat (UAH per unit) and
#    CoinGecko prices for crypto (USD). NBU publishes only on business days,
#    so weekends/holidays are forward-filled to get a calendar-day series.
#    Prices are converted to logs, so forecasts can never go negative.
#
# 2. Median path: Holt's exponential smoothing with a damped trend (ETS)
#      f = l + phi * t                   (one-step-ahead forecast)
#      l = alpha * y + (1 - alpha) * f   (level)
#      t = beta * (l_new - l_old) + (1 - beta) * phi * t   (trend)
#    alpha, beta, phi come from a grid search (lowest one-step squared error).
#    phi < 1 damps the trend so the forecast flattens instead of running away.
#
# 3. Uncertainty: GARCH(1,1) volatility + filtered historical simulation
#      sigma2[t+1] = omega + alpha_g * e[t]^2 + beta_g * sigma2[t]
#    GARCH(1,1) is fitted (Student-t likelihood, zero mean) with the `arch`
#    package. Volatility is therefore not constant: the forecast cone widens
#    after large moves and narrows in calm periods.
#    The fitted model is standardized (z = return / conditional volatility)
#    and 3000 future paths are simulated by BOOTSTRAPPING these z values
#    (random draws with replacement from real history), so fat tails and
#    skewness of the data are kept instead of assuming a normal distribution.
#    Band = ETS median + simulated cumulative shock quantiles (p10/p25/p75/p90)
#    measured relative to the median shock.
#    Fallback when `arch` is missing or the fit fails: constant volatility
#    (alpha_g = beta_g = 0) with the same bootstrap.
#
# 4. Backtest (rolling origin)
#    Every 15 days (starting after 120 days of history) the model is refitted
#    on data up to that day, forecasts 30 days ahead, and is compared with
#    what actually happened at horizons of 7 and 30 days:
#      ratio       = MAE of ETS median / MAE of the naive forecast
#                    ("tomorrow equals today"). Below 1.0 means better than
#                    naive, above 1.0 means worse.
#      coverage_80 = share of actual values inside the p10..p90 band
#                    (ideal: about 0.80)
#      coverage_50 = share of actual values inside the p25..p75 band
#                    (ideal: about 0.50)
#
# LIMITATIONS
#  - Few origins (about 15) with overlapping windows: backtest numbers are
#    noisy, treat them as a rough indication.
#  - Prices are hard to predict; ratio close to 1.0 is the normal outcome.
#  - Forward-filled fiat series contain many zero returns (weekends), which
#    slightly distorts the GARCH fit for fiat.
#  - The model knows nothing about news or fundamentals.
#    Statistical estimate, not financial advice.
# =============================================================================
import json, math, os, random, time, warnings
import urllib.error, urllib.request
import datetime as dt

try:
    import numpy as np
    from arch import arch_model
    HAVE_ARCH = True
except Exception:
    HAVE_ARCH = False

FIAT = ['USD', 'EUR', 'PLN', 'GBP', 'CNY', 'JPY', 'CAD', 'CHF', 'AUD', 'XAU']
CRYPTO = ['bitcoin', 'ethereum', 'tether', 'binancecoin', 'ripple', 'solana',
          'dogecoin', 'cardano', 'tron', 'the-open-network', 'litecoin', 'monero']
H = 30
N_FINAL = 3000
N_BACKTEST = 500
OUT = '/var/www/html/forecast.json'
QS = {'p10': 0.10, 'p25': 0.25, 'p75': 0.75, 'p90': 0.90}
random.seed(42)


def _get_once(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def get(url):
    for wait in (0, 20, 40, 60):
        time.sleep(wait)
        try:
            return _get_once(url)
        except urllib.error.HTTPError as e:
            if e.code != 429:
                raise
    raise RuntimeError('rate limited (429) after retries')


def fiat_hist(cc):
    end = dt.date.today()
    start = end - dt.timedelta(days=365)
    d = get('https://bank.gov.ua/NBU_Exchange/exchange_site?start=%s&end=%s&valcode=%s&sort=exchangedate&order=asc&json'
            % (start.strftime('%Y%m%d'), end.strftime('%Y%m%d'), cc.lower()))
    by_day = {dt.datetime.strptime(x['exchangedate'], '%d.%m.%Y').date(): (x.get('rate_per_unit') or x['rate']) for x in d}
    if not by_day:
        raise ValueError('no data')
    day, last_day = min(by_day), max(by_day)
    out, cur = [], by_day[day]
    while day <= last_day:
        cur = by_day.get(day, cur)
        out.append(cur)
        day += dt.timedelta(days=1)
    return out


def crypto_hist(cid):
    d = get('https://api.coingecko.com/api/v3/coins/%s/market_chart?vs_currency=usd&days=365' % cid)
    return [p[1] for p in d['prices']]


def holt(y, a, b, phi):
    l, t, sse = y[0], y[1] - y[0], 0.0
    for v in y[1:]:
        f = l + phi * t
        sse += (v - f) ** 2
        ln = a * v + (1 - a) * f
        t = b * (ln - l) + (1 - b) * phi * t
        l = ln
    return sse, l, t


def fit_holt(y):
    best = None
    for a in (0.1, 0.3, 0.5, 0.7, 0.9):
        for b in (0.01, 0.05, 0.1, 0.2):
            for phi in (0.8, 0.9, 0.98):
                sse, l, t = holt(y, a, b, phi)
                if best is None or sse < best[0]:
                    best = (sse, l, t, a, b, phi)
    return best[1:]


def fit_vol(r):
    """Fit volatility on log returns. Works in percent units internally."""
    x = [100.0 * v for v in r]
    vol = None
    if HAVE_ARCH:
        try:
            with warnings.catch_warnings():
                warnings.simplefilter('ignore')
                res = arch_model(np.array(x), mean='Zero', vol='GARCH',
                                 p=1, q=1, dist='t').fit(disp='off', show_warning=False)
            p = res.params
            om, al, be = float(p['omega']), float(p['alpha[1]']), float(p['beta[1]'])
            cv = np.asarray(res.conditional_volatility)
            if om > 0 and al >= 0 and be >= 0 and al + be < 0.9999:
                z = [xi / c for xi, c in zip(x, cv) if c > 0]
                vol = {'omega': om, 'alpha': al, 'beta': be,
                       's2': float(cv[-1]) ** 2, 'e': x[-1], 'z': z,
                       'model': 'GARCH(1,1)-t'}
        except Exception:
            vol = None
    if vol is None:
        var = sum(v * v for v in x) / len(x)
        sd = math.sqrt(var)
        z = [v / sd for v in x] if sd > 0 else [0.0]
        vol = {'omega': var, 'alpha': 0.0, 'beta': 0.0, 's2': var, 'e': 0.0,
               'z': z, 'model': 'constant vol'}
    z = vol['z']
    m = sum(z) / len(z)
    s = math.sqrt(sum((v - m) ** 2 for v in z) / len(z)) or 1.0
    vol['z'] = [(v - m) / s for v in z]
    return vol


def simulate(v, horizon, n):
    cols = [[0.0] * n for _ in range(horizon)]
    z = v['z']
    for i in range(n):
        s2 = v['omega'] + v['alpha'] * v['e'] ** 2 + v['beta'] * v['s2']
        cum = 0.0
        for k in range(horizon):
            e = math.sqrt(s2) * random.choice(z)
            cum += e
            cols[k][i] = cum / 100.0
            s2 = v['omega'] + v['alpha'] * e * e + v['beta'] * s2
    for c in cols:
        c.sort()
    return cols


def quant(sorted_vals, q):
    return sorted_vals[min(len(sorted_vals) - 1, int(q * len(sorted_vals)))]


def make_forecast(prices, horizon, n):
    y = [math.log(p) for p in prices if p > 0]
    if len(y) < 30:
        raise ValueError('not enough data')
    l, t, a, b, phi = fit_holt(y)
    r = [y[i] - y[i - 1] for i in range(1, len(y))]
    v = fit_vol(r)
    cols = simulate(v, horizon, n)

    res = {k: [] for k in ('median', 'p10', 'p25', 'p75', 'p90')}
    damp = 0.0
    for h in range(1, horizon + 1):
        damp += phi ** h
        m = l + t * damp
        mid = quant(cols[h - 1], 0.5)
        res['median'].append(round(math.exp(m), 6))
        for k, q in QS.items():
            res[k].append(round(math.exp(m + quant(cols[h - 1], q) - mid), 6))
    today = dt.date.today()
    res['dates'] = [(today + dt.timedelta(days=h)).isoformat() for h in range(1, horizon + 1)]
    res['last'] = prices[-1]
    s2_next = v['omega'] + v['alpha'] * v['e'] ** 2 + v['beta'] * v['s2']
    res['params'] = {'alpha': a, 'beta': b, 'phi': phi,
                     'daily_sd': round(math.sqrt(s2_next) / 100.0, 5)}
    res['vol_model'] = v['model']
    return res


def backtest(prices):
    acc = {h: {'em': [], 'en': [], 'c80': [], 'c50': []} for h in (7, 30)}
    n = 0
    for o in range(120, len(prices) - H, 15):
        train = prices[:o + 1]
        f = make_forecast(train, H, N_BACKTEST)
        n += 1
        for h in (7, 30):
            actual = prices[o + h]
            a = acc[h]
            a['em'].append(abs(math.log(actual / f['median'][h - 1])))
            a['en'].append(abs(math.log(actual / train[-1])))
            a['c80'].append(f['p10'][h - 1] <= actual <= f['p90'][h - 1])
            a['c50'].append(f['p25'][h - 1] <= actual <= f['p75'][h - 1])
    if n == 0:
        return None
    res = {'origins': n}
    for h, a in acc.items():
        mm, mn = sum(a['em']) / n, sum(a['en']) / n
        res['h%d' % h] = {
            'mae_model_pct': round(mm * 100, 3),
            'mae_naive_pct': round(mn * 100, 3),
            'ratio': round(mm / mn, 3) if mn > 0 else None,
            'coverage_80': round(sum(a['c80']) / n, 3),
            'coverage_50': round(sum(a['c50']) / n, 3),
        }
    return res


def main():
    print('arch available:', HAVE_ARCH)
    try:
        with open(OUT) as fh:
            old = json.load(fh).get('assets', {})
    except Exception:
        old = {}
    jobs = [(c, '₴', c, fiat_hist) for c in FIAT] + [(c, '$', c, crypto_hist) for c in CRYPTO]
    assets = {}
    for key, unit, arg, fn in jobs:
        try:
            prices = fn(arg)
            f = make_forecast(prices, H, N_FINAL)
            f['unit'] = unit
            f['backtest'] = backtest(prices)
            assets[key] = f
            bt = f['backtest'] or {}
            print('ok', key, f['vol_model'],
                  '| ratio 7d/30d:', bt.get('h7', {}).get('ratio'), bt.get('h30', {}).get('ratio'),
                  '| coverage80 7d/30d:', bt.get('h7', {}).get('coverage_80'), bt.get('h30', {}).get('coverage_80'))
        except Exception as e:
            print('fail', key, e)
            if key in old:
                assets[key] = old[key]
                print('kept previous forecast for', key)
        time.sleep(10 if key in CRYPTO else 0.3)
    data = {'generated': dt.datetime.now(dt.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
            'method': 'Damped-trend ETS median + GARCH(1,1)-t bootstrap bands',
            'horizon': H, 'assets': assets}
    tmp = OUT + '.tmp'
    with open(tmp, 'w') as f:
        json.dump(data, f, ensure_ascii=False)
    os.chmod(tmp, 0o644)
    os.replace(tmp, OUT)


if __name__ == '__main__':
    main()

