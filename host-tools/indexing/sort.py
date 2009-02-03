#!/usr/bin/env python

"""
A better sort utility... better because:
    - It ignores the file offset/pointer
    - It prefers shorter matches
   
(not better because it is not memory efficient)
"""

import sys

lines = []

def text_cmp(_a,_b):
    a = _a[0].upper()
    b = _b[0].upper()

    if a == b:
        return 0
    if a < b:
        return -1
    if a > b:
        return 1

for line in sys.stdin.readlines():
    # take a line: 'Alina Kabaieva 6b6318\n'
    # and split into ('Alina Kabaieva', '6b6318')
    split = line.rsplit(' ', 1)
    lines.append((split[0], split[1][:-1]))

lines.sort(text_cmp)
for line in lines:
    print "%s %s" % (line[0], line[1])