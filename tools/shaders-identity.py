# Byte-identity check for the .hlsl -> header move (brief CLOUD-1 "shaders").
# Parses an OLD src/shaders.h (hand-split raw-string literals) and the header
# cmake/embed_hlsl.cmake generated, concatenates the raw-string pieces per
# source, and compares them byte for byte. CRLF in either file is read as LF,
# which is what a raw string in a CRLF file compiles to. No GPU, no build.
#
# Usage: python tools/shaders-identity.py <old shaders.h> <generated shaders_gen.h> [piece_max]
#   old: e.g. `git show 35cffa4:src/shaders.h > old_shaders.h`
#   generated: build2/generated/shaders_gen.h (or run the generator by hand:
#     cmake -DIN_DIR=src/shaders -DOUT=gen.h -DSHADERS=compute,display,post,gradient
#           -P cmake/embed_hlsl.cmake)
# Exit 0 = every source IDENTICAL and every piece <= piece_max (default 16000).
import hashlib, re, sys

VARS = ['kComputeSrc', 'kDisplaySrc', 'kPostSrc', 'kGradientSrc']

def pieces(path):
    t = open(path, 'rb').read().decode('utf-8').replace('\r\n', '\n')
    out = {}
    for m in re.finditer(r'static const char\s*\*\s*(\w+)\s*=', t):
        # the variable ends at the first )hlsl" followed by ';' (a ';' inside
        # the HLSL itself never follows the terminator)
        e = re.compile(r'\)hlsl"\s*;').search(t, m.end()).end()
        out[m.group(1)] = re.findall(r'R"hlsl\((.*?)\)hlsl"', t[m.end():e], re.S)
    return out

old, gen = sys.argv[1], sys.argv[2]
cap = int(sys.argv[3]) if len(sys.argv) > 3 else 16000
po, pg = pieces(old), pieces(gen)
ok = True
for v in VARS:
    if v not in po or v not in pg:
        print('%-13s MISSING (old %s, generated %s)' % (v, v in po, v in pg)); ok = False; continue
    a = ''.join(po[v]).encode('utf-8'); b = ''.join(pg[v]).encode('utf-8')
    big = max(len(p.encode('utf-8')) for p in pg[v])
    same = a == b
    ok &= same and big <= cap
    print('%-13s old %6d B in %2d pieces | generated %6d B in %2d pieces, largest %5d | md5 %s %s | %s'
          % (v, len(a), len(po[v]), len(b), len(pg[v]), big,
             hashlib.md5(a).hexdigest()[:8], hashlib.md5(b).hexdigest()[:8],
             'IDENTICAL' if same else 'DIFFERS'))
    if not same:
        n = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
        print('   first difference at byte %d: old %r / generated %r' % (n, a[n:n+40], b[n:n+40]))
    if big > cap:
        print('   a generated piece is %d bytes > %d' % (big, cap))
extra = sorted(set(pg) - set(VARS))
if extra: print('generated header has unexpected sources:', extra); ok = False
print('shaders-identity: ' + ('PASS' if ok else 'FAIL'))
sys.exit(0 if ok else 1)
