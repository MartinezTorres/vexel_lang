"""Check stored SDCC operands and promotions in a small ROM harness."""
import json


def check_ordering(root, solver, directory, run, msx, backend=None):
    source = directory / "ordering.vx"
    source.write_text("""
[[c_binding(clamp,full,1,exclusive)]]
&^clamp(x:#u8,dt:#u8){
    room:#u8=x<240 ? 240-x : (#u8)0;
    step:#u8=dt<room ? dt : room;
    next:#u8=x+step;
    (step,next,next-1,dt<=room ? (#u8)1 : (#u8)0,
     dt<room ? (#u8)1 : (#u8)0,dt>room ? (#u8)1 : (#u8)0,
     dt>=room ? (#u8)1 : (#u8)0)
}
[[c_binding(compose,full,1,exclusive)]]
&^compose(anchor:#u8,phase:#u8){
    height:#u8=phase<16 ? phase : 32-phase;
    (((#u16)anchor<<8)+(#u16)height,
     ((#u16)anchor<<8)+((#u16)0-(#u16)height),height)
}
""")
    rows = []
    for x in (0, 13, 180, 239, 240, 255):
        for dt in (0, 1, 2, 239, 240, 255):
            room = max(0, 240-x)
            step = min(dt, room)
            rows.append([x, dt, step, (x+step)&255, (x+step-1)&255,
                         int(dt<=room), int(dt<room), int(dt>room), int(dt>=room)])
    poses = []
    for anchor in (0, 1, 127, 255):
        for phase in (0, 1, 15, 16, 17, 31, 255):
            height = phase if phase < 16 else (32-phase)&255
            poses.append([anchor, phase, ((anchor<<8)+height)&65535,
                          ((anchor<<8)-height)&65535, height])
    client = '#include "vx_bind_clamp.h"\n#include "vx_bind_compose.h"\nstatic const uint8_t vectors[][9]={'
    client += ','.join('{' + ','.join(map(str, row)) + '}' for row in rows) + '};\n'
    client += 'static const uint16_t poses[][5]={'
    client += ','.join('{' + ','.join(map(str, row)) + '}' for row in poses) + '};\n'
    client += '''uint32_t check_ordering(void) {
    uint8_t i; VX_BIND_clamp_LOCAL; VX_BIND_compose_LOCAL;
    for(i=0;i<sizeof(vectors)/sizeof(vectors[0]);++i) {
        vx_bind_clamp_frame.a0=vectors[i][0];
        vx_bind_clamp_frame.a1=vectors[i][1];vx_bind_clamp_run();
'''
    client += '        if(' + ' || '.join(
        f'vx_bind_clamp_frame.result.v{j}!=vectors[i][{j+2}]' for j in range(7))
    client += ''')return i+1;
    }
    for(i=0;i<sizeof(poses)/sizeof(poses[0]);++i) {
        vx_bind_compose_frame.a0=poses[i][0];
        vx_bind_compose_frame.a1=poses[i][1];vx_bind_compose_run();
        if(vx_bind_compose_frame.result.v0!=poses[i][2] ||
           vx_bind_compose_frame.result.v1!=poses[i][3] ||
           vx_bind_compose_frame.result.v2!=poses[i][4])return i+101;
    }
    return 0;
}
'''
    for budget in (0, 32):
        generated = directory / f"ordering-{budget}"
        report = json.loads(run([solver, source, '--prefer', f'value_frame_bytes={budget}',
                                 '--emit-c', generated]).stdout)
        for binding in report['value_bindings']['bindings']:
            assert binding['storage'] == ('fixed' if budget else 'automatic'), binding
        host = directory / f"ordering-{budget}.c"
        host.write_text(client + '\nint main(void){return check_ordering()!=0;}\n')
        binary = directory / f"ordering-host-{budget}"
        run(['cc', '-std=c11', '-O2', '-fsanitize=undefined', '-fno-sanitize-recover=all',
             '-I'+str(generated), '-I'+str(root/'backends/msx-neo8/runtime'), host,
             generated/'vx_binding_frames.c', '-o', binary])
        run([binary])
        if msx:
            project = directory / f"ordering-project-{budget}"
            backend.package(generated, project, source, solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(client + '''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_ordering();__asm halt __endasm;}
''')
            # Match the allocator limit that exposed the game failure.
            run(['make', '-C', project, '-j2', 'simulate',
                 'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --max-allocs-per-node 2000'])
            execution = json.loads((project/'build/execution.json').read_text())
            assert execution['observation']['last_result'] == execution['observation']['fault'] == 0, execution
    print('Stored operands: byte ordering, promoted/negated word addition and automatic/fixed bindings agree' +
          (' on host and Z80' if msx else ' (UBSan)'))
