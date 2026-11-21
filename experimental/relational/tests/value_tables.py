"""Exhaustive byte inputs for synthesized ROM tables, including Z80 bindings."""
import json


def check_tables(root, solver, directory, run, msx, backend=None):
    source = directory / 'tables.vx'
    source.write_text('''
&sample(x:#u8)->#u8 {
    p:#u8=(x>>1)&7;
    p==0 ? (#u8)0 : (p==1 ? (#u8)12 : (p==2 ? (#u8)23 :
    (p==3 ? (#u8)35 : (p==4 ? (#u8)46 : (p==5 ? (#u8)57 :
    (p==6 ? (#u8)67 : (#u8)76))))))
}
[[c_binding(periodic,full,1,exclusive)]]
&^periodic(anchor:#u8,x:#u8) {
    s:#u16=(#u16)sample(x);
    ((#u16)anchor<<8)+((x&16)!=0 ? (#u16)0-s : s)
}
[[c_binding(tail,full,1,exclusive)]]
&^tail(x:#u8) {
    x==0 || x==6 || x==12 || x==18 || x==24 ? (#u8)7 : (#u8)0
}
[[c_binding(signed_curve,full,1,exclusive)]]
&^signed_curve(x:#i8) {
    t:#i8=x>>3;
    t < -12 ? (#i16)-300 : (t < -8 ? (#i16)-1 :
    (t < -1 ? (#i16)3 : (t < 4 ? (#i16)256 :
    (t < 12 ? (#i16)32767 : (#i16)-32768))))
}
[[c_binding(wrapping,full,1,exclusive)]]
&^wrapping(x:#u8) { (#u32)sample(x)*(#u32)4294967295 }
[[c_binding(division,full,1,exclusive)]]
&^division(x:#i8) { ((#i8)0-(#i8)sample((#u8)x))/8 }
[[c_binding(bits,full,1,exclusive)]]
&^bits(x:#u8) { (#i32)((#u32)sample(x)<<30) }
[[c_binding(many,full,1,exclusive)]]
&^many(x:#u8) { (sample(x),sample(x)+1,sample(x)+2,sample(x)+3,sample(x)+4) }
[[c_binding(nested,full,1,exclusive)]]
&^nested(x:#u8,y:#u8) {
    x!=0 ? (sample(y),(#u8)0) : ((#u8)0,sample(y+1))
}
''')
    rows = []
    samples = (0, 12, 23, 35, 46, 57, 67, 76)
    for x in range(256):
        s = samples[(x >> 1) & 7]
        anchor = (x * 17 + 3) & 255
        signed = x if x < 128 else x - 256
        q = signed >> 3
        selection = (-300 if q < -12 else -1 if q < -8 else 3 if q < -1
                     else 256 if q < 4 else 32767 if q < 12 else -32768)
        bits = (s << 30) & 0xffffffff
        rows.append([((anchor << 8) + (-s if x & 16 else s)) & 65535,
                     7 if x in (0, 6, 12, 18, 24) else 0, selection,
                     (-s) & 0xffffffff, s, samples[(((x+1)&255) >> 1) & 7], -(s//8),
                     bits if bits < 0x80000000 else bits-0x100000000])
    client = ''.join(f'#include "vx_bind_{name}.h"\n' for name in
                     ('periodic', 'tail', 'signed_curve', 'wrapping', 'nested', 'division', 'bits', 'many'))
    client += 'static const struct {uint16_t p;uint8_t t;int16_t s;uint32_t w;uint8_t n,m;int8_t q;int32_t b;} expected[]={\n'
    client += ',\n'.join('{' + ','.join('(-2147483647L-1L)' if v == -2147483648 else str(v) + ('UL' if k == 3 else 'L' if k == 7 else '')
                        for k, v in enumerate(row)) + '}' for row in rows) + '};\n'
    client += '''uint32_t check_tables(void) {
    uint16_t i;
    VX_BIND_periodic_LOCAL;VX_BIND_tail_LOCAL;VX_BIND_signed_curve_LOCAL;
    VX_BIND_wrapping_LOCAL;VX_BIND_nested_LOCAL;VX_BIND_division_LOCAL;VX_BIND_bits_LOCAL;
    VX_BIND_many_LOCAL;
    for(i=0;i<256;++i) {
        vx_bind_periodic_frame.a0=(uint8_t)(i*17+3);vx_bind_periodic_frame.a1=(uint8_t)i;
        vx_bind_periodic_run();if(vx_bind_periodic_frame.result.v0!=expected[i].p)return 1000+i;
        vx_bind_tail_frame.a0=(uint8_t)i;vx_bind_tail_run();
        if(vx_bind_tail_frame.result.v0!=expected[i].t)return 2000+i;
        vx_bind_signed_curve_frame.a0=VX_VALUE_I8(i);vx_bind_signed_curve_run();
        if(vx_bind_signed_curve_frame.result.v0!=expected[i].s)return 3000+i;
        vx_bind_wrapping_frame.a0=(uint8_t)i;vx_bind_wrapping_run();
        if(vx_bind_wrapping_frame.result.v0!=expected[i].w)return 4000+i;
        vx_bind_nested_frame.a0=1;vx_bind_nested_frame.a1=(uint8_t)i;vx_bind_nested_run();
        if(vx_bind_nested_frame.result.v0!=expected[i].n || vx_bind_nested_frame.result.v1!=0)return 5000+i;
        vx_bind_nested_frame.a0=0;vx_bind_nested_frame.a1=(uint8_t)i;vx_bind_nested_run();
        if(vx_bind_nested_frame.result.v0!=0 || vx_bind_nested_frame.result.v1!=expected[i].m)return 6000+i;
        vx_bind_division_frame.a0=VX_VALUE_I8(i);vx_bind_division_run();
        if(vx_bind_division_frame.result.v0!=expected[i].q)return 7000+i;
        vx_bind_bits_frame.a0=(uint8_t)i;vx_bind_bits_run();
        if(vx_bind_bits_frame.result.v0!=expected[i].b)return 8000+i;
        vx_bind_many_frame.a0=(uint8_t)i;vx_bind_many_run();
        if(vx_bind_many_frame.result.v0!=expected[i].n || vx_bind_many_frame.result.v1!=expected[i].n+1 ||
           vx_bind_many_frame.result.v2!=expected[i].n+2 || vx_bind_many_frame.result.v3!=expected[i].n+3 ||
           vx_bind_many_frame.result.v4!=expected[i].n+4)return 9000+i;
    }
    return 0;
}
'''
    for budget in (0, 64):
        generated = directory / f'tables-{budget}'
        report = json.loads(run([solver, source, '--prefer', f'value_frame_bytes={budget}',
                                 '--emit-c', generated]).stdout)
        plans = {b['name']: b for b in report['value_bindings']['bindings']}
        for plan in plans.values():
            assert plan['storage'] == ('fixed' if budget else 'automatic'), plan
            assert plan['codegen']['finite_tables'] > 0, plan
        assert plans['periodic']['codegen']['finite_table_bytes'] == 16, plans['periodic']
        assert plans['wrapping']['codegen']['finite_table_bytes'] == 8, plans['wrapping']
        assert plans['tail']['codegen']['finite_table_bytes'] == 25, plans['tail']
        assert plans['many']['codegen']['finite_tables'] == 5, plans['many']
        assert plans['many']['codegen']['finite_table_bytes'] <= 512, plans['many']
        host = directory / f'tables-{budget}.c'
        host.write_text(client + '\n#include <stdio.h>\nint main(void){uint32_t r=check_tables();'
                        'if(r)printf("table mismatch %lu\\n",(unsigned long)r);return r!=0;}\n')
        binary = directory / f'tables-host-{budget}'
        run(['cc', '-std=c11', '-O2', '-fsanitize=undefined', '-fno-sanitize-recover=all',
             '-I'+str(generated), '-I'+str(root/'backends/msx-neo8/runtime'), host,
             generated/'vx_binding_frames.c', '-o', binary])
        run([binary])
        if msx:
            project = directory / f'tables-project-{budget}'
            backend.package(generated, project, source, solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(client + '''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_tables();__asm halt __endasm;}
''')
            run(['make', '-C', project, '-j2', 'simulate',
                 'SIMFLAGS=--max-ticks=400000000',
                 'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --max-allocs-per-node 2000'])
            execution = json.loads((project/'build/execution.json').read_text())
            assert execution['observation']['last_result'] == execution['observation']['fault'] == 0, execution
    print('Finite tables: every byte input, signed division, modular narrowing, periodic indices, '
          'default suffix and conditional bindings agree' + (' on host and Z80' if msx else ' (UBSan)'))
