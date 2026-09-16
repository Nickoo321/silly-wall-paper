"""Blind sheet: shuffle variants, label A/B/C/D, write the key to a file.
usage: python blind.py out.png key.txt "name=prefix" "name=prefix" ...
"""
import sys, random, subprocess, os
out, key = sys.argv[1], sys.argv[2]
variants = [a.split('=', 1) for a in sys.argv[3:]]
random.seed()  # non-deterministic on purpose
random.shuffle(variants)
letters = 'ABCDEFGH'
args = [f'{letters[i]}={p}' for i, (n, p) in enumerate(variants)]
here = os.path.dirname(os.path.abspath(__file__))
subprocess.check_call([sys.executable, os.path.join(here, 'ab.py'), out] + args)
with open(key, 'w') as f:
    for i, (n, p) in enumerate(variants):
        f.write(f'{letters[i]} = {n}  ({p})\n')
print('key written to', key, '(do not open until the user has picked)')
