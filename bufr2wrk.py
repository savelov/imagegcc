#!/usr/bin/env python3
"""
bufr2wrk.py  -  Pure-Python replacement for debufr.exe (ASPD radar BUFR decoder).

Reads an ASPD-system BUFR radar message and writes the internal working files
that debufr.exe produced under wine.  No wine / Windows binary required.

Products (identified by the IPRNnn heading in the text envelope; the pixel
transform is keyed off the image descriptor in section 3):

  IPRN   product   descriptor  output file(s)
  40-54  Z00-Z14   021001      4_dbz_1.wrk .. 4_dbz_15.wrk   (reflectivity dBZ)
  56-65  Zdif01-10 021003      4_dif_1.wrk .. 4_dif_10.wrk   (differential refl.)
  70     S         021022      4_myavl.wrk + 4_storm.wrk     (phenomena)
  71     H         021021*     4_heigh.wrk                   (echo-top height)
  72     R         021036      4_dbz_0.wrk                   (rain rate)
  73-77  Q1/3/6/12/24 013019-23  1_summ.wrk .. 5_summ.wrk   (precip sums)
  78-87  Vel01-10  021014      4_vel_1.wrk .. 4_vel_10.wrk   (radial velocity)
  * H applies Table-C operators 201135/202130 to widen/rescale 021021.
  * Some precip-sum files overrun section 4 (a debufr bug) -> debufr and
    bufr2wrk both write nothing for those.
  * The rain rate and the precip sums deviate from debufr deliberately: they
    get a dedicated no-data byte instead of a saturated reading (RAIN_Q_MISSING,
    see t_rain/t_q).  t_vel deviates the same way and for the same reason.

Each map file is an 8-byte header + a 100*100 grid of bytes; header.wrk is a
128-byte metadata block.  Field names/offsets follow the AKSOPRI HEADER.WRK and
map-file passports (see AKSOPRI_PASSPORT).  The image is run-length encoded:
delayed *repetition* (031012 = value stored once, repeated N times) interleaved
with delayed *replication* (031002 = N distinct values).

Reverse-engineered from debufr.exe v14.1 and validated byte-exact against its
output for every sample in bufr_oper/.  Pixel value tables (_DIF/_DBZ and the
rain/precip run-length tables) were extracted by feeding debufr synthetic
messages that sweep the full input range.
"""
import sys, os, struct, bisect, datetime

# ---- Table B: descriptor -> (name, unit, scale, ref, bits)
TB = {
 (0,1,18):("shortname","CCITTIA5",0,0,40),
 (0,1,15):("name","CCITTIA5",0,0,160),
 (0,1,19):("name","CCITTIA5",0,0,256),      # long site name (alt radar type)
 (0,5,2):("lat","NUM",2,-9000,15),
 (0,6,2):("lon","NUM",2,-18000,16),
 (0,7,1):("station_height","NUM",0,-400,15),
 (0,12,4):("d012004","NUM",1,0,12),
 (0,8,1):("vss","NUM",0,0,7),
 (0,7,2):("height_alt","NUM",-1,-40,16),
 (0,19,5):("d019005","NUM",0,0,9),
 (0,19,6):("d019006","NUM",2,0,14),
 (0,29,1):("projection","NUM",0,0,3),
 (0,30,31):("picture_type","NUM",0,0,4),
 (0,30,21):("pix_per_row","NUM",0,0,12),
 (0,30,22):("pix_per_col","NUM",0,0,12),
 (0,5,33):("pixsize1","NUM",-1,0,16),
 (0,6,33):("pixsize2","NUM",-1,0,16),
 (0,10,40):("d010040","NUM",0,0,10),
 (0,7,6):("height_above_stn","NUM",0,0,15),
 (0,8,7):("dim_sig","NUM",0,0,4),
 (0,31,2):("repl_ext","NUM",0,0,16),
 (0,31,12):("rep_ident","NUM",0,0,16),
 (0,21,3):("zdif","NUM",1,-5,7),
 (0,21,1):("z","NUM",0,-64,7),
 (0,21,36):("rr","NUM",7,0,12),
 (0,21,14):("vel","NUM",1,-4096,13),
 (0,21,21):("ht","NUM",-3,0,4),
 (0,13,19):("q","NUM",1,-1,14),      # Q1 precip sum; 013020..023 = Q3/Q6/Q12/Q24
 (0,13,20):("q3","NUM",1,-1,14),
 (0,13,21):("q6","NUM",1,-1,14),
 (0,13,22):("q12","NUM",1,-1,14),
 (0,13,23):("q24","NUM",1,-1,14),
 (0,21,22):("phenom","NUM",0,0,5),
 (0,13,55):("ri","NUM",4,0,8),            # rain intensity (DMRL rain product)
 # date/time + period descriptors used by the DMRL rain layout
 (0,4,1):("yr","NUM",0,0,12),(0,4,2):("mon","NUM",0,0,4),(0,4,3):("day","NUM",0,0,6),
 (0,4,4):("hr","NUM",0,0,5),(0,4,5):("mi","NUM",0,0,6),(0,4,32):("tperiod","NUM",0,0,6),
 (0,8,21):("tsig","NUM",0,0,5),
}
TD = {(3,1,24):[(0,5,2),(0,6,2),(0,7,1)]}
IMAGE_DESCRIPTORS = {(0,21,3),(0,21,1),(0,21,36),(0,21,14),(0,21,21),
                     (0,13,19),(0,13,20),(0,13,21),(0,13,22),(0,13,23),(0,21,22),(0,13,55)}

