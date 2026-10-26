"""Fail-closed source inventory of every native C loop + scheduling contracts.

This is deliberately a reviewed-source contract, not a C termination prover.
Only service and lock-wait loops may repeat indefinitely; all other loops must
match an explicitly reviewed finite-work entry in loop_bounds12.json. New loops,
changed bodies/bounds, missing waits, callbacks draining without quotas and the
old no-argument time syscall fail. No console, network or external dependencies.
"""
import hashlib,json,pathlib,re,sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
FILES=['game/native.c','game/cellusbd_adapter.c','game/import_hook.c','game/format.c',
       'game/resolve_game.c','core/buzz.c','core/usb_model.c','core/websocket.c','net/server.c']
MANIFEST=ROOT/'plugin/tools/loop_bounds12.json'
def mask(s):
    return re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: ''.join('\n' if c=='\n' else ' ' for c in m[0]),s)
def inventory(s):
    clean=mask(s)
    token_pattern=r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\w+|\+\+|--|&&|\|\||<=|>=|==|!=|->|[^\s]'
    ts=[m for m in re.finditer(token_pattern,s) if not m[0].startswith(('/*','//'))]
    t=[m[0] for m in ts]
    def pair(i):
        close={'(':')','{':'}','[':']'}[t[i]]; depth=1; j=i+1
        while depth:
            if t[j]==t[i]: depth+=1
            if t[j]==close: depth-=1
            j+=1
        return j
    def statement(i):
        if t[i]=='{': return pair(i)
        if t[i] in ('if','for','while','switch'):
            j=statement(pair(i+1))
            if t[i]=='if' and j<len(t) and t[j]=='else': j=statement(j+1)
            return j
        if t[i]=='do': return pair(statement(i+1)+1)+1
        j=i
        while t[j]!=';':
            j=pair(j) if t[j] in ('(','{','[') else j+1
        return j+1
    funcs=[]
    for m in re.finditer(r'(?m)^(?:static\s+)?(?:[\w*]+\s+)+\**(\w+)\s*\([^;{}]*\)\s*\{',clean):
        idx=next(i for i,x in enumerate(ts) if x.start()==m.end()-1)
        funcs.append((m.start(),ts[pair(idx)-1].end(),m[1]))
    suffix=set(); result=[]; ordinal={}
    for i,word in enumerate(t):
        if word=='do':
            end_body=statement(i+1); suffix.add(end_body)
        elif word not in ('for','while') or i in suffix: continue
        if word=='do':
            end=pair(end_body+1); header=t[end_body+2:end-1]; body=t[i+1:end_body]
        else:
            h=pair(i+1); end=statement(h); header=t[i+2:h-1]; body=t[h:end]
        pos=ts[i].start(); fn=next((name for a,b,name in funcs if a<=pos<b),'macro')
        ordinal[fn]=ordinal.get(fn,0)+1
        normalized=' '.join(t[i:end])
        result.append(dict(function=fn,ordinal=ordinal[fn],kind=word,line=s.count('\n',0,pos)+1,
                           header=' '.join(header),sha256=hashlib.sha256(normalized.encode()).hexdigest(),
                           body=' '.join(body)))
    return result
def collect(sources):
    return [{**loop,'file':'plugin/'+file} for file,s in sources.items() for loop in inventory(s)]
def check(sources,review):
    loops=collect(sources)
    actual={(l['file'],l['function'],l['ordinal']):l for l in loops}
    expected={(l['file'],l['function'],l['ordinal']):l for l in review}
    assert actual.keys()==expected.keys(),'new, missing or moved loop requires finite-bound review'
    for key,l in actual.items():
        e=expected[key]
        assert l['sha256']==e['sha256'],f'{key}: loop changed; re-review mandatory'
        assert e['reason'] and e['class'] in ('service','sleeping-lock','finite'),'unclassified loop'
        if e['class'] in ('service','sleeping-lock'):
            minimum=1000000 if l['function']=='diagnostics' else 10000 if l['function']=='network' else 2000
            assert l['body'].startswith('{ sys_timer_usleep ( '+str(minimum)+' ) ;'),f'{key}: must sleep first'
            if l['function']=='diagnostics':
                assert l['body'].count('sys_timer_usleep ( 1000000 ) ; check_stop ( ) ;')==2
                assert l['body'].count('callback_watch (')==2
    native=sources['game/native.c']; clean=mask(native)
    assert 'lv2syscall0(145)' not in clean and 'sys_time_get_system_time()' in clean
    assert 'NETWORK_PRIORITY=1010' in clean and 'DIAGNOSTIC_PRIORITY=990' in clean
    assert 'sys_timer_usleep(100000)' in clean and '++g->count>1000' in clean
    for fn in ('heartbeat_line','heartbeat'):
        body=next(l for l in re.finditer(r'static void '+fn+r'\([\s\S]*?\n\}',native))[0]
        assert not re.search(r'\b(log_line|flush_trace|enter|atomic_flag_test_and_set\w*)\s*\(',mask(body)),'heartbeat depends on shared lock'
    assert 'drained<BUZZ_USB_EVENTS' in sources['game/cellusbd_adapter.c']
    assert 'frames<16' in sources['net/server.c']
    return loops
def main():
    sources={f:(ROOT/'plugin'/f).read_text() for f in FILES}
    cmake=(ROOT/'plugin/game/CMakeLists.txt').read_text()
    native_list=re.search(r'add_executable\(ps3buzz_game\s+([^)]*)\)',cmake)[1].split()
    actual_units={(ROOT/'plugin/game'/f).resolve() for f in native_list if f.endswith('.c')}
    assert actual_units=={(ROOT/'plugin'/f).resolve() for f in FILES},'native build sources changed; audit coverage must be reviewed'
    if '--inventory' in sys.argv:
        for l in collect(sources): print(l['file'],l['function'],l['ordinal'],l['kind'],l['header'])
        return
    review=json.loads(MANIFEST.read_text()); loops=check(sources,review)
    mutations=[('game/native.c','sys_timer_usleep(2000);\n  guard_loop','/* missing sleep */\n  guard_loop'),
               ('game/native.c','sys_timer_usleep(10000);','sys_timer_usleep(0);'),
               ('game/native.c','sys_timer_usleep(1000000); check_stop();','check_stop();'),
               ('game/native.c','sys_timer_usleep(2000); if(atomic_load','if(atomic_load'),
               ('game/cellusbd_adapter.c','drained<BUZZ_USB_EVENTS','1'),
               ('net/server.c','frames<16','1'),
               ('net/server.c','if(!server.started) return;','while(1) {} if(!server.started) return;'),
               ('game/native.c','return (uint64_t)sys_time_get_system_time();','lv2syscall0(145); return (uint64_t)p1;')]
    for file,before,after in mutations:
        assert before in sources[file],before
        changed=dict(sources); changed[file]=changed[file].replace(before,after,1)
        try: check(changed,review)
        except AssertionError: pass
        else: raise AssertionError('missed unsafe mutation '+before)
    report={'result':'PASS','scope':'all loops in nine native C translation units; source contract, no external-call termination proof',
            'negative_mutations_rejected':len(mutations),'loops':[{k:v for k,v in l.items() if k!='body'} for l in loops]}
    (ROOT/'build/loop_audit12.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Loop audit PASS: {len(loops)} reviewed loops; three paced services, sleeping lock; eight unsafe mutations rejected.')
if __name__=='__main__': main()
