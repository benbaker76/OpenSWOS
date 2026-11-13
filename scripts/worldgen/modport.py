"""worldgen/modport.py -- code ported from the SWOS 96/97 Mod's tools (github.com/aminta/swos-9697-mod,
tag v4.1.2): its CODE is MIT-licensed; its DATA is not, and none is here.

  swos_name   tools/c1c2.py swos_name: a player name as the game stores it
  xtra_cap    tools/mkseason.py _xtra_cap: an extra squad player (17-30) no better than the club's
              weakest of the 16 (worked on our player dicts instead of 38-byte records)

MIT License

Copyright (c) 2026 Davide Lorigliola

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""
import unicodedata


def swos_name(s, maxlen=22):
    """Upper-case ASCII, accents dropped (the German sharp s as SS, which NFKD would drop);
    MASSIMILIANO DAL COMPARE -> M. DAL COMPARE when longer than maxlen."""
    s = s.replace('ß', 'ss').replace('ø', 'o').replace('Ø', 'O').replace('ł', 'l').replace('Ł', 'L') \
        .replace('đ', 'd').replace('Đ', 'D').replace('æ', 'ae').replace('Æ', 'AE').replace('ı', 'i')
    s = unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode().upper().strip()
    s = ' '.join(s.split())
    if len(s) > maxlen and ' ' in s:
        w = s.split(' ', 1)
        s = f'{w[0][0]}. {w[1]}'
    return s[:maxlen]


def xtra_cap(p, sixteen):
    """An extra player (squad rows 17-30) no better than the weakest of his club's 16: a keeper's
    price <= the lowest keeper price of the 16; an outfield player's skill sum and price <= the
    lowest outfield price of the 16 (skills scaled down in proportion, key-skill marks kept,
    price = sum). p / sixteen: dicts with 'position', 'skills' (7 values), 'keys', 'price'."""
    gk = p['position'] == 'G'
    same = [q['price'] for q in sixteen if (q['position'] == 'G') == gk] or [q['price'] for q in sixteen]
    cap = min(same)
    if gk:
        p['price'] = min(p['price'], cap)
        return p
    val = list(p['skills'])
    tot = sum(val)
    if tot > cap:                                   # scaled in proportion: the profile stays his own
        exact = [v * cap / tot for v in val]
        val = [int(x) for x in exact]
        for i in sorted(range(7), key=lambda i: (val[i] - exact[i], i))[:cap - sum(val)]:
            val[i] += 1
    p['skills'] = val
    p['price'] = min(p['price'], sum(val))
    return p