# ======== pixel value tables (extracted from debufr, validated exact) ========
_DIF = [0,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,35,35,37,37,39,40,40,42,42,44,45,45,47,48,49,49,51,52,53,54,54,56,57,58,59,59,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121]
_DBZ = [0,0,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,4,7,10,13,16,19,22,25,28,31,34,37,40,43,46,49,52,55,58,61,64,67,70,73,76,79,82,85,88,91,94,97,100,103,106,109,112,115,118,121,124,127,130,133,136,139,142,145,148,151,154,157,160,163,166,169,172,175,178,181,184,254]
# rain (021036) and precip (013019): monotone step LUTs stored as (threshold,value) runs
_RAIN_TH = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,28,29,30,32,33,35,37,38,40,42,44,46,49,51,54,56,59,62,65,68,71,75,78,82,86,90,95,99,104,109,115,120,126,132,139,146,153,160,168,176,185,194,204,214,224,235,247,259,271,285,299,313,329,345,362,379,398,418,438,459,482,506,531,557,584,613,643,674,707,742,778,817,857,899,943,989,1038,1089,1142,1199,1257,1319,1384,1452,1523,1598,1677,1759,1845,1936,2031,2131,2236,2345,2461,2582,2708,2841,2981,3128,3281,3442,3612,3789,3975]
_RAIN_VAL = [0,48,62,71,77,81,85,88,91,94,96,98,100,101,103,104,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221]
_Q_TH = [0,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,32,33,34,35,36,38,39,40,42,43,45,47,48,50,52,54,56,58,60,62,64,67,69,72,74,77,80,83,86,89,92,95,99,102,106,110,114,118,122,127,131,136,141,146,152,157,163,169,175,182,188,195,202,210,217,225,233,242,251,260,269,279,290,300,311,322,334,346,359,372,386,400,415,430,446,462,479,496,514,533,553,573,594,616,638,662,686,711,737,764,792,821,851,882,914,948,983,1019,1056,1095,1135,1176,1219,1264,1310,1358,1408,1460,1513,1568,1626,1685,1747,1811,1877,1946,2017,2091,2168,2247,2330,2415,2503,2595,2690,2789,2891,2997,3106,3220,3338,3460,3587,3719,3855,3996,4142,4294,4451,4614,4783,4959,5140,5328,5524,5726,5936,6153,6379,6612,6854,7105,7366,7636,7915,8205,8506,8817,9140,9822,10182,10555,10942,11342,11758,12189,12635,13098,13578,14075,14591,15125,15679,16254]
_Q_VAL = [0,19,31,39,45,50,54,58,61,64,67,69,71,73,75,77,79,80,82,83,85,86,87,88,89,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14]
# IPRN70 phenomena code -> byte (codes 0..19; 20..31 undefined -> 254)
_STORM = [0,3,3,5,10,10,15,15,17,17,25,35,40,45,45,50,50,60,60,60]

