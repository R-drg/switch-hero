#!/usr/bin/env python3
"""Generate an original 32-beat synth exercise. No commercial music/assets."""
import math
import struct
import sys
import wave
from pathlib import Path

folder = Path(sys.argv[1] if len(sys.argv) > 1 else 'songs/First-Light')
folder.mkdir(parents=True, exist_ok=True)
resolution = 192
notes = []
for beat in range(4, 64):
    lane = [0, 1, 2, 3, 4, 3, 2, 1][beat % 8]
    tick = beat * resolution
    length = resolution if beat % 8 == 4 else 0
    notes.append((tick, lane, length))
    if beat > 20 and beat % 8 == 0:
        notes.append((tick, (lane + 2) % 5, 0))

def seconds(tick):
    return tick / 192 * .5 if tick <= 6144 else 16 + (tick - 6144) / 192 * .4

sync = '[SyncTrack]\n{\n  0 = B 120000\n  0 = TS 4\n  6144 = B 150000\n}\n'
song = '[Song]\n{\n  Name = "First Light"\n  Artist = "Fretboard demo"\n  Resolution = 192\n  Offset = 0\n}\n'
track = '[ExpertSingle]\n{\n' + ''.join(f'  {t} = N {l} {n}\n' for t,l,n in notes)
track += ''.join(f'  {t} = S 2 768\n' for t in [768,2304,3840,6912,8448]) + '}\n'
easy = '[EasySingle]\n{\n' + ''.join(f'  {t} = N {l % 3} {n}\n' for t,l,n in notes if t % 384 == 0) + '}\n'
(folder / 'notes.chart').write_text(song + sync + track + easy)
(folder / 'song.ini').write_text('[song]\nname = First Light\nartist = Fretboard demo\ncharter = Original generated exercise\ndelay = 0\n')
rate = 24000
duration = seconds(64*192) + 2
signal = [0.] * int(duration * rate)
for t,l,n in notes:
    start = int(seconds(t)*rate)
    frequency = [220,261.6256,293.6648,329.6276,391.9954][l]
    length = min(.4, .16 + n/resolution*.12)
    for i in range(int(length*rate)):
        env = min(1., i/(rate*.005)) * math.exp(-i/(rate*.085))
        signal[start+i] += .17 * env * (math.sin(2*math.pi*frequency*i/rate) + .25*math.sin(4*math.pi*frequency*i/rate))
for beat in range(64):
    start = int(seconds(beat*192)*rate)
    for i in range(int(rate*.055)):
        signal[start+i] += .09*math.exp(-i/(rate*.012))*math.sin(2*math.pi*90*i/rate)
with wave.open(str(folder/'song.wav'),'wb') as out:
    out.setparams((1,2,rate,0,'NONE','not compressed'))
    out.writeframes(b''.join(struct.pack('<h',int(max(-1,min(1,x))*32767)) for x in signal))
print(folder)
