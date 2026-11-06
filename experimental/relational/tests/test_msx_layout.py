#!/usr/bin/env python3
"""Absolute resources and linked allocation occupy distinct address spaces."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('layout', ROOT/'backends/msx-neo8/tools/check_project.py')
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)


class LayoutTests(unittest.TestCase):
    def test_external_bindings_respect_linked_ram_and_stack(self):
        plan = dict(ram_start=0xc000, resident_limit=0xef80, stack_top=0xf380)
        areas = [dict(address=0xc000, bytes=0x100, module='runtime', area='_HOME'),
                 dict(address=0xc200, bytes=8, module='state', area='_DATA')]
        accepted = [(0xc100, 0x100), (0xc208, 1), (0xef7f, 1), (0xf380, 1), (65535, 0)]
        rejected = [(0xbfff, 2), (0xc000, 1), (0xc1ff, 2), (0xc207, 1),
                    (0xef7f, 2), (0xef80, 1), (0xf37f, 2), (65535, 2), (65536, 0), (-1, 1)]
        for address, size in accepted + rejected:
            with self.subTest(address=address, size=size):
                plan['storage_bindings'] = [dict(name='device', space='address', address=address, bytes=size)]
                if (address, size) in accepted:
                    layout.check_bindings(plan, areas)
                else:
                    with self.assertRaises(ValueError):
                        layout.check_bindings(plan, areas)

    def test_ports_and_external_aliases(self):
        plan = dict(ram_start=0xc000, resident_limit=0xef80, stack_top=0xf380)
        # A port number and memory address can coincide without aliasing.
        plan['storage_bindings'] = [dict(name='status', space='port', address=0x98, bytes=1),
                                   dict(name='bios', space='address', address=0x98, bytes=1),
                                   dict(name='bios_alias', space='address', address=0x98, bytes=1)]
        layout.check_bindings(plan, [])
        for space, address, size in [('port', 256, 1), ('port', 1, 2), ('unknown', 0, 1)]:
            plan['storage_bindings'] = [dict(name='bad', space=space, address=address, bytes=size)]
            with self.assertRaises(ValueError):
                layout.check_bindings(plan, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