# header.wrk feature-motion sentinels for missing/undefined motion.  These match
# the production vector chain (vector_wms.wsgi filters `distance>0 and
# bearing!=511`, where distance = velocity/3.6 and bearing = azimuth): a missing
# vector must have velocity 0 (=> distance 0) AND azimuth 511 (debufr's native
# missing marker, 0x01FF).  Applied uniformly to MRL-5, AMRK and RUUL layouts.
SPEED_MISSING = 0        # byte 112  -> distance 0
AZIM_MISSING  = 511      # bytes 113-114 as 16-bit BE (0x01FF); == bearing!=511 marker
# BUFR "all-ones" missing markers in the two source encodings:
_D019005_MISSING = 511   # 9-bit  (MRL-5 direction of motion)
_D019006_MISSING = 16383 # 14-bit (MRL-5 speed of motion)
_S2AZ_MISSING    = 0xFFFF # AMRK section-2 azimuth word

def _rle(th, val, raw):
    i=bisect.bisect_right(th, raw)-1
    return val[i] if i>=0 else val[0]

def t_dif(r):  return _DIF[r] if 0<=r<128 else 0
def t_dbz(r, konst=0):
    if not 0<=r<128: return 0
    # per-station dBZ calibration: konst is added to any echo (raw>=2) before the
    # palette lookup, clamped at the top (raw 127 -> 254).  raw 0/1 = no echo.
    return _DBZ[r] if r<2 else _DBZ[min(r+konst,127)]
# The rain rate and the precipitation sums are the two products with no missing
# marker of their own: debufr's run length tables simply saturate, so a point
# with no reading came out as a reading.  For the rain rate that is 221, which
# is 1431 mm/h; for the sums the table wraps past 254 and the missing value
# lands on 14, which is 0.12 mm and entirely plausible.  Neither can be told
# apart from data downstream, and the map viewer painted both as weather.
#
# Give them a dedicated byte the way t_vel already does.  255 is free in both
# tables - t_rain tops out at 221 and t_q at 254 - and is what the viewer
# treats as "no data" whatever the product.  254 would have been the more usual
# choice, but t_q reaches it for a genuine 914..982 mm band.
#
# This is a deliberate departure from debufr, like the one in t_vel: files
# written before it carry the saturated values instead, and the viewer still
# folds those (see maps[] in files.c).
RAIN_Q_MISSING = 255

def t_rain(r, width=12):
    if r==(1<<width)-1: return RAIN_Q_MISSING
    return _rle(_RAIN_TH,_RAIN_VAL,r)
def t_q(r, width=14):
    if r==(1<<width)-1: return RAIN_Q_MISSING
    return _rle(_Q_TH,_Q_VAL,r)
