#!/usr/bin/env python3
"""Native proofs use incoming capture facts and physical returned slot order."""
import unittest
import test_tuples as support

class NativeCaptureControlTests(unittest.TestCase):
    execute=support.TupleTests.execute
    check=support.TupleTests.check

    def test_read_only_and_changed_captures(self):
        self.execute('&^entry(n:#u8){x=n;&~read(k:#u8){x}'
                     'r=n;{read(n)!=n?missing();r+=1;};r}',lambda n:[(n+1)&255])
        self.execute('&^entry(n:#u8){x=n;&~read(k:#u8){x}'
                     'a=read(n);x+=1;b=read(n);r=n;{(a!=n||b!=n+1)?missing();r+=1;};r}',lambda n:[(n+1)&255])

    def test_output_only_and_static_return_slot(self):
        for body in ('x=k;', 'x=k;"done"'):
            self.execute('&^entry(n:#u8){x:#u8;&~set(k:#u8){'+body+'}'
                         'r=n;{set(n);x!=n?missing();r+=1;};(r,x)}',lambda n:[(n+1)&255,n],2)

    def test_read_write_outputs(self):
        self.execute('&^entry(n:#u8){x=n;&~advance(k:#u8){x+=k;x}'
                     'r=n;{a=advance(n);(a!=n+n||x!=a)?missing();r+=1;};(r,x)}',lambda n:[(n+1)&255,(2*n)&255],2)

    def test_sequential_capture_certificates_compose(self):
        self.execute('&^entry(n:#u8){x=n;&~advance(k:#u8){x+=k;x}'
                     'r=n;{advance(n);a=advance(n);(a!=n+n+n||x!=a)?missing();r+=1;};(r,x)}',lambda n:[(n+1)&255,(3*n)&255],2)
        self.execute('&~identity(k:#u8){k}&^entry(n:#u8){x=n;&~read(k:#u8){x}'
                     'r=n;{identity(read(n))!=n?missing();r+=1;};r}',lambda n:[(n+1)&255])

    def test_type_only_capture_does_not_read_uninitialized_value(self):
        self.execute('&^entry(n:#u8){data:#u8[2];&~size(k:#u8){copy:#[data]=[k,k];|copy|}'
                     'r=n;{size(n)!=2?missing();r+=1;};r}',lambda n:[(n+1)&255])

    def test_aggregate_output_slot_order(self):
        self.execute('#P(tag:#s,x:#u8,y:#u16);&^entry(n:#u8){p=#P("abc",n,(#u16)n+300);'
                     '&~set(k:#u8){p.x=k+1;p.y=(#u16)k+301;(k,(#u16)k+300)}'
                     'r=n;{a=set(n);(a.__0!=n||a.__1!=(#u16)n+300||p.x!=n+1||p.y!=(#u16)n+301)?missing();r+=1;};'
                     '(r,p.x,p.y)}',lambda n:[(n+1)&255,(n+1)&255,n+301],3)

    def test_captured_writes_keep_native_traps(self):
        self.execute('&^entry(n:#u8){x:#u8;&~set(k:#u8){x=k;1/k;}'
                     'r=n;{set(n);x!=n?missing();r+=1;};(r,x)}',lambda n:[(n+1)&255,n] if n else (1,),2)

    def test_readiness_is_not_a_guessed_input(self):
        for source in (
            '&^entry(n:#u8){x:#u8;&~read(k:#u8){x}r=n;{read(n)!=n?missing();r+=1;};r}',
            '&^entry(n:#u8){x:#u8;&~set(k:#u8){k<128?{x=k;};}r=n;{set(n);x!=n?missing();r+=1;};r}',
        ):
            self.check(source,status='unresolved',query='value:entry')

    def test_captured_choices_remain_public(self):
        self.check('&^entry(n:#u8){x=?{p=?(#u8,[0,1]);p};'
                   '&~read(k:#u8){x+k}(read(n),x)}',status='ambiguous',query='value:entry')

if __name__=='__main__':unittest.main(verbosity=2)
