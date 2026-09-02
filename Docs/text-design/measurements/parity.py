"""Does FreeType light autohint + round(v*3/255) reproduce TouchGFX's shipped 2bpp Poppins glyphs?
For glyphs whose bitmap dimensions differ, test whether the TouchGFX bitmap is the FreeType one
with an all-zero edge row/column removed (a crop), a one-pixel shift, or something else."""
import re, glob, numpy as np, freetype, os, sys, collections
SQ="/Users/tobymurray/git/watch-apps/Squash/Software/Apps/TouchGFX-GUI/generated/fonts/src/"
F=os.path.dirname(os.path.abspath(__file__))+"/fonts/"
def hexbytes(path):
    src="\n".join(l for l in open(path).read().splitlines() if not l.strip().startswith("//"))
    return bytes(int(x,16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", src))
def table(name):
    return [(int(o),int(u,16),int(w),int(h),int(t),int(l),int(a)) for o,u,w,h,t,l,a in re.findall(r"\{\s*(\d+),\s*0x([0-9A-F]+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(\d+),", open(SQ+f"Table_Poppins_{name}_2bpp.cpp").read())]
def unpack(data,off,w,h):
    out=np.zeros((h,w),dtype=np.uint8); bit=off*8
    for y in range(h):
        for x in range(w): out[y,x]=(data[bit//8]>>(bit%8))&3; bit+=2
    return out
def ft(face,px,cp):
    f=freetype.Face(F+face); f.set_pixel_sizes(0,px)
    f.load_glyph(f.get_char_index(cp),freetype.FT_LOAD_RENDER|freetype.FT_LOAD_TARGET_LIGHT); b=f.glyph.bitmap
    cov=np.frombuffer(bytes(b.buffer),dtype=np.uint8).reshape(b.rows,b.pitch)[:,:b.width].astype(int) if b.rows else np.zeros((0,0),int)
    q=((cov*3+127)//255).astype(np.uint8)
    return q,f.glyph.bitmap_top,f.glyph.bitmap_left
def crop(q):
    ys,xs=np.nonzero(q)
    return q[ys.min():ys.max()+1, xs.min():xs.max()+1] if len(ys) else q[:0,:0]
print("freetype", freetype.version())
for name,face,px in [("Regular_16","Poppins-Regular.ttf",16),("Regular_18","Poppins-Regular.ttf",18),("SemiBold_20","Poppins-SemiBold.ttf",20),("Medium_50","Poppins-Medium.ttf",50)]:
    data=hexbytes(SQ+f"Font_Poppins_{name}_2bpp_0.cpp"); rows=table(name)
    verdict=collections.Counter(); examples=[]
    for off,u,w,h,t,l,a in rows:
        if u==0x20: continue
        q,top,left=ft(face,px,u); g=unpack(data,off,w,h)
        if q.shape==g.shape and (q==g).all(): verdict["identical"]+=1; continue
        qc=crop(q); gc=crop(g)
        if qc.shape==gc.shape and (qc==gc).all(): verdict["identical after cropping blank edges"]+=1; continue
        if qc.shape==gc.shape:
            diff=(qc!=gc).sum(); verdict["same ink box, differing pixels"]+=1; examples.append((chr(u),diff,qc.size))
        else:
            verdict[f"different ink box"]+=1; examples.append((chr(u),f"{qc.shape}vs{gc.shape}",""))
    print(name, dict(verdict), "examples:", examples[:6])
