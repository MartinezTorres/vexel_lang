"""Shared choices: compare a bulk formula with an independent tick interpreter."""
import json


def check_choices(root, solver, directory, run, msx, backend=None):
    source=directory/'choices.vx'
    source.write_text('''
[[c_binding(timeline,full,1,exclusive)]]
&^timeline(counter:#u8,elapsed:#u8) {
    room:#u8=24-counter;
    spent:#u8=elapsed<room ? elapsed : room;
    (elapsed<=room ? (#u8)1 : (#u8)0,counter+spent,
     spent>(#u8)(18-counter) ? (#u8)3 : (spent>(#u8)(12-counter) ? (#u8)2 :
     (spent>(#u8)(6-counter) ? (#u8)1 : (#u8)255)))
}
[[c_binding(widths,full,1,exclusive)]]
&^widths(x:#u16,y:#u16,flag:#u8) {
    value:#u16=flag!=0 ? y : x;
    (value+y,(#u8)value,value*(#u16)17,(#i16)value)
}
[[c_binding(ordering,full,1,exclusive)]]
&^ordering(x:#u8,y:#u8) {
    value:#u8=x<y ? x : y;
    (value+17,x+value,x<=y,x>y,x>=y,x==y,value+value+value)
}
[[c_binding(bounded_steps,full,1,exclusive)]]
&^bounded_steps(ticks:#u8,elapsed:#u8) {
    room:#u8=ticks>6 ? (#u8)6 : ticks;
    spent:#u8=elapsed<room ? elapsed : room;
    (room-spent,spent+spent+spent,room!=0 ? (#u8)2 : (#u8)1)
}
''')
    client='''
#include "vx_bind_timeline.h"
#include "vx_bind_widths.h"
#include "vx_bind_ordering.h"
#include "vx_bind_bounded_steps.h"
uint32_t check_choices(void) {
    uint16_t c,dt,k;uint8_t current,frame,live;
    static const uint16_t words[]={0,1,127,255,256,32767,32768,65502,65503,65535};
    static const uint8_t times[]={0,1,2,3,6,24,255};
    VX_BIND_timeline_LOCAL;VX_BIND_widths_LOCAL;VX_BIND_ordering_LOCAL;
    VX_BIND_bounded_steps_LOCAL;
    for(c=0;c<256;++c)for(k=0;k<
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
        current=(uint8_t)c;frame=255;live=1;
        {uint16_t tick;for(tick=0;tick<dt;++tick) {
            if(current==6)frame=1;if(current==12)frame=2;if(current==18)frame=3;
            if(current==24){live=0;break;}++current;
        }}
        vx_bind_timeline_frame.a0=(uint8_t)c;vx_bind_timeline_frame.a1=(uint8_t)dt;vx_bind_timeline_run();
        if(vx_bind_timeline_frame.result.v0!=live || vx_bind_timeline_frame.result.v1!=current ||
           vx_bind_timeline_frame.result.v2!=frame)return 1UL+256UL*c+dt;
        vx_bind_ordering_frame.a0=(uint8_t)c;vx_bind_ordering_frame.a1=(uint8_t)dt;vx_bind_ordering_run();
        current=(uint8_t)(c<dt?c:dt);
        if(vx_bind_ordering_frame.result.v0!=(uint8_t)(current+17) ||
           vx_bind_ordering_frame.result.v1!=(uint8_t)(c+current) ||
           vx_bind_ordering_frame.result.v2!=(c<=dt) || vx_bind_ordering_frame.result.v3!=(c>dt) ||
           vx_bind_ordering_frame.result.v4!=(c>=dt) || vx_bind_ordering_frame.result.v5!=(c==dt) ||
           vx_bind_ordering_frame.result.v6!=(uint8_t)(3u*current))return 100000UL+256*c+dt;
        {uint16_t tick;uint8_t displacement=0;current=(uint8_t)(c>6?6:c);
         uint8_t status=(uint8_t)(current?2:1);
         for(tick=0;tick<dt && current;++tick){--current;displacement+=3;}
         vx_bind_bounded_steps_frame.a0=(uint8_t)c;vx_bind_bounded_steps_frame.a1=(uint8_t)dt;
         vx_bind_bounded_steps_run();
         if(vx_bind_bounded_steps_frame.result.v0!=current ||
            vx_bind_bounded_steps_frame.result.v1!=displacement ||
            vx_bind_bounded_steps_frame.result.v2!=status)return 300000UL+256UL*c+dt;
        }
    }
    for(c=0;c<10;++c)for(dt=0;dt<10;++dt)for(k=0;k<3;++k) {
        uint16_t v=words[k?dt:c];
        vx_bind_widths_frame.a0=words[c];vx_bind_widths_frame.a1=words[dt];
        vx_bind_widths_frame.a2=(uint8_t)(k*7);vx_bind_widths_run();
        if(vx_bind_widths_frame.result.v0!=(uint16_t)((uint32_t)v+words[dt]) ||
           vx_bind_widths_frame.result.v1!=(uint8_t)v ||
           vx_bind_widths_frame.result.v2!=(uint16_t)((uint32_t)v*17) ||
           vx_bind_widths_frame.result.v3!=VX_VALUE_I16(v))return 200000UL+100*c+10*dt+k;
    }
    return 0;
}
'''
    # Exercise choice budgets independently, then check their composition with
    # finite search and linear collection. Either may legitimately eliminate
    # a selection that an older policy split; retain the value oracle in both.
    policies=[(limit,budget,0) for limit in (0,2,8) for budget in (0,64)]+[(8,64,16)]
    for limit,budget,table_search in policies:
        tag=f'{limit}-{budget}-{table_search}'
        generated=directory/f'choices-{tag}'
        report=json.loads(run([solver,source,'--prefer',f'value_frame_bytes={budget}',
                              '--prefer',f'value_choice_branches={limit}',
                              '--prefer',f'value_linear_terms={16 if table_search else 0}',
                              '--prefer',f'value_table_search={table_search}','--emit-c',generated]).stdout)
        for binding in report['value_bindings']['bindings']:
            splits=binding['codegen']['shared_select_branches']
            assert splits<=limit,binding
            if not table_search:
                assert (splits>0) if limit else splits==0,binding
                if binding['name']=='bounded_steps' and limit==8:assert splits>2,binding
            assert binding['storage']==('fixed' if budget else 'automatic'),binding
        host=directory/f'choices-{tag}.c';binary=directory/f'choices-host-{tag}'
        host.write_text(client+'\n#include <stdio.h>\nint main(void){uint32_t r=check_choices();'
                        'if(r)printf("choice mismatch %lu\\n",(unsigned long)r);return r!=0;}\n')
        run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
             '-I'+str(generated),'-I'+str(root/'backends/msx-neo8/runtime'),host,
             generated/'vx_binding_frames.c','-o',binary]);run([binary])
        if msx:
            project=directory/f'choices-project-{tag}'
            backend.package(generated,project,source,solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(client+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_choices();__asm halt __endasm;}
''')
            run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=400000000',
                 'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --max-allocs-per-node 2000'])
            execution=json.loads((project/'build/execution.json').read_text())
            assert execution['observation']['fault']==execution['observation']['last_result']==0,execution
    print('Shared choices: budgets 0/2/8, tick interpreter, ordering, conversions and wrapping agree'+
          (' on host and Z80' if msx else ' (UBSan; every byte pair)'))
