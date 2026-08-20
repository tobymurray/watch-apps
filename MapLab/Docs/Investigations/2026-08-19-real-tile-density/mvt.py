import struct, gzip

def varint(b,i):
    v=0;s=0
    while True:
        c=b[i]; i+=1; v|=(c&0x7F)<<s
        if not c&0x80: return v,i
        s+=7

def fields(b):
    """Yield (field_number, wire_type, payload) over a protobuf message."""
    i=0; n=len(b)
    while i<n:
        key,i=varint(b,i); fn=key>>3; wt=key&7
        if wt==0:
            v,i=varint(b,i); yield fn,wt,v
        elif wt==2:
            ln,i=varint(b,i); yield fn,wt,b[i:i+ln]; i+=ln
        elif wt==5: yield fn,wt,b[i:i+4]; i+=4
        elif wt==1: yield fn,wt,b[i:i+8]; i+=8
        else: raise ValueError(f"wire type {wt}")

def zigzag(v): return (v>>1) ^ -(v&1)

def parse_geometry(buf):
    """Return (points, parts) from an MVT geometry command stream."""
    i=0; n=len(buf); pts=0; parts=0
    while i<n:
        cmd,i=varint(buf,i)
        cid=cmd&0x7; cnt=cmd>>3
        if cid in (1,2):                      # MoveTo / LineTo
            for _ in range(cnt):
                _,i=varint(buf,i); _,i=varint(buf,i)
            pts+=cnt
            if cid==1: parts+=cnt
        elif cid==7:                          # ClosePath, no params
            pass
        else: break
    return pts,parts

def parse_tile(raw):
    """-> list of dicts, one per layer."""
    out=[]
    for fn,wt,val in fields(raw):
        if fn!=3: continue
        name=None; extent=4096; feats=0; pts=0; parts=0; bytype={}
        for lfn,lwt,lval in fields(val):
            if lfn==1: name=lval.decode('utf8','replace')
            elif lfn==5: extent=lval
            elif lfn==2:
                feats+=1
                gtype=0
                for ffn,fwt,fval in fields(lval):
                    if ffn==3: gtype=fval
                    elif ffn==4:
                        p,q=parse_geometry(fval); pts+=p; parts+=q
                bytype[gtype]=bytype.get(gtype,0)+1
        out.append(dict(name=name,extent=extent,features=feats,points=pts,parts=parts,bytype=bytype))
    return out

def pm_entries(path):
    b=open(path,'rb').read()
    u64=lambda o: struct.unpack_from('<Q',b,o)[0]
    root_off,root_len=u64(8),u64(16); comp=b[97]
    raw=b[root_off:root_off+root_len]
    if comp==2: raw=gzip.decompress(raw)
    g=(lambda buf:( (yield from iter([])) ))
    def vints(buf):
        i=0
        while i<len(buf):
            v,i2=varint(buf,i); i=i2; yield v
    it=vints(raw); n=next(it)
    ids=[];last=0
    for _ in range(n): last+=next(it); ids.append(last)
    runs=[next(it) for _ in range(n)]
    lens=[next(it) for _ in range(n)]
    offs=[]
    for k in range(n):
        v=next(it)
        offs.append(offs[k-1]+lens[k-1] if (v==0 and k>0) else v-1)
    tdo=u64(56)
    return b,[(i,o,l,r) for i,o,l,r in zip(ids,offs,lens,runs)],tdo

def zoom_of(tid):
    z=0;base=0
    while True:
        c=4**z
        if tid<base+c: return z
        base+=c; z+=1
