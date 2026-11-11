"""Copies PKHeX's event flag and work labels (English) into romfs/flags for the Event flags editor.

PKHeX (GPLv3) keeps them in PKHeX.Core/Resources/text/script/genN/flags_<game>_en.txt and const_<game>_en.txt,
one entry per line: index, category letter (s story, e event encounter, g gift, r rebattle, h hidden item,
t trainer, f fly, a achievement, + statistic, * feature, m misc), name, and for work values the named values
("0:Not Chosen,387:Turtwig").
Usage: eventlabels.py <PKHeX checkout> <out romfs dir>"""
import os
import shutil
import sys

PKHEX, OUT = sys.argv[1], sys.argv[2]
SRC = os.path.join(PKHEX, 'PKHeX.Core', 'Resources', 'text', 'script')
GAMES = {'gen2': ['gs', 'c'], 'gen3': ['rs', 'e', 'frlg'], 'gen4': ['dp', 'pt', 'hgss'], 'gen5': ['bw', 'b2w2'],
         'gen6': ['xy', 'oras'], 'gen7': ['sm', 'usum']}
dest = os.path.join(OUT, 'flags')
os.makedirs(dest, exist_ok=True)
n = 0
for gen, games in GAMES.items():
    for g in games:
        for kind in ('flags', 'const'):
            src = os.path.join(SRC, gen, '%s_%s_en.txt' % (kind, g))
            if os.path.exists(src):
                shutil.copyfile(src, os.path.join(dest, '%s_%s.txt' % (kind, g)))
                n += 1
print(n, 'files')
