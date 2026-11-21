"""Joint decisions below arithmetic, checked against independent fixed-width C."""
import json


def check_predicates(root, solver, directory, run, msx, backend=None):
    source=directory/'predicates.vx'
    source.write_text('''
&distance(v:#i8,dt:#u8)->#i16 {
    w:#i16=(#i16)v;
    dt==0 ? (#i16)0 : (dt==1 ? w : (dt==2 ? w<<1 :
        (dt==3 ? w+(w<<1) : w*(#i16)dt)))
}
[[c_binding(travel,full,1,exclusive)]]
&^travel(x:#u16,y:#u16,vi:#i8,vj:#i8,dt:#u8) {
    (x+(#u16)distance(vi,dt),y+(#u16)distance(vj,dt),x)
}
[[c_binding(words,full,1,exclusive)]]
&^words(flag:#u8,a:#u32,b:#u32,s:#i16) {
    (a+(flag!=0 ? b : (#u32)65535),
     b^(flag!=0 ? a : (#u32)2147483648),
     s+(flag!=0 ? (#i16)-32768 : (#i16)32767),flag)
}
[[c_binding(narrow,full,1,exclusive)]]
&^narrow(w:#u16,x:#u16) {
    (w+((#u8)w==1 ? (#u16)7 : (#u16)9),
     x+((#u8)w==1 ? (#u16)11 : (#u16)13),w)
}
''')
    client='''
#include "vx_bind_travel.h"
#include "vx_bind_words.h"
#include "vx_bind_narrow.h"
uint32_t check_predicates(void) {
    uint16_t sample,k,dt,t,x,y,expected_x,expected_y;
    int16_t vi,vj;
    static const uint8_t times[]={0,1,2,3,7,255};
    static const uint32_t words[]={0,1,255,65535,65536,2147483647UL,2147483648UL,4294967295UL};
    static const int16_t signed_words[]={-32768,-1,0,1,32767};
    VX_BIND_travel_LOCAL;VX_BIND_words_LOCAL;VX_BIND_narrow_LOCAL;
    for(sample=0;sample<256;++sample)for(k=0;k<
#ifdef __SDCC
        sizeof(times)
#else
        256
#endif
        ;++k) {
#ifdef __SDCC
        dt=times[k];
#else
        dt=k;
#endif
        vi=(int16_t)sample-128;vj=127-(int16_t)sample;
        x=(uint16_t)((uint32_t)sample*257+(uint32_t)dt*997);y=(uint16_t)(65535u-x);
        expected_x=x;expected_y=y;
        for(t=0;t<dt;++t){expected_x=(uint16_t)(expected_x+vi);expected_y=(uint16_t)(expected_y+vj);}
        vx_bind_travel_frame.a0=x;vx_bind_travel_frame.a1=y;
        vx_bind_travel_frame.a2=(int8_t)vi;vx_bind_travel_frame.a3=(int8_t)vj;
        vx_bind_travel_frame.a4=(uint8_t)dt;vx_bind_travel_run();
        if(vx_bind_travel_frame.result.v0!=expected_x || vx_bind_travel_frame.result.v1!=expected_y ||
           vx_bind_travel_frame.result.v2!=x)return 1UL+256UL*sample+dt;
    }
    for(sample=0;sample<8;++sample)for(k=0;k<8;++k)for(t=0;t<2;++t) {
        uint32_t a=words[sample],b=words[k];uint8_t index=(uint8_t)(sample+k);int16_t s;
        if(index>=10)index-=10;if(index>=5)index-=5;s=signed_words[index];
        vx_bind_words_frame.a0=(uint8_t)(7*t);vx_bind_words_frame.a1=a;
        vx_bind_words_frame.a2=b;vx_bind_words_frame.a3=s;vx_bind_words_run();
        if(vx_bind_words_frame.result.v0!=(uint32_t)(a+(t?b:65535UL)) ||
           vx_bind_words_frame.result.v1!=(b^(t?a:2147483648UL)) ||
           (uint16_t)vx_bind_words_frame.result.v2!=(uint16_t)((int32_t)s+(t?-32768L:32767L)) ||
           vx_bind_words_frame.result.v3!=7*t)return 100000UL+100*sample+10*k+t;
        y=(uint16_t)((sample<<8)|k);x=(uint16_t)b;
        vx_bind_narrow_frame.a0=y;vx_bind_narrow_frame.a1=x;vx_bind_narrow_run();
        if(vx_bind_narrow_frame.result.v0!=(uint16_t)(y+(k==1?7:9)) ||
           vx_bind_narrow_frame.result.v1!=(uint16_t)(x+(k==1?11:13)) ||
           vx_bind_narrow_frame.result.v2!=y)return 200000UL+100*sample+10*k+t;
    }
    return 0;
}
'''
    for limit,budget in ((limit,budget) for limit in (0,8) for budget in (0,64)):
        generated=directory/f'predicates-{limit}-{budget}'
        report=json.loads(run([solver,source,'--prefer',f'value_choice_branches={limit}',
                              '--prefer',f'value_frame_bytes={budget}','--emit-c',generated]).stdout)
        for binding in report['value_bindings']['bindings']:
            code=binding['codegen'];count=code['predicate_branches']
            assert (count>0) if limit else count==0,binding
            assert code['shared_select_branches']+count<=limit,binding
            assert binding['storage']==('fixed' if budget else 'automatic'),binding
        host=directory/f'predicates-{limit}-{budget}.c';binary=directory/f'predicates-host-{limit}-{budget}'
        host.write_text(client+'\n#include <stdio.h>\nint main(void){uint32_t r=check_predicates();'
                        'if(r)printf("predicate mismatch %lu\\n",(unsigned long)r);return r!=0;}\n')
        run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
             '-I'+str(generated),'-I'+str(root/'backends/msx-neo8/runtime'),host,
             generated/'vx_binding_frames.c','-o',binary]);run([binary])
        if msx:
            project=directory/f'predicates-project-{limit}-{budget}'
            backend.package(generated,project,source,solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(client+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_predicates();__asm halt __endasm;}
''')
            run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=400000000',
                 'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --max-allocs-per-node 2000'])
            observation=json.loads((project/'build/execution.json').read_text())['observation']
            assert observation['fault']==observation['last_result']==0,observation
    print('Joint predicates: step interpreter, signed velocities, word/dword wrap and invariant outputs agree'+
          (' on host and Z80' if msx else ' (UBSan; every byte velocity/duration pair)'))
