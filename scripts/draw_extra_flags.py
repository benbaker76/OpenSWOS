#!/usr/bin/env python3
# draw_extra_flags.py -- the flags mod's own flags (bin/mods/flags/flags/): those GoSquared's set lacks.
#   python3 scripts/draw_extra_flags.py bin/mods/flags/flags   (writes 4 BMPs and a preview beside the folder)
# The flags GoSquared's set lacks, drawn here at the set's size: a 24x24 icon, white,
# with the flag in its 22x16 middle (x 1..22, y 4..19).
from PIL import Image
W,H=22,16
def icon(px):
    im=Image.new('RGB',(24,24),(255,255,255))
    for y in range(H):
        for x in range(W): im.putpixel((1+x,4+y),px[y][x])
    return im
def field(c): return [[c]*W for _ in range(H)]
def put(f,x,y,c):
    if 0<=x<W and 0<=y<H: f[y][x]=c
RED=(204,0,0); YEL=(255,216,0); BLK=(0,0,0); GOLD=(255,206,0)
# Soviet Union: red, gold hammer and sickle with a star above, in the hoist's top corner
su=field(RED)
for x,y in [(4,1),(3,2),(4,2),(5,2),(4,3),(3,3),(5,3)]: put(su,x,y,YEL)          # star
for x,y in [(5,5),(6,6),(5,7),(4,8),(3,8),(2,7),(2,6)]: put(su,x,y,YEL)            # sickle
for x,y in [(3,5),(3,6),(4,6),(4,7),(5,8),(6,9)]: put(su,x,y,YEL)                  # hammer
# East Germany: black, red, gold; the emblem (hammer and compass in a ring of rye) in the middle
dd=[[BLK]*W for _ in range(5)]+[[(221,0,0)]*W for _ in range(6)]+[[GOLD]*W for _ in range(5)]
for y in range(H):
    for x in range(W):
        d=(x-10.5)**2+(y-7.5)**2
        if 2.6**2<=d<=4.1**2: dd[y][x]=GOLD
        elif d<2.6**2: dd[y][x]=(221,0,0)
for x,y in [(10,6),(11,6),(10,7),(11,8),(10,9),(12,9),(9,9)]: put(dd,x,y,GOLD)       # hammer and compass
# Zaire (1971-97): green, a yellow disc with an arm holding a torch's red flame
GRN=(0,153,68); zr=field(GRN)
for y in range(H):
    for x in range(W):
        if (x-10.5)**2+(y-7.5)**2<=5.2**2: zr[y][x]=YEL
for x,y in [(9,11),(10,10),(10,9),(11,8),(11,7)]: put(zr,x,y,(150,90,40))           # arm, torch
for x,y in [(11,6),(12,5),(11,4),(12,4),(12,3),(11,5)]: put(zr,x,y,RED)              # flame
# C.I.S. (1992): its blue field and the Commonwealth's gold emblem, a ring in the middle
BLU=(0,57,166); cis=field(BLU)
for y in range(H):
    for x in range(W):
        d=(x-10.5)**2+(y-7.5)**2
        if 3.2**2<=d<=4.6**2: cis[y][x]=GOLD
for x,y in [(10,7),(11,7),(10,8),(11,8)]: put(cis,x,y,GOLD)
import sys
out=sys.argv[1]
for name,f in [('Soviet-Union',su),('East-Germany',dd),('Zaire',zr),('CIS',cis)]:
    icon(f).save(f'{out}/{name}.bmp')
big=Image.new('RGB',(4*22*8+30,16*8),(40,120,40))
for i,(name,f) in enumerate([('Soviet-Union',su),('East-Germany',dd),('Zaire',zr),('CIS',cis)]):
    big.paste(icon(f).crop((1,4,23,20)).resize((176,128),Image.NEAREST),(i*186,0))
big.save(out+'/../../extra_flags_preview.png') if '--preview' in sys.argv else None
