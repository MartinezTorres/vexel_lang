#!/usr/bin/env python3
"""Portable representation casts, independent of host/target struct layouts."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
class RepresentationTests(unittest.TestCase):
    def check(self,source,expected=None,error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-representation-') as scratch:
            path=Path(scratch)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                run=subprocess.run([str(BINARY),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(run.returncode,1,run.stderr);self.assertIn(error,run.stderr)
                else:
                    self.assertEqual(run.returncode,0,run.stderr)
                    self.assertEqual({r['name']:r['value'] for r in json.loads(run.stdout)['roots']},expected)

    def test_boolean_bit_order_and_copy(self):
        self.check('^answer:#u8={v:#u8=129;bits=(#b[8])v;bits[1]=1;v+(#u8)bits};',{'answer':'4'})
        self.check('^bits:#b[9]=(#b[9])((#u9)257);',{'bits':'[1, 0, 0, 0, 0, 0, 0, 0, 1]'})
        self.check('^x:#u8=(#u8)((#b[7])((#u7)1));',error='exactly one element')

    def test_canonical_record_bytes_and_padding(self):
        self.check('#Wire(a:#u9,b:#i7); ^raw:#u8[3]=(#u8[3])#Wire(257,-2);',{'raw':'[1, 1, 126]'})
        self.check('#Wire(a:#u9,b:#i7); raw:#u8[3]=[1,255,254]; ^answer:#Wire=(#Wire)raw;',{'answer':'[257, -2]'})
        self.check('^raw:#u8[2]=(#u8[2])((#i16)-2);',{'raw':'[254, 255]'})

    def test_nested_arrays_tuples_and_empty_values(self):
        self.check('a:#u16[2][2]=[[1,256],[513,65535]]; ^raw:#u8[8]=(#u8[8])a;',{'raw':'[1, 0, 0, 1, 1, 2, 255, 255]'})
        self.check('^raw:#u8[3]=(#u8[3])((#b)1,(#i16)-1);',{'raw':'[1, 255, 255]'})
        self.check('#Empty(); ^raw:#u8[0]=(#u8[0])#Empty();',{'raw':'[]'})
        self.check('^raw:#u8[0]=(#u8[0])[];',{'raw':'[]'})

    def test_size_and_boolean_validation(self):
        self.check('^raw:#u8[1]=(#u8[1])((#u16)1);',error='size mismatch')
        self.check('raw:#u8[2]=[0,2]; ^bits:#b[2]=(#b[2])raw;',error='not representable as a boolean')
        self.check('^answer:#u8={raw:#u8[2];bits:#[((#b[2])raw)]=[0,1];(#u2)bits};',{'answer':'2'})

    def test_symbolic_representation_constraints(self):
        self.check('^answer:#u4=?{bits=?(#b[4],[0,1]);n=(#u4)bits;?(n==9);n};',{'answer':'9'})
        self.check('^answer:#b[1]=?{raw=?(#u8[1],[0,1,2]);bits=(#b[1])raw;?(bits[0]);bits};',{'answer':'[1]'})

if __name__=='__main__':unittest.main(verbosity=2)
