#!/usr/bin/env python3
# The language picker's six flags (README.md here): 24x16 SVGs, a pixel style, our own.
import os

def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="16" viewBox="0 0 24 16" shape-rendering="crispEdges">\n'
            + body + '  <rect x="0.5" y="0.5" width="23" height="15" fill="none" stroke="#000" stroke-opacity="0.35"/>\n</svg>\n')

def hstripes(cols, sizes):
    y, out = 0, ''
    for c, h in zip(cols, sizes):
        out += '  <rect x="0" y="%d" width="24" height="%d" fill="%s"/>\n' % (y, h, c)
        y += h
    return out

def vstripes(cols):
    return ''.join('  <rect x="%d" y="0" width="8" height="16" fill="%s"/>\n' % (i * 8, c) for i, c in enumerate(cols))

FLAGS = {
    'en': '  <rect width="24" height="16" fill="#fff"/>\n  <rect x="10" y="0" width="4" height="16" fill="#ce1124"/>\n'
          '  <rect x="0" y="6" width="24" height="4" fill="#ce1124"/>\n',
    'de': hstripes(['#000', '#dd0000', '#ffce00'], [5, 6, 5]),
    'fr': vstripes(['#0055a4', '#fff', '#ef4135']),
    'es': hstripes(['#c60b1e', '#ffc400', '#c60b1e'], [4, 8, 4]),
    'it': vstripes(['#009246', '#fff', '#ce2b37']),
    'nl': hstripes(['#ae1c28', '#fff', '#21468b'], [5, 6, 5]),
}

here = os.path.dirname(os.path.abspath(__file__))
for code, body in FLAGS.items():
    with open(os.path.join(here, code + '.svg'), 'w') as f:
        f.write(svg(body))
