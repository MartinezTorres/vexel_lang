#!/usr/bin/env python3
"""Execute linked Z80, including startup and bank transitions, on an independent CPU."""
import json
import importlib.util
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile
import unittest

from test_msx import EXE, REPO, BASE_SOURCE, FIXED_SOURCE, project_environment, run


@unittest.skipUnless(shutil.which("sdcc") and shutil.which("sdasz80"), "SDCC toolchain unavailable")
class MachineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="vexel-z80-test-")
        cls.base = Path(cls.tmp.name)
        cls.source = cls.base / "machine.vx"
        cls.source.write_text(BASE_SOURCE + "\n&!cross(x:#u16)->#u16;\n"
                              "&^outer(x:#u16)->#u16 {cross(x)+17}\n"
                              "&!msx_putchar(c:#u8);\n"
                              "&^printadd(x:#u16)->#u16 {msx_putchar((#u8)x); x+7}\n")
        cls.project = cls.base / "project"
        run([EXE, cls.source, "--emit-msx", cls.project])
        reference = cls.base / "reference.vx"
        # The reference resolves finite executions with @ instead of the !@
        # barrier, using the same language rules, without running generated C.
        reference.write_text(cls.source.read_text().replace("!@", "@"))
        # A full module forces the callback's caller into a separate segment.
        # C remains editable; an externally visible const object is retained by SDCC.
        with (cls.project / "generated/vxp_outer.c").open("a") as f:
            f.write("\nconst unsigned char force_separate_segment[7000] = { 0x5a };\n")
        cases = [
            ("recursive_sum", [7]), ("recursive_acc", [9]), ("recursive_inferred", [8]),
            ("recursive_even", [11]), ("recursive_even", [12]), ("recursive_export", [6]),
            ("recursive_shape", [5]), ("capture_recursion", [7]), ("capture_after", [6]), ("receiver_recursion", [7]),
            ("materialized_capture", [42]), ("materialized_array", [65535]), ("materialized_record", [65535]),
            ("materialized_method", [42,0]), ("materialized_method", [65535,1]),
            ("array_compare", [65535,0]), ("array_compare", [65535,65535]),
            ("shorter_compare", [42,42]), ("record_compare", [42,42]), ("signed_array_compare", [-2]),
            ("bit_round", [65535]), ("wire_byte", [65535,1]), ("wire_byte", [12,2]),
            ("wire_decode", [65535]), ("boolean_bytes", [1]),
            ("tuple_assign", [4,7]), ("tuple_loop", [65535,123]),
            ("early_call", [0,65535]), ("early_call", [1,19]),
            ("early_value", [0]), ("early_value", [1]), ("early_both", [0]), ("early_both", [1]),
            ("early_capture", [0]), ("early_capture", [1]),
            ("unroll_exit", [0]), ("unroll_exit", [6]), ("runtime_exit", [6]),
            ("runtime_return", [4]), ("runtime_return", [12]), ("repeat_exit", [5]),
            ("expressions", [65535]), ("captured", [123]), ("captured_loop", [65535]),
            ("broadcast", [65535,0]), ("broadcast", [123,2]),
            ("logical_method", [65535]), ("logical_field", [42]),
            ("lifted_method", [65535]), ("lifted_capture", [42]),
            ("method_call", [65535,0]), ("method_call", [123,1]), ("method_loop", [65535]),
            ("operator_loop", [65535]), ("custom_iter", [123]), ("macro_loop", [65535,7]),
            ("record_copy", [123,0]), ("record_copy", [65535,2]),
            ("record_branch", [456,1]), ("record_branch", [65535,0]),
            ("record_loop", [65535]), ("record_iter", [123]),
            ("arithmetic", [65535, 40000]), ("wide", [4294967295, 2147483649]),
            ("division", [-2147483648, -1]), ("division", [-2147483648, 3]),
            ("remainder", [-2147483648, 3]), ("remainder", [2147483647, -99]),
            ("right", [-32768, 15]), ("right", [-1, 0]), ("left", [255, 7]),
            ("signed_small", [-128]), ("signed_small", [127]), ("odd_width", [127]),
            ("absolute", [-32768]), ("compare", [-300, 127]), ("narrow", [-1000]),
            ("flow", [123, 1]), ("flow", [65535, 0]), ("nested", [250, 1, 0]),
            ("lookup", [3]), ("unroll", [65534]), ("order", [255]),
            ("loop_sum", [0]), ("loop_sum", [10]), ("nested_loops", [8]),
            ("guard_mutation", [0]), ("guard_mutation", [5]),
            ("runtime_array", [123, 65535]), ("mutating_loop", [123]),
            ("iteration_snapshot", [200]), ("array_copy", [200,0]),
            ("array_copy", [200,1]), ("matrix", [123,0,1]),
            ("matrix", [123,1,2]), ("chosen_array", [200,1]),
            ("chosen_array", [200,0]), ("nested_iteration", [200]),
            ("unrolled_repeat", [250]), ("constant_after_loop", [10]),
            ("index_once", [0]), ("index_once", [2]),
        ]
        rng = random.Random(731)
        for _ in range(8):
            a, b = rng.randrange(-2**31, 2**31), rng.randrange(-2**31, 2**31) or 1
            cases.extend([("division", [a, b]), ("remainder", [a, b])])
        record_type=re.search(r'void vx_round_record\(const (\w+)\* p0,',(cls.project/'generated/vexel.h').read_text()).group(1)
        cls.main = '#include "vexel.h"\n' + f'''void vx_ext_record_exchange(const {record_type}* input, {record_type}* output) VX_ABI VX_RESIDENT {{ *output=*input; output->f1+=2; }}
''' + '''
volatile uint32_t vx_last_result;
volatile uint16_t initialized = 0x1234;
volatile uint8_t reads, writes, last;
uint8_t again_count;
uint8_t vx_ext_again(void) VX_ABI VX_RESIDENT { return again_count++ < 3; }
uint16_t vx_ext_bytes_sum(const uint8_t* bytes,uint16_t size) VX_ABI VX_RESIDENT {
    uint16_t result=0;while(size--) result+=*bytes++;return result;
}
uint8_t vx_ext_read_byte(void) VX_ABI VX_RESIDENT { ++reads; return 11; }
void vx_ext_write_byte(uint8_t v) VX_ABI VX_RESIDENT { ++writes; last=v; }
uint16_t vx_ext_cross(uint16_t x) VX_ABI VX_RESIDENT { return vx_flow(x, 1); }
uint8_t iff(void) VX_ABI __naked {
    __asm
        ld a,i
        ld l,#0
        ret po
        inc l
        ret
    __endasm;
}
void main(void) VX_ABI VX_RESIDENT {
    vx_last_result=0;
    if (initialized != 0x1234 || reads || writes || last || vx_fault) vx_last_result=1;
'''
        for index, (name, args) in enumerate(cases, 2):
            command = [EXE, reference, "--call", name, "--json"]
            for arg in args:
                command.extend(["--arg", str(arg)])
            expected = int(json.loads(run(command).stdout)["roots"][0]["value"]) & 0xffffffff
            # Cast the signed result to u32 for comparison with oracle bit patterns.
            values = ",".join(str(v) + ("UL" if v >= 0 else "L") for v in args)
            cls.main += f"    if ((uint32_t)vx_{name}({values}) != {expected}UL) vx_last_result={index};\n"
        record_type = re.search(r'void vx_round_record\(const (\w+)\* p0,', (cls.project/'generated/vexel.h').read_text()).group(1)
        cls.main += f'''    {{
        {record_type} input, output;
        input.f0.items[0]=42; input.f0.items[1]=2; input.f0.items[2]=3; input.f1=4;
        vx_round_record(&input,&output);
        if (output.f0.items[0]!=42 || output.f0.items[1]!=6 || output.f1!=5 || input.f1!=4) vx_last_result=121;
    }}
'''
        cls.main += '''
    if (vx_public_number!=513 || vx_public_array.items[1]!=257 || vx_public_record.f0.items[2]!=30) vx_last_result=122;
    if (vx_public_bytes_length!=3 || vx_public_bytes[0]!=65 || vx_public_bytes[1]!=0 || vx_public_bytes[2]!=66) vx_last_result=123;
    again_count=0;if (vx_noarg_dynamic()!=3 || again_count!=4) vx_last_result=124;
    again_count=0;if (vx_noarg_generalized()!=3 || again_count!=4) vx_last_result=125;
    if (vx_binary_text()!=131 || vx_external_record(65535)!=5) vx_last_result=126;
    if (vx_outer(123) != 400) vx_last_result=100;
    if (vx_effects(250, 1) != 5 || reads != 1 || writes != 1 || last != 5) vx_last_result=101;
    if (vx_short(0) || reads != 1) vx_last_result=102;
    if (!vx_short(1) || reads != 2) vx_last_result=103;
    if (vx_reuse_one() != 42 || last != 1 || writes != 2) vx_last_result=104;
    if (vx_reuse_two() != 42 || last != 2 || writes != 3) vx_last_result=105;
    if (vx_wide_constant() != 255 || last != 255 || writes != 4) vx_last_result=106;
    /* Exercise both register bytes, including v1.2 bits 12/13, from RAM.
       These segments have no ROM payload; no read from them is attempted. */
    __asm di __endasm;
    if (vx_bank_b(0x1234) != 1 || vx_segment_b != 0x1234 || iff()) vx_last_result=107;
    if (vx_bank_b(0x2101) != 0x1234 || vx_segment_b != 0x2101 || iff()) vx_last_result=108;
    if (vx_bank_b(1) != 0x2101 || iff()) vx_last_result=109;
    __asm ei __endasm;
    if (vx_bank_b(0x1234) != 1 || !iff()) vx_last_result=110;
    if (vx_bank_b(1) != 0x1234 || !iff()) vx_last_result=111;
    if (vx_printadd(346) != 353) vx_last_result=112;
    vx_msx_putchar('O'); vx_msx_putchar('K');
    for (;;) { __asm halt __endasm; }
}
'''
        (cls.project / "user/main.c").write_text(cls.main)
        run(["make", "-j2", "simulate"], cwd=cls.project)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def report(self):
        return json.loads((self.project / "build/execution.json").read_text())

    def test_z80_fixed_point(self):
        source = self.base / 'fixed.vx'
        source.write_text(FIXED_SOURCE)
        project = self.base / 'fixed-project'
        run([EXE,source,'--emit-msx',project])
        cases = [
            ("fixed_mul_bits", [0xffffffff,0x7fffffff]), ("fixed_mul_bits", [0x80000000,0x80000000]),
            ("fixed_div_bits", [0xfffffffd,0x00018000]), ("fixed_div_bits", [0x80000000,0xffffffff]),
            ("fixed_fraction_bits", [0xffffffff,0xffffffff]),
            ("fixed_rescale", [-3]), ("fixed_branch", [-3,0]), ("fixed_branch", [-3,1]),
            ("fixed_record", [-3]), ("fixed_array", [-3]), ("fixed_compare", [4]), ("fixed_compare", [3]),
            ("fixed_call", [1]), ("fixed_call", [4]),
        ]
        main = '#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {\n'
        for index,(name,args) in enumerate(cases,1):
            command = [EXE,source,'--call',name,'--json']
            for argument in args:
                command.extend(['--arg',str(argument)])
            expected = int(json.loads(run(command).stdout)['roots'][0]['value']) & 0xffffffff
            arguments = ','.join(str(a) + ('UL' if a >= 0 else 'L') for a in args)
            main += f'if ((uint32_t)vx_{name}({arguments}) != {expected}UL) vx_last_result={index};\n'
        main += 'for (;;) { __asm halt __endasm; }\n}\n'
        (project/'user/main.c').write_text(main)
        run(['make','-j2','simulate'],cwd=project)
        report = json.loads((project/'build/execution.json').read_text())
        self.assertEqual(report['observation']['last_result'],0)

    def test_frame_candidates_execute_against_same_oracle(self):
        spec = importlib.util.spec_from_file_location("measure_msx", REPO / "examples/measure_msx.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        report = module.experiment(EXE, self.base / "comparison")
        self.assertEqual(len(report["expected_results"]), 8)
        self.assertEqual(report["stack_proof"], "unproven")
        automatic, static = report["candidates"]
        self.assertNotEqual(automatic["rom_sha256"], static["rom_sha256"])
        for row in report["candidates"]:
            execution = json.loads((Path(row["project"]) / "build/execution.json").read_text())
            self.assertEqual(execution["observation"]["last_result"], 1)
            self.assertGreater(execution["observation"]["interrupts_accepted"], 0)
            self.assertEqual(execution["rom_sha256"], row["rom_sha256"])
        for plan in report['resolved_plans']:
            admissible = [r for r in report['candidates'] if r['resident_bytes'] <= plan['resident_budget']
                          and r['observed_stack_bytes'] <= plan['observed_stack_limit']]
            expected = min(admissible, key=lambda r: (r['rom_section_bytes'], report['candidates'].index(r)))
            self.assertEqual(plan['selected_candidate'], expected['candidate'])
            self.assertEqual(json.loads(Path(plan['search']).read_text())['puzzles'][0]['search']['status'], 'unique')
        self.assertNotEqual(report['resolved_plans'][0]['selected_candidate'],report['resolved_plans'][1]['selected_candidate'])
        original = Path(automatic['project']) / 'build/execution.json'
        original.write_text(original.read_text() + '\n')
        with self.assertRaisesRegex(ValueError, 'stale candidate evidence'):
            module.solve_plan(EXE, self.base, report['candidates'], 128)

    def test_z80_matches_solver_and_nested_cross_bank_callback(self):
        report = self.report()
        observation = report["observation"]
        self.assertEqual(observation["last_result"], 0)
        self.assertEqual(observation["output_bytes"], list(b"ZOK"))
        self.assertEqual(observation["fault"], 0)
        self.assertEqual(observation["bank_b"], 1)
        self.assertTrue(observation["interrupts_enabled"])
        self.assertGreaterEqual(len(observation["fetched_segments"]), 2)
        self.assertIn(0x1234, observation["bank_b_segments_selected"])
        self.assertIn(0x2101, observation["bank_b_segments_selected"])
        self.assertGreater(observation["observed_stack_bytes"], 20)
        self.assertEqual(report["stack_proof"], "unproven")
        placement = json.loads((self.project / "build/resolution.json").read_text())
        segments = {a["module"]: a["segment"] for a in placement["areas"] if a["area"] == "CODE"}
        self.assertNotEqual(segments["vxp_outer"], segments["vxp_flow"])

    def test_z80_interrupts_expanded_slot_and_reproducibility(self):
        flags = "--irq-period 997 --expanded-slot"
        run(["make", "simulate", "SIMFLAGS=" + flags], cwd=self.project)
        first = self.report()
        self.assertEqual(first["observation"]["last_result"], 0)
        self.assertGreater(first["observation"]["interrupts_accepted"], 10)
        self.assertTrue(first["observation"]["interrupts_enabled"])
        run(["make", "simulate", "SIMFLAGS=" + flags], cwd=self.project)
        self.assertEqual(self.report(), first)

    def test_z80_rejected_workloads_leave_no_success_report(self):
        for flags, message in [("--max-ticks 20", "tick budget"),
                               ("--himem 0xf000", "startup did not complete")]:
            result = subprocess.run(["make", "simulate", "SIMFLAGS=" + flags], cwd=self.project,
                                    text=True, capture_output=True, env=project_environment())
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(message, result.stderr)
            self.assertFalse((self.project / "build/execution.json").exists())
        # Restore observation for tests regardless of unittest's name ordering.
        run(["make", "simulate"], cwd=self.project)

    def test_z80_runtime_traps(self):
        try:
            for expression, reason in [("vx_division(1,0)", 1), ("vx_left(1,8)", 2),
                                       ("vx_lookup(4)", 3), ("vx_flow(1,2)", 5),
                                       ("vx_trap_then_constant(0)", 1),
                                       ("vx_bounds_then_constant(2)", 3), ("vx_boolean_bytes(2)", 5),
                                       ("vx_discard_boolean_bytes(2)", 5)]:
                source = self.main.replace("vx_last_result=0;", "vx_last_result=0;\n    " + expression + ";")
                (self.project / "user/main.c").write_text(source)
                result = subprocess.run(["make", "simulate", "-j2"], cwd=self.project, text=True,
                                        capture_output=True, env=project_environment())
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"runtime trapped with reason {reason}", result.stderr)
                self.assertFalse((self.project / "build/execution.json").exists())
        finally:
            (self.project / "user/main.c").write_text(self.main)
            run(["make", "simulate", "-j2"], cwd=self.project)

    def test_z80_stack_budget_failure(self):
        try:
            source = self.main.replace("vx_last_result=0;", """vx_last_result=0;
    __asm
        ld hl,#-1100
        add hl,sp
        ld sp,hl
        nop
    __endasm;
""")
            (self.project / "user/main.c").write_text(source)
            result = subprocess.run(["make", "simulate", "-j2"], cwd=self.project, text=True,
                                    capture_output=True, env=project_environment())
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("observed stack exceeded", result.stderr)
            self.assertFalse((self.project / "build/execution.json").exists())
        finally:
            (self.project / "user/main.c").write_text(self.main)
            run(["make", "simulate", "-j2"], cwd=self.project)

    def test_stale_rom_measurements_are_rejected(self):
        rom = self.project / "build/game.rom"
        before = rom.read_bytes()
        try:
            rom.write_bytes(before[:-1] + bytes([before[-1] ^ 1]))
            result = subprocess.run(["python3", "tools/simulate.py", "--plan", "generated/plan.json",
                                     "--resolution", "build/resolution.json", "--rom", "build/game.rom",
                                     "--runner", "build/neo8-runner", "--output", "build/execution.json"],
                                    cwd=self.project, text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("different ROM", result.stderr)
            self.assertFalse((self.project / "build/execution.json").exists())
        finally:
            rom.write_bytes(before)
            run(["make", "simulate"], cwd=self.project)

    def test_nonterminating_region_is_not_folded_to_its_constant_result(self):
        source = self.base / "infinite.vx"
        source.write_text("&^entry()->#u8{(1)!@{};42}")
        project = self.base / "infinite"
        run([EXE, source, "--emit-msx", project])
        result = subprocess.run(["make", "simulate", "-j2", "SIMFLAGS=--max-ticks 200000"],
                                cwd=project, text=True, capture_output=True, env=project_environment())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("execution exhausted its tick budget", result.stderr)
        self.assertFalse((project / "build/execution.json").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
