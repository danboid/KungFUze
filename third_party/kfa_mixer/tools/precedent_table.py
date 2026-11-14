"""2-bit precedent machine copied from MegaBomber/tools/build_adpcm4_luts.py.
Preserves its exact minimized transition format.
"""
from collections import deque
def precedent_transition(state, code):
    slope, up_count, down_count, neutral_count = state
    total = 0
    for decision, divisor in enumerate((1, 2)):
        delta = slope // divisor
        up = bool(code & (1 << (1 - decision)))
        if up:
            up_count += 1; down_count = 0; total += delta
        else:
            down_count += 1; up_count = 0; total -= delta
    neutral_count += 1
    if neutral_count == 4:
        up_count = down_count = neutral_count = 0
        if slope > 2: slope -= 1
    if up_count == 2 or down_count == 2:
        up_count = down_count = neutral_count = 0
        if slope < 16: slope += 1
    return (slope, up_count, down_count, neutral_count), total & 0xff


def quotient_aux():
    init = (2, 0, 0, 0); symbols = 4
    seen = {init}; q = deque([init])
    while q:
        s = q.popleft()
        for c in range(symbols):
            n, _ = precedent_transition(s, c)
            if n not in seen: seen.add(n); q.append(n)
    states = list(seen); index = {s:i for i,s in enumerate(states)}
    part = [0] * len(states)
    while True:
        sig_to_part = {}; new = []
        for s in states:
            sig = tuple((precedent_transition(s,c)[1], part[index[precedent_transition(s,c)[0]]]) for c in range(symbols))
            if sig not in sig_to_part: sig_to_part[sig] = len(sig_to_part)
            new.append(sig_to_part[sig])
        if new == part: break
        part = new
    members = {}
    for s,p in zip(states,part): members.setdefault(p,[]).append(s)
    init_p = part[index[init]]
    ordered=[]; seenp={init_p}; qq=deque([init_p])
    while qq:
        p=qq.popleft(); ordered.append(p); rep=members[p][0]
        for c in range(symbols):
            np=part[index[precedent_transition(rep,c)[0]]]
            if np not in seenp: seenp.add(np); qq.append(np)
    remap={p:i for i,p in enumerate(ordered)}
    table=[]
    for p in ordered:
        rep=members[p][0]; row=[]
        for c in range(symbols):
            n,d=precedent_transition(rep,c)
            row += [d, remap[part[index[n]]]]
        table.append(row)
    return table, len(states)



def precedent_variant_transition(state,code,bits):
    """Exact Tower/dconvert1bit, or explicitly defined3bit extrapolation.
    3bit: initial slope4, three decisions (s,s/2,s/4), neutral3, run>=3,
    slope floor3/ceiling20, increase1. Modulo accumulator, LSB symbols.
    This is not dconvert transform8's experimental two-tap predictor.
    """
    slope,up,down,neutral=state;delta=0
    for decision in range(bits):
        value=slope>>decision
        if code&(1<<(bits-1-decision)):delta+=value;up+=1;down=0
        else:delta-=value;down+=1;up=0
    neutral+=1
    if neutral==(6 if bits==1 else 3):
        up=down=neutral=0
        if slope>(0 if bits==1 else 3):slope-=1
    run=(up==3 or down==3)if bits==1 else(up>=3 or down>=3)
    if run:
        up=down=neutral=0
        if slope<(16 if bits==1 else 20):slope+=2 if bits==1 else 1
    return (slope,up,down,neutral),delta&255

def quotient_variant(bits):
    init=(2 if bits==1 else 4,0,0,0);symbols=1<<bits
    transition=lambda s,c:precedent_variant_transition(s,c,bits)
    states=[init];index={init:0}
    for s in states:
        for c in range(symbols):
            n,_=transition(s,c)
            if n not in index:index[n]=len(states);states.append(n)
    part=[0]*len(states)
    while True:
        groups={};new=[]
        for s in states:
            sig=tuple((transition(s,c)[1],part[index[transition(s,c)[0]]])for c in range(symbols))
            if sig not in groups:groups[sig]=len(groups)
            new.append(groups[sig])
        if new==part:break
        part=new
    reps={}
    for i,p in enumerate(part):reps.setdefault(p,states[i])
    order=[part[0]];remap={part[0]:0}
    for p in order:
        for c in range(symbols):
            n,_=transition(reps[p],c);np=part[index[n]]
            if np not in remap:remap[np]=len(order);order.append(np)
    rows=[]
    for p in order:
        row=[]
        for c in range(symbols):
            n,d=transition(reps[p],c);row.extend((d,remap[part[index[n]]]))
        rows.append(row)
    assert len(rows)<=256,(bits,len(rows))
    return rows,len(states)
