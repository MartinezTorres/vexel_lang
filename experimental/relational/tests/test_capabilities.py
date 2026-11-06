#!/usr/bin/env python3
"""Capabilities close signatures; selected bodies remain ordinary obligations."""
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support


class CapabilityTests(unittest.TestCase):
    check = support.TupleTests.check
    execute = support.TupleTests.execute

    def test_function_signatures_and_non_evaluating_arguments(self):
        self.check('&f(x:#u8){missing()}a=?f(1/0);', '1')
        self.check('a=?f(1/0);&f(x:#u8){missing()}', '1')
        self.check('a=?missing(1/0);', '0')
        self.check('&f(x:#u8){x}a=?f("wrong");', '0')
        self.check('&f(x:#u8){x}a=?f();', '0')
        self.check('&!sink(x:#u8);a=?sink(1/0);', '1')
        self.execute('&f(x:#u8){x}&^entry(n:#u8){x=n;exists=?f(x=3);(x,(#u8)exists)}', lambda n: [n, 1], 2)

    def test_selected_body_has_no_rollback_or_overload_fallback(self):
        self.check('a={x:#u8=7;?missing(x)?{missing();x=9;};x};', '7')
        self.check('&f(x:#u8){missing()}a={?f(3)?{f(3);};7};', status='unresolved')
        self.check('&f(x:#u8){1/0}a={?f(3)?{f(3);};7};', status='contradictory')
        self.check('&f(x:#u8){missing()}&f(x){42}a={?f((#u8)3)?f((#u8)3):7};', status='unresolved')
        self.execute('&(x:#u8)add(k:#u8){x+=k;}&^entry(n:#u8){x=n;'
                     'x.?add(1)?{x.add(1);};x.?absent()?{missing();};x}', lambda n: [(n+1)&255])

    def test_unknown_and_ambiguous_are_not_absence(self):
        self.check('&f(x:#u8){1}&f(x:#u16){2}a=?f(3);', status='ambiguous')
        self.check('&f(x:#u8){1}a={x:#Missing;?f(x)};', status='unresolved')
        self.check('&f(x:#Missing){1}a=?f(3);', status='unresolved')
        self.check('x:#[y];y:#[x];&f(x:#u8){1}a=?f(x);', status='unresolved')

    def test_capability_guards_keep_public_constraints(self):
        self.check('&f(x:#u8){x}a=?{x=?(#u8,0..4);?f(x)?{?(x==2);};x};', '2')
        self.check('a=?{x=?(#u8,0..4);?missing(x)?{?(x==1);};?(x==2);x};', '2')
        self.check('&f(x:#u8){x}a=?{x=?(#u8,0..4);?f(x)?{?(x==1);};?(x==2);x};', status='contradictory')

    def test_transaction_syntax_is_a_migration_error_even_if_unused(self):
        for source in ('a={#{missing();};7};', '&unused(){#{1;};}a=7;'):
            with tempfile.TemporaryDirectory(prefix='vexel-capability-migration-') as temp:
                path=Path(temp)/'source.vx';path.write_text(source)
                result=support.run([support.EXE,path,'--query','value:a'])
                self.assertEqual(result.returncode,2,result.stdout+result.stderr)
                self.assertIn('semantic rollback was removed',result.stderr)
                self.assertIn('?function(args)',result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
