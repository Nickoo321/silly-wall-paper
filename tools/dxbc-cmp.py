# Compares compiled D3D shader bytecode (DXBC) between an OLD and a NEW copy of
# the FluidWallpaper HLSL, for EVERY PSO the app builds -- used to PROVE a
# refactor (e.g. slot renumbering, the .hlsl move) changed no bytecode.
# Compiles both sides with d3dcompiler_47.dll via ctypes (no build needed) and
# prints byte length + md5 + IDENTICAL/DIFFERS per PSO/entry point:
#   compute  every CS* entry in kComputeSrc, plus CSSplatDye with DROP_COMPACT
#   display  kDisplaySrc VS/PS as fluid, ink (INK) and liquid_acid (LIQUID_ACID
#            + the LA_* slot macros from acid_slots.h)
#   post     kPostSrc VS/PS, plain and BN_OPTICS
#   gradient kGradientSrc VS/PS
# Each side is either an old single-header `shaders.h` (hand-split raw-string
# literals) or a `src/shaders` DIRECTORY of .hlsl files (brief CLOUD-1: the
# build embeds those byte for byte, CRLF read as LF), so old-vs-new works
# across the move.
#
# Usage: python tools\dxbc-cmp.py <old> <new> <acid_slots.h> [<old_acid_slots.h>] [--display-only]
#   e.g. git show main:src/shaders.h > old_shaders.h
#        python tools\dxbc-cmp.py old_shaders.h src\shaders src\acid_slots.h
# The acid PSO needs the LA_* slot macros on BOTH sides (the old side used to get
# only LIQUID_ACID and failed with "undeclared identifier LA_*", brief BU-b fix):
# the old side uses <old_acid_slots.h> when given, else the same <acid_slots.h>.
# Exit 1 if any PSO DIFFERS or is missing on one side.
import ctypes, os, re, sys, hashlib
from ctypes import c_void_p, c_char_p, c_size_t, c_uint, POINTER, Structure, byref
d3dc = ctypes.WinDLL('d3dcompiler_47.dll')
class MACRO(Structure): _fields_=[('Name',c_char_p),('Definition',c_char_p)]
d3dc.D3DCompile.argtypes=[c_char_p,c_size_t,c_char_p,POINTER(MACRO),c_void_p,c_char_p,c_char_p,c_uint,c_uint,POINTER(c_void_p),POINTER(c_void_p)]
def blob_bytes(b):
    vt=ctypes.cast(ctypes.cast(b,POINTER(c_void_p))[0],POINTER(c_void_p))
    gp=ctypes.WINFUNCTYPE(c_void_p,c_void_p)(vt[3]); gs=ctypes.WINFUNCTYPE(c_size_t,c_void_p)(vt[4])
    return ctypes.string_at(gp(b),gs(b))
def compile(src, entry, target, defs):
    arr=(MACRO*(len(defs)+1))(*[MACRO(k.encode(),v.encode()) for k,v in defs], MACRO(None,None))
    code=c_void_p(); err=c_void_p()
    hr=d3dc.D3DCompile(src,len(src),entry.encode(),arr,None,entry.encode(),target.encode(),0,0,byref(code),byref(err))
    if hr!=0: raise SystemExit(blob_bytes(err).decode(errors='replace')[:2000])
    return blob_bytes(code)
FILES={'kComputeSrc':'compute','kDisplaySrc':'display','kPostSrc':'post','kGradientSrc':'gradient'}
def literal(path, var):
    if os.path.isdir(path):   # src/shaders: the file IS the embedded text
        return open(os.path.join(path, FILES[var]+'.hlsl'),'rb').read().replace(b'\r\n',b'\n')
    t=open(path,'rb').read().decode('utf-8').replace('\r\n','\n')
    s=t.index('static const char* %s = '%var); e=t.index(')hlsl";',s)+len(')hlsl";')
    return ''.join(re.findall(r'R"hlsl\((.*?)\)hlsl"',t[s:e],re.S)).encode('utf-8')
args=[a for a in sys.argv[1:] if not a.startswith('--')]
displayOnly='--display-only' in sys.argv
old,new,slots=args[0:3]
oldSlots=args[3] if len(args)>3 else slots
def acid_defs(path):
    rows=re.findall(r'(?m)^\s*X\(\s*(\w+)\s*,\s*(\d+)\s*,\s*([xyzw])',open(path).read())
    return [('LIQUID_ACID','1')]+[('LA_'+n,'laP%s.%s'%(v,c)) for n,v,c in rows]
acidDefs=acid_defs(slots); acidDefsOld=acid_defs(oldSlots)
bad=0
def cmp(label, var, entry, tgt, do, dn):
    global bad
    so,sn=literal(old,var),literal(new,var)
    a=compile(so,entry,tgt,do); b=compile(sn,entry,tgt,dn)
    same=a==b; bad+=not same
    print(label,entry,len(a),len(b),'IDENTICAL' if same else 'DIFFERS', hashlib.md5(a).hexdigest()[:8], hashlib.md5(b).hexdigest()[:8])
print('kDisplaySrc bytes old/new',len(literal(old,'kDisplaySrc')),len(literal(new,'kDisplaySrc')))
VSPS=[('VSMain','vs_5_0'),('PSMain','ps_5_0')]
for label,do,dn in [('fluid',[],[]),('ink',[('INK','1')],[('INK','1')]),('acid',acidDefsOld,acidDefs)]:
    for entry,tgt in VSPS: cmp(label,'kDisplaySrc',entry,tgt,do,dn)
if not displayOnly:
    for label,defs in [('post',[]),('postBN',[('BN_OPTICS','1')])]:
        for entry,tgt in VSPS: cmp(label,'kPostSrc',entry,tgt,defs,defs)
    for entry,tgt in VSPS: cmp('gradient','kGradientSrc',entry,tgt,[],[])
    eo=re.findall(rb'\bvoid\s+(CS\w+)\s*\(',literal(old,'kComputeSrc'))
    en=re.findall(rb'\bvoid\s+(CS\w+)\s*\(',literal(new,'kComputeSrc'))
    for e in sorted(set(eo)^set(en)):
        print('compute',e.decode(),'only on the','old' if e in eo else 'new','side'); bad+=1
    for e in [x for x in eo if x in en]:
        cmp('compute','kComputeSrc',e.decode(),'cs_5_0',[],[])
    cmp('computeCompact','kComputeSrc','CSSplatDye','cs_5_0',[('DROP_COMPACT','1')],[('DROP_COMPACT','1')])
print('dxbc-cmp: '+('ALL IDENTICAL' if bad==0 else '%d DIFFER'%bad))
sys.exit(1 if bad else 0)