def t_height(r, scale=-1, width=11):
    # echo-top height in hectometres.  BUFR physical value = r * 10**(-scale)
    # metres (scale from the 202 operator); output = physical // 100.  The
    # all-ones (missing) value maps to 254.
    if r==(1<<width)-1: return 254
    return (r*10**(-scale)//100)&0xff
def t_vel(r):
    # radial velocity: byte = 2*v + 127 where v = (r-4096)/10 m/s.
    # r==0 is source no-echo -> 0.  r==8191 (13-bit all-ones) is the BUFR
    # "missing" value that fills points beyond the CAPPI radius; give it a
    # DEDICATED no-data byte 255 (debufr would wrap it to 178, which collides
    # with a real 25.5 m/s reading).  255 is otherwise free because the ramp's
    # own 255 is remapped to 254.
    if r==0: return 0
    if r==8191: return 255
    v=int(0.2*(r-4096)+127)&0xff
    return 254 if v==255 else v
def t_myavl(c): return c if 0<=c<len(_STORM) else 254
def t_storm(c): return _STORM[c] if 0<=c<len(_STORM) else 254

# ---- product routing: IPRN number -> (transform key, [output filenames])
def product_for(iprn, image_desc):
    if iprn is not None:
        if 40<=iprn<=54: return "dbz",  ["4_dbz_%d.wrk"%(iprn-39)]
        if 56<=iprn<=65: return "dif",  ["4_dif_%d.wrk"%(iprn-55)]
        if iprn==70:     return "phenom",["4_myavl.wrk","4_storm.wrk"]
        if iprn==71:     return "height",["4_heigh.wrk"]
        if iprn==72:     return "rain", ["4_dbz_0.wrk"]
        if 73<=iprn<=77: return "q",    ["%d_summ.wrk"%(iprn-72)]  # Q1/Q3/Q6/Q12/Q24
        if 78<=iprn<=87: return "vel",  ["4_vel_%d.wrk"%(iprn-77)]
    # fallback by descriptor when IPRN is unknown
    fb={(0,21,3):("dif",["4_dif.wrk"]),(0,21,1):("dbz",["4_dbz.wrk"]),
        (0,21,36):("rain",["4_dbz_0.wrk"]),(0,21,14):("vel",["4_vel.wrk"]),
        (0,21,21):("height",["4_heigh.wrk"]),(0,21,22):("phenom",["4_myavl.wrk","4_storm.wrk"])}
    for d in ((0,13,19),(0,13,20),(0,13,21),(0,13,22),(0,13,23)):
        fb[d]=("q",["1_summ.wrk"])
    return fb.get(image_desc,(None,None))

# =============================== BUFR decode ===============================
def _u(b):
    v=0
    for x in b: v=(v<<8)|x
    return v

class BitReader:
    def __init__(self,buf): self.buf=buf; self.pos=0
    def read(self,n):
        v=0
        for _ in range(n):
            v=(v<<1)|((self.buf[self.pos>>3]>>(7-(self.pos&7)))&1); self.pos+=1
        return v

def read_cfg_tme(infile=None):
    """LST-GMT shift from DEBUFR.CFg 'TME <n>'.  Search the current directory
    (as debufr does), then the input file's directory, then this script's dir."""
    cands=["DEBUFR.CFg"]
    if infile: cands.append(os.path.join(os.path.dirname(infile) or ".","DEBUFR.CFg"))
    cands.append(os.path.join(os.path.dirname(os.path.abspath(__file__)),"DEBUFR.CFg"))
    for path in cands:
        try:
            with open(path,"rb") as f:
                for raw in f:
                    line=raw.split(b';',1)[0].split()
                    if len(line)>=2 and line[0].upper()==b"TME":
                        return int(line[1])
        except OSError:
            continue
    return 0

def read_iprn(data):
    """IPRN number from the text envelope heading, or None."""
    env=data[:data.find(b"BUFR")]
    for tok in env.replace(b"\r",b" ").replace(b"\n",b" ").split():
        if tok.startswith(b"IPRN") and tok[4:].isdigit():
            return int(tok[4:])
    return None

def read_station_code(data):
    """Station code (e.g. b'RUSP') from the envelope heading 'IPRNnn CODE DDHHMM'."""
    env=data[:data.find(b"BUFR")]
    toks=env.replace(b"\r",b" ").replace(b"\n",b" ").split()
    for i,tok in enumerate(toks):
        if tok.startswith(b"IPRN") and i+1<len(toks):
            return toks[i+1]
    return None

def _cfg_paths(infile):
    p=["DEBUFR.CFg"]
    if infile: p.append(os.path.join(os.path.dirname(infile) or ".","DEBUFR.CFg"))
    p.append(os.path.join(os.path.dirname(os.path.abspath(__file__)),"DEBUFR.CFg"))
    return p

def read_cfg_stations(infile=None):
    """Parse DEBUFR.CFg STA table -> {code: (site-name bytes CP866, konst)}.
    A station line is 'STA <code> <name> <konst> <time-border>'.  debufr uses
    this name in header.wrk (for stations listed here) and applies <konst> as a
    per-station dBZ reflectivity calibration offset."""
    for path in _cfg_paths(infile):
        try:
            table={}
            with open(path,"rb") as f:
                for raw in f:
                    line=raw.split(b';',1)[0].split()
                    if len(line)>=3 and line[0].upper()==b"STA":
                        nums=[t for t in line[3:] if t.lstrip(b'-').isdigit()]
                        konst=int(nums[0]) if nums else 0
                        table[line[1]]=(line[2], konst)
            if table: return table
        except OSError:
            continue
    return {}

def decode_bufr(path):
    data=open(path,"rb").read()
    iprn=read_iprn(data)
    stcode=read_station_code(data)
    p=data.find(b"BUFR")
    if p<0: raise ValueError("no BUFR message found")
    p+=8
    s1len=_u(data[p:p+3]); sec1=data[p:p+s1len]; flag=sec1[7]; p+=s1len
    obs=dict(year=sec1[12], month=sec1[13], day=sec1[14], hour=sec1[15], minute=sec1[16])
    s2mov=None
    if flag & 0x80:                               # optional (local ASPD) section 2
        s2len=_u(data[p:p+3]); s2=data[p:p+s2len][4:]   # payload after 4-byte header
        # words are 16-bit LE; word 6 = feature speed [km/h], word 7 = direction
        # of motion [deg] (correlated against 019005/019006 across MRL-5 files).
        if len(s2)>=16:
            s2mov=(s2[12]|(s2[13]<<8), s2[14]|(s2[15]<<8))
        p+=s2len
    s3len=_u(data[p:p+3]); sec3=data[p:p+s3len]
    raw=[]; q=7
    while q+1<s3len:
        raw.append((sec3[q]>>6, sec3[q]&0x3f, sec3[q+1])); q+=2
    p+=s3len
    s4len=_u(data[p:p+3]); sec4=data[p:p+s4len]
    bits=BitReader(sec4[4:])

    prog=[]
    for d in raw: prog.extend(TD.get(d,[d]))
    image_desc=next((d for d in prog if d in IMAGE_DESCRIPTORS), None)
    longname=(0,1,19) in prog             # alt (DMRL) message layout

    fields={}; pixels=[]; op={"w":0,"s":0}  # Table-C width/scale deltas (201/202)
    def elem(d):
        name,unit,scale,ref,nb=TB[d]
        if unit=="CCITTIA5":
            return b"".join(bytes([bits.read(8)]) for _ in range(nb//8))
        return bits.read(nb+op["w"])
    def store(d,v): fields.setdefault(TB[d][0],[]).append(v)
    def walk(program, sink):
        i=0
        while i<len(program):
            d=program[i]; F,X,Y=d
            if F==2:                              # Table-C operator
                if   X==1: op["w"]=(Y-128) if Y else 0   # change data width
                elif X==2: op["s"]=(Y-128) if Y else 0   # change scale
                elif X==5:                        # 205YYY: inline YYY-char text
                    txt=b"".join(bytes([bits.read(8)]) for _ in range(Y))
                    fields.setdefault("text",[]).append(txt)
                i+=1; continue
            if F==1:                              # replication / repetition
                factor=program[i+1]; fv=elem(factor); store(factor,fv)
                block=program[i+2:i+2+X]
                if factor in ((0,31,11),(0,31,12)):
                    # repetition: value stored once, emitted fv times.  debufr
                    # reads the value even when fv==0 (matched byte-exact); the
                    # Q6/Q12/Q24 precip sums abuse this and overrun section 4,
                    # which is exactly why debufr itself errors out on them.
                    sub=[]; walk(block,sub)
                    for _ in range(fv): sink.extend(sub)
                else:
                    for _ in range(fv): walk(block,sink)
                i+=2+X
            else:
                v=elem(d); store(d,v)
                if d in IMAGE_DESCRIPTORS:
                    sink.append(v)
                    # the width after any 201 operator, so the transforms can
                    # recognise the all-ones "missing" value
                    fields["_imgwidth"]=[TB[d][4]+op["w"]]
                    if d==(0,21,21):      # echo-top height: effective scale & width
                        fields["_htscale"]=[TB[d][2]+op["s"]]
                        fields["_htwidth"]=[TB[d][4]+op["w"]]
                i+=1
    walk(prog,pixels)
    fields["_stcode"]=[stcode]; fields["_longname"]=[longname]
    if s2mov is not None: fields["_s2mov"]=[s2mov]   # (speed km/h, azimuth deg)
    return obs, fields, pixels, image_desc, iprn

# =============================== outputs ===============================
def map_header(fields):
    # 8-byte map-file passport: 0=Hcs section height [hm], 1=MRes cell size,
    # 2=MT map size [cells], 3=ZDD accurate-data radius [km], 4=averaging method,
    # 5-7 free.  (For the precip-sum product 1_summ.wrk bytes 3-7 instead encode
    # the summation start time/interval; zero in the available sample.)
    Hcs   = fields["height_above_stn"][-1]//100 if "height_above_stn" in fields else 0
    MRes  = fields["pixsize1"][0]//10
    MT    = fields["pix_per_row"][0]
    return bytes([Hcs, MRes, MT, 0,0,0,0,0])

_TRANSFORM = {"dif":t_dif,"dbz":t_dbz,"rain":t_rain,"q":t_q,
              "height":t_height,"vel":t_vel}

def build_maps(fields, pixels, tkey, names, konst=0):
    hdr=map_header(fields)
    if tkey=="phenom":
        return {names[0]: hdr+bytes(t_myavl(c) for c in pixels),
                names[1]: hdr+bytes(t_storm(c) for c in pixels)}
    if tkey=="dbz":
        return {names[0]: hdr+bytes(t_dbz(r,konst) for r in pixels)}
    if tkey=="height":
        sc=fields.get("_htscale",[-1])[0]; wd=fields.get("_htwidth",[11])[0]
        return {names[0]: hdr+bytes(t_height(r,sc,wd) for r in pixels)}
    if tkey=="rain" and "ri" in fields:
        # DMRL rain layout (013055): debufr does not decode it — it reports
        # "0 map elements" and writes an empty map, so match that.  (bufr2wrk
        # can read the 10000 013055 values, but there is no valid reference to
        # calibrate the palette against.)
        return {names[0]: hdr+bytes(10000)}
    fn=_TRANSFORM[tkey]
    if tkey in ("rain","q"):
        wd=fields.get("_imgwidth",[12 if tkey=="rain" else 14])[0]
        return {names[0]: hdr+bytes(fn(r,wd) for r in pixels)}
    return {names[0]: hdr+bytes(fn(r) for r in pixels)}

def _dms(centi_deg):
    # debufr converts centidegrees to deg/min/sec through 32-bit float, then
    # truncates each field.  The float rounding is observable (e.g. 43.98 ->
    # 43 deg 58' 47", not 48"), so reproduce it with float32 exactly.
    x=struct.unpack("f",struct.pack("f",centi_deg/100.0))[0]
    deg=int(x); m=(x-deg)*60.0; mn=int(m); sc=int((m-mn)*60.0)
    return deg, mn, sc

def build_header(obs, fields, tme, cfg_name=None, konst=0):
    # Byte layout per the AKSOPRI HEADER.WRK passport (128 bytes, offsets decimal).
    h=bytearray(128)
    # 0-4  local date/time = BUFR observation time + TME shift.  The shift can
    # roll the day (and month/year) over, so use real date arithmetic.
    lt=datetime.datetime(2000+obs["year"],obs["month"],obs["day"],
                         obs["hour"],obs["minute"])+datetime.timedelta(hours=tme)
    h[0]=lt.year-2000; h[1]=lt.month; h[2]=lt.day
    # 3 local hour; 4 minute rounded down to 10-min slot
    h[3]=lt.hour; h[4]=(lt.minute//10)*10
    # 5-29 STATION - site name.  If the station is in DEBUFR.CFg, debufr writes
    # that CP866 name (zero-filled).  Otherwise: the short-name (MRL-5) layout
    # keeps the BUFR Latin name; the long-name (DMRL) layout leaves it empty.
    if cfg_name is not None:
        h[5:5+min(len(cfg_name),25)]=cfg_name[:25]
    elif not fields.get("_longname",[False])[0]:
        h[5:5+20]=fields["name"][0][:20].ljust(20)
    # 30-34 MRL-5 radar hardware parameters.  Not carried in the BUFR (debufr
    # takes them from its internal config); constant for these products/stations.
    h[30]=ord('Z')      # RadMode  : scan mode Z/F/R
    h[31]=2             # TAU      : pulse duration [us]
    h[32]=0             # A        : MRL-5 potential [dBZ] + 100
    h[33]=1             # CAN      : channel (1 = 3 cm, 2 = 10 cm)
    h[34]=202           # Rmax0    : index of last info cell in a subarray
    # 42-43 NREV (number of conical sections), RRes (radial resolution [km*10])
    h[42]=fields.get("d010040",[0])[0]     # NREV (absent in the DMRL rain layout)
    h[43]=fields["pixsize1"][0]//10
    # 47-49 DX  eastern longitude  (deg, min, sec)
    # 50-52 SY  northern latitude  (deg, min, sec)
    ln_d,ln_m,ln_s=_dms(fields["lon"][0]-18000)
    la_d,la_m,la_s=_dms(fields["lat"][0]-9000)
    h[47]=ln_d; h[48]=ln_m; h[49]=ln_s
    h[50]=la_d; h[51]=la_m; h[52]=la_s
    # 53 (passport "free" region): debufr writes the station elevation in
    # hundreds of metres, i.e. (station height + reference -400) // 100.
    h[53]=(fields["station_height"][0]-400)//100
    # 112 SPEED / 113-114 AZIM : feature (storm) advection speed [km/h] and
    # direction of motion [deg].  Sources differ by radar type -- MRL-5 carries
    # 019006/019005 in section 3; AMRK omits them (debufr drops the data) but
    # they live in local section 2 (speed word 6, azimuth word 7); RUUL carries
    # neither.  When the motion is missing/absent we write ONE consistent pair
    # (SPEED_MISSING=0 / AZIM_MISSING=511) for all layouts, matching the
    # production vector filter (distance>0 and bearing!=511).
    speed=azim=None
    if "d019006" in fields:                       # MRL-5 (section 3)
        if fields["d019006"][0]!=_D019006_MISSING and fields["d019005"][0]!=_D019005_MISSING:
            speed=(fields["d019006"][0]//100)*18//5
            azim=fields["d019005"][0]
    elif "_s2mov" in fields:                      # AMRK (section 2)
        sp,az=fields["_s2mov"][0]
        if az!=_S2AZ_MISSING:
            speed,azim=sp,az
    if speed is None:
        h[112]=SPEED_MISSING; struct.pack_into(">H",h,113,AZIM_MISSING)
    else:
        h[112]=speed&0xff; struct.pack_into(">H",h,113,azim&0xffff)
    # 121, 125, 126 : values debufr writes in the passport "free" tail
    h[121]=158
    h[125]=(fields["pix_per_row"][0]+konst)&0xff   # map-size marker + dBZ konst
    h[126]=obs["minute"]
    return bytes(h)

def _iprn_supported(iprn):
    # 74-77 (Q6/Q12/Q24) overrun section 4 and debufr writes nothing for them.
    return iprn is None or (40<=iprn<=54 or 56<=iprn<=65 or
                            iprn in (70,71,72,73,74,75,76,77) or 78<=iprn<=87)

def convert(infile, outdir=".", tme=None):
    if tme is None:
        tme=read_cfg_tme(infile)
    iprn=read_iprn(open(infile,"rb").read())
    if not _iprn_supported(iprn):        # e.g. Q6/Q12 - debufr writes nothing
        return None, None, iprn, None, []
    try:
        obs, fields, pixels, image_desc, iprn = decode_bufr(infile)
    except IndexError:                   # section-4 overrun (as debufr aborts)
        return None, None, iprn, None, []
    tkey, names = product_for(iprn, image_desc)
    if tkey is None:
        return obs, fields, iprn, None, []          # unsupported
    cfg_name,konst=read_cfg_stations(infile).get(fields["_stcode"][0],(None,0))
    written=[]
    for fname, blob in build_maps(fields, pixels, tkey, names, konst).items():
        path=os.path.join(outdir,fname)
        with open(path,"wb") as f: f.write(blob)
        written.append(path)
    hpath=os.path.join(outdir,"header.wrk")
    with open(hpath,"wb") as f: f.write(build_header(obs,fields,tme,cfg_name,konst))
    written.append(hpath)
    return obs, fields, iprn, tkey, written

def main():
    if len(sys.argv)<2:
        print("usage: bufr2wrk.py <input.buf> [output_dir]"); return 1
    infile=sys.argv[1]; outdir=sys.argv[2] if len(sys.argv)>2 else "."
    obs, fields, iprn, tkey, written = convert(infile, outdir)
    if fields is None:
        print("IPRN%s  unsupported product (debufr produces no output) - nothing written"%iprn)
        return 0
    tme=read_cfg_tme(infile)
    print("IPRN%s  station %s %s  %04d-%02d-%02d %02d:%02d (local)"%(
        iprn, fields["shortname"][0].decode(errors="replace"),
        fields["name"][0].decode(errors="replace").strip(),
        obs["year"]+2000,obs["month"],obs["day"],(obs["hour"]+tme)%24,obs["minute"]))
    if tkey is None:
        print("  unsupported product (debufr produces no output for it) - nothing written")
        return 0
    for w in written: print("  wrote",w)
    return 0

if __name__=="__main__":
    sys.exit(main())
