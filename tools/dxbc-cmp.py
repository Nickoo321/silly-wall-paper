# Compares compiled D3D shader bytecode (DXBC) between an OLD and a NEW copy of
# src\shaders.h's kDisplaySrc literal, for all three display PSOs (fluid, ink,
# liquid_acid) x both stages (VS/PS) -- used to PROVE a refactor (e.g. slot
# renumbering) changed no bytecode. Compiles both sides with d3dcompiler_47.dll
# via ctypes (no build needed) and prints byte length + md5 + IDENTICAL/DIFFERS
# per label/entry point. slots = src\acid_slots.h (its `X(name, laP#, comp)`
# rows drive the LA_* macros compiled into the acid PSO).
#
# Usage: python tools\dxbc-cmp.py <old_shaders.h> <new_shaders.h> <acid_slots.h> [<old_acid_slots.h>]
# The acid PSO needs the LA_* slot macros on BOTH sides (the old side used to get
# only LIQUID_ACID and failed with "undeclared identifier LA_*", brief BU-b fix):
# the old side uses <old_acid_slots.h> when given, else the same <acid_slots.h>.
import ctypes, re, sys, hashlib
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
def literal(path, var):
    t=open(path,'rb').read().decode('utf-8').replace('\r\n','\n')
    s=t.index('static const char* %s = '%var); e=t.index(')hlsl";',s)+len(')hlsl";')
    return ''.join(re.findall(r'R"hlsl\((.*?)\)hlsl"',t[s:e],re.S)).encode('utf-8')
old,new,slots=sys.argv[1:4]
oldSlots=sys.argv[4] if len(sys.argv)>4 else slots
def acid_defs(path):
    rows=re.findall(r'(?m)^\s*X\(\s*(\w+)\s*,\s*(\d+)\s*,\s*([xyzw])',open(path).read())
    return [('LIQUID_ACID','1')]+[('LA_'+n,'laP%s.%s'%(v,c)) for n,v,c in rows]
acidDefs=acid_defs(slots); acidDefsOld=acid_defs(oldSlots)
so,sn=literal(old,'kDisplaySrc'),literal(new,'kDisplaySrc')
print('kDisplaySrc bytes old/new',len(so),len(sn))
for label,do,dn in [('fluid',[],[]),('ink',[('INK','1')],[('INK','1')]),('acid',acidDefsOld,acidDefs)]:
    for entry,tgt in [('VSMain','vs_5_0'),('PSMain','ps_5_0')]:
        a=compile(so,entry,tgt,do); b=compile(sn,entry,tgt,dn)
        print(label,entry,len(a),len(b),'IDENTICAL' if a==b else 'DIFFERS', hashlib.md5(a).hexdigest()[:8], hashlib.md5(b).hexdigest()[:8])
