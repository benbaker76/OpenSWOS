#!/usr/bin/env python3
"""worldgen/fetch.py -- the one polite fetcher of the world data generator (docs/WORLD_DATAGEN.md).

Every page any worldgen script reads comes through get(): fetched ONCE, one request at a time,
with a delay before each request to a host (4-7 s for Transfermarkt, 3 s for the others), and
cached gzipped under the cache folder with a manifest line (url, date, status, bytes, sha1). A
build reads the cache only, so the same cache gives the same output.

The cache is raw source data and NEVER goes in git: it lives in bin/data_json/worldgen-cache/
(ignored with the rest of bin/data_json), or wherever $WORLDGEN_CACHE says.

Requests carry an ordinary desktop browser's request properties (the owner, 2026-11-13): a
current browser User-Agent, Accept, Accept-Language, Accept-Encoding (gzip handled here) and
one cookie jar for the whole run. No personal data in any header.

    python3 -I scripts/worldgen/fetch.py URL        # fetch (or read from the cache), print the size
    python3 -I scripts/worldgen/fetch.py --stats    # what the cache holds, per host
"""
import gzip
import hashlib
import http.cookiejar
import os
import random
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CACHE = os.environ.get('WORLDGEN_CACHE') or os.path.join(REPO, 'bin', 'data_json', 'worldgen-cache')
PAGES = os.path.join(CACHE, 'pages')
MANIFEST = os.path.join(CACHE, 'manifest.tsv')

UA = ('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) '
      'Chrome/131.0.0.0 Safari/537.36')
HEADERS_HTML = {
    'User-Agent': UA,
    'Accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8',
    'Accept-Language': 'en-GB,en;q=0.9',
    'Accept-Encoding': 'gzip',
    'Upgrade-Insecure-Requests': '1',
}
HEADERS_JSON = dict(HEADERS_HTML, Accept='application/json, text/plain, */*')
HEADERS_JSON.pop('Upgrade-Insecure-Requests')

# Wikimedia's servers want a client that says what it is (their User-Agent policy: a browser
# string from a script is refused with 403), and so does the Internet Archive's Wayback Machine
# (a browser string from a script gets 429), so they get the project's own, generic one.
TOOL_UA = 'OpenSWOS-worldgen/1.0 (https://github.com/benbaker76/OpenSWOS; football game data study)'
TOOL_UA_HOSTS = ('query.wikidata.org', 'www.wikidata.org', 'en.wikipedia.org', 'commons.wikimedia.org',
                 'upload.wikimedia.org', 'web.archive.org')

DELAY = {'www.transfermarkt.com': (4.0, 7.0), 'web.archive.org': (5.0, 7.0)}        # seconds between two requests to one host
DEFAULT_DELAY = (3.0, 3.5)

_jar = http.cookiejar.CookieJar()
_opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(_jar))
_last = {}                                              # host -> time of its last request
COUNT = {'fetched': 0, 'cached': 0}


def _key(url):
    return hashlib.sha1(url.encode()).hexdigest()


def cached(url):
    """The cached body of url (bytes), or None."""
    p = os.path.join(PAGES, _key(url) + '.gz')
    if os.path.exists(p):
        with gzip.open(p, 'rb') as f:
            return f.read()
    return None


def get(url, json_api=False, offline=False, retries=3):
    """The body of url (bytes): from the cache, else fetched politely and cached. None for a 404
    (remembered: a 404 is cached as an empty page with status 404 in the manifest)."""
    body = cached(url)
    if body is not None:
        COUNT['cached'] += 1
        return body or None
    if offline or os.environ.get('WORLDGEN_OFFLINE'):
        raise SystemExit(f'not in the cache (offline): {url}')
    host = urllib.parse.urlsplit(url).netloc
    lo, hi = DELAY.get(host, DEFAULT_DELAY)
    for attempt in range(retries):
        wait = _last.get(host, 0) + random.uniform(lo, hi) - time.time()
        if wait > 0:
            time.sleep(wait)
        headers = dict(HEADERS_JSON if json_api else HEADERS_HTML)
        if host in TOOL_UA_HOSTS:
            headers['User-Agent'] = TOOL_UA
        req = urllib.request.Request(url, headers=headers)
        status = 0
        try:
            with _opener.open(req, timeout=60) as r:
                status, raw = r.status, r.read()
                if r.headers.get('Content-Encoding') == 'gzip':
                    raw = gzip.decompress(raw)
        except urllib.error.HTTPError as e:
            status, raw = e.code, b''
        except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
            status, raw = -1, str(e).encode()
        _last[host] = time.time()
        if status == 200 or status == 404:
            break
        sys.stderr.write(f'[fetch] {status} {url} (attempt {attempt + 1})\n')
        time.sleep(30 * (attempt + 1))                  # back off: 403 / 429 / 5xx
    else:
        raise SystemExit(f'giving up: {status} {url}')
    COUNT['fetched'] += 1
    os.makedirs(PAGES, exist_ok=True)
    body = raw if status == 200 else b''
    with gzip.open(os.path.join(PAGES, _key(url) + '.gz'), 'wb') as f:
        f.write(body)
    with open(MANIFEST, 'a', encoding='utf-8') as f:
        f.write(f"{url}\t{time.strftime('%Y-%m-%dT%H:%M:%S')}\t{status}\t{len(body)}\t"
                f"{hashlib.sha1(body).hexdigest()}\n")
    return body or None


def stats():
    hosts = {}
    if os.path.exists(MANIFEST):
        for line in open(MANIFEST, encoding='utf-8'):
            url = line.split('\t')[0]
            h = urllib.parse.urlsplit(url).netloc
            hosts[h] = hosts.get(h, 0) + 1
    return hosts


if __name__ == '__main__':
    if sys.argv[1:] == ['--stats']:
        for h, n in sorted(stats().items()):
            print(f'{h:32} {n}')
    else:
        for u in sys.argv[1:]:
            b = get(u)
            print(u, len(b or b''))
