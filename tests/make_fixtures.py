#!/usr/bin/env python3
import math
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

root=Path(sys.argv[1]);root.mkdir(parents=True,exist_ok=True)
chart='''[Song]
{
  Name = "Chart title"
  Artist = "Artist"
  Resolution = 192
  Offset = -0.5
}
[SyncTrack]
{
  0 = B 120000
  384 = B 60000
}
[Events]
{
  0 = E "section intro"
  384 = E "section verse_1a"
}
[ExpertSingle]
{
  0 = N 0 576
  0 = S 2 384
  192 = E solo
  384 = E soloend
  48 = N 1 0
  96 = N 1 0
  96 = N 5 0
  192 = N 1 192
  192 = N 2 384
  384 = N 7 0
  384 = N 6 0
  576 = N 4 0
}
'''
ini='[song]\nname = INI title\nartist = Fixture artist\ndelay = 250\n'
for name in ['chart','midi','utf16','bad','stems']:
    folder=root/name;folder.mkdir(exist_ok=True);(folder/'song.ini').write_text(ini)
    with wave.open(str(folder/'song.wav'),'wb') as w:
        w.setparams((1,2,24000,0,'NONE','not compressed'))
        w.writeframes(b''.join(struct.pack('<h',int(10000*math.sin(2*math.pi*440*i/24000))) for i in range(24000)))
(root/'chart'/'notes.chart').write_text(chart)
(root/'utf16'/'notes.chart').write_text(chart,encoding='utf-16')
(root/'bad'/'notes.chart').write_text(chart.replace('Resolution = 192','Resolution = 0'))
(root/'stems'/'notes.chart').write_text(chart)
for n in ['drums','drums_1','drums_2','vocals','vocals_1','preview']:
    shutil.copyfile(root/'stems'/'song.wav',root/'stems'/f'{n}.wav')

def vlq(v):
    out=[v&127];v>>=7
    while v:out.insert(0,(v&127)|128);v>>=7
    return bytes(out)
def track(events):
    previous=0;out=b''
    for tick,event in sorted(events,key=lambda e:e[0]):
        out+=vlq(tick-previous)+event;previous=tick
    out+=b'\x00\xff\x2f\x00'
    return b'MTrk'+struct.pack('>I',len(out))+out
events=[(0,b'\xff\x03\x0bPART GUITAR'),(0,b'\xff\x01\x10[ENHANCED_OPENS]')]
for start,end,pitch in [(0,576,96),(48,49,97),(96,97,97),(192,384,97),(192,576,98),(384,385,95),(576,577,100),(0,384,116),(96,97,101),(384,385,104),(192,385,103)]:
    events.extend([(start,bytes([0x90,pitch,100])),(end,bytes([0x80,pitch,0]))])
tempo=track([(0,b'\xff\x51\x03\x07\xa1\x20'),(384,b'\xff\x51\x03\x0f\x42\x40')])
# Section names live in their own EVENTS track: plain and Rock Band spellings.
names=track([(0,b'\xff\x03\x06EVENTS'),(0,b'\xff\x01\x0f[section intro]'),(384,b'\xff\x01\x0e[prc_verse_1a]')])
mid=b'MThd'+struct.pack('>IHHH',6,1,3,192)+tempo+track(events)+names
(root/'midi'/'notes.mid').write_bytes(mid)

# Running status survives a meta event (a known chart-file quirk).
folder=root/'running';folder.mkdir(exist_ok=True);shutil.copyfile(root/'chart'/'song.wav',folder/'song.wav')
payload=b'\x00\xff\x03\x0bPART GUITAR\x00\x90\x60\x64\x01\x60\x00\x2f\xff\x01\x01x\x00\x61\x64\x01\x61\x00\x00\xff\x2f\x00'
(folder/'notes.mid').write_bytes(b'MThd'+struct.pack('>IHHH',6,1,2,192)+tempo+b'MTrk'+struct.pack('>I',len(payload))+payload)

# SysEx open conversion and inclusive tap end; no enhanced-open text needed.
folder=root/'sysex';folder.mkdir(exist_ok=True);shutil.copyfile(root/'chart'/'song.wav',folder/'song.wav')
def sysex(d,kind,on):return b'\xf0\x08PS\x00\x00'+bytes([d,kind,on,0xf7])
ev=[(0,b'\xff\x03\x0bPART GUITAR'),(0,sysex(3,1,1)),(0,sysex(255,4,1)),(0,b'\x90\x60\x64'),(1,b'\x80\x60\x00'),(192,sysex(3,1,0)),(192,sysex(255,4,0)),(192,b'\x90\x61\x64'),(193,b'\x80\x61\x00')]
(folder/'notes.mid').write_bytes(b'MThd'+struct.pack('>IHHH',6,1,2,192)+tempo+track(ev))

folder=root/'truncated';folder.mkdir(exist_ok=True);shutil.copyfile(root/'chart'/'song.wav',folder/'song.wav');(folder/'notes.mid').write_bytes(mid[:-10])

# Audio decode fixtures are generated only when ffmpeg is available.
if shutil.which('ffmpeg'):
    for ext,codec in [('ogg','libvorbis'),('opus','libopus'),('mp3','libmp3lame'),('flac','flac')]:
        subprocess.run(['ffmpeg','-v','error','-y','-i',str(root/'chart'/'song.wav'),'-c:a',codec,str(root/f'audio.{ext}')],check=True)
print(f'Fixtures generated in {root}')
