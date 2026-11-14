"""Reference encoders/decoders for the explicitly selected mixer formats.
2-bit precedent and4-bit fast16 retain MegaBomber/SFA2 compatibility.
1-bit matches TowerOfRubble/dconvert transform5.3-bit extrapolates that
precedent-scaling family; it is not dconvert transform8 or IMA/MSM5205.
All adaptive streams pack least-significant symbol/bit first, including
3-bit symbols straddling bytes. Fixed4 retains KFM's high-nibble-first layout.
"""
from megabomber_codec import adpcm4_fast16_transition, ADPCM4_FAST16_STEPS
from precedent_table import quotient_aux
FORMATS=('adpcm1','adpcm2','adpcm3','adpcm4','fixed4')
def table(codec):
 if codec=='adpcm2':return quotient_aux()[0]
 if codec in ('adpcm1','adpcm3'):
  from precedent_table import quotient_variant
  if codec=='adpcm1':
   # The exact1-bit machine has270 minimized states, so its AVR decoder
   # uses the original arithmetic counters rather than an8-bit state LUT.
   import precedent_table
   # Build reachable concrete transitions; Python has no state-width limit.
   init=(2,0,0,0);states=[init];index={init:0};rows=[]
   for state in states:
    row=[]
    for code in range(2):
     nxt,delta=precedent_table.precedent_variant_transition(state,code,1)
     if nxt not in index:index[nxt]=len(states);states.append(nxt)
     row.extend((delta,index[nxt]))
    rows.append(row)
   return rows
  return quotient_variant(3)[0]
 return [[v for code in range(16) for v in ((adpcm4_fast16_transition(state,code)[1]&255),adpcm4_fast16_transition(state,code)[0])]for state in range(16)]

def encode(samples,codec):
 samples=list(samples)
 if codec=='fixed4':
  accum=128;codes=[]
  for target in samples:
   delta=max(-7,min(7,target-accum));accum+=delta;codes.append(delta+8)
  if len(codes)&1:codes.append(8)
  return bytes((a<<4)|b for a,b in zip(codes[::2],codes[1::2]))
 if codec=='adpcm2':
  from megabomber_codec import encode_precedent
  return encode_precedent(samples,2)
 rows=table(codec);state=0;accum=128;bits=int(codec[-1]);reservoir=count=0;out=bytearray()
 for target in samples:
  row=rows[state]
  if codec=='adpcm1':code=int(accum<target) # Exact dconvert direction decision.
  else:code=min(range(1<<bits),key=lambda c:abs(((accum+row[c*2])&255)-target))
  accum=(accum+row[code*2])&255;state=row[code*2+1]
  reservoir|=code<<count;count+=bits
  while count>=8:out.append(reservoir&255);reservoir>>=8;count-=8
 if count:out.append(reservoir)
 return bytes(out)

def decode(data,samples,codec):
 accum=128;state=0;out=[]
 if codec=='fixed4':
  for i in range(samples):
   code=(data[i//2]>>(0 if i&1 else 4))&15
   if code==0:raise ValueError('Fixed delta terminator before explicit sample count')
   accum=(accum+code-8)&255;out.append(accum)
  return bytes(out)
 rows=table(codec);bits=int(codec[-1])
 for i in range(samples):
  at=i*bits;word=int.from_bytes(data[at//8:at//8+2],'little');code=(word>>(at&7))&((1<<bits)-1)
  accum=(accum+rows[state][code*2])&255;state=rows[state][code*2+1];out.append(accum)
 return bytes(out)
