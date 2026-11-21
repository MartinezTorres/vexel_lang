#!/usr/bin/env python3
"""Only proved-defined arithmetic can establish a control certificate."""
import unittest
import test_tuples as support

class DefinedArithmeticTests(unittest.TestCase):
    check=support.TupleTests.check
    execute=support.TupleTests.execute

    def test_division_and_remainder_control(self):
        for predicate in ('n/3<=85', 'n%3<3', '(n/3)*3+n%3==n', 'n/(n|1)<=1'):
            self.execute('&^entry(n:#u8){r=n;{('+predicate+')?{r+=1;}:missing();};r}',lambda n:[(n+1)&255])
        self.check('&^entry(n:#u8){r=n;{n/3<85?{r+=1;}:missing();};r}',status="unresolved",query="value:entry")

    def test_signed_truncation_and_wrapping(self):
        self.execute('&^entry(n:#u8){v=(#i8)n;r=n;{((v/3)*3+v%3)==v?{r+=1;}:missing();};r}',lambda n:[(n+1)&255])
        self.execute('&^entry(n:#u8){v=(#i8)n;r=n;{v%(-1)==0?{r+=1;}:missing();};r}',lambda n:[(n+1)&255])
        self.execute('&^entry(n:#u8){v=(#i8)n;r=n;{v/(-1)==-v?{r+=1;}:missing();};r}',lambda n:[(n+1)&255])

    def test_masked_variable_shifts(self):
        for predicate in ('(n>>(n&7))<=n', '((#u8)0<<(n&7))==0', '((#i8)-1>>(n&7))==(#i8)-1'):
            self.execute('&^entry(n:#u8){r=n;{('+predicate+')?{r+=1;}:missing();};r}',lambda n:[(n+1)&255])
        self.check('&^entry(n:#u8){r=n;{(n>>(n&7))==n?{r+=1;}:missing();};r}',status="unresolved",query="value:entry")

    def test_residual_constants_keep_signed_arithmetic(self):
        # n-n becomes constant in the shared physical facts, after the source
        # arithmetic has already produced its evaluation obligations.
        for value in (-128, -127, 127):
            quotient = -value if value != -128 else -128
            remainder = value-int(value/3)*3
            source = '&^entry(n:#u8){s:#i8=(#i8)(n-n)+(#i8)('+str(value)+');'
            source += '(s/(-1),s>>1,s%3,(#u8)s<<2)}'
            self.execute(source,lambda n,q=quotient,r=remainder,v=value:
                         [q&0xffffffff,(v>>1)&0xffffffff,r&0xffffffff,(v<<2)&255],4)

    def test_unsafe_operands_and_real_traps(self):
        for predicate in ('(1/n)>0', '(n<<n)>0'):
            self.check('&^entry(n:#u8){r=n;{'+predicate+'?missing();r+=1;};r}',status='unresolved',query='value:entry')
        self.execute('&^entry(n:#u8){r=n;{r=1/n;n%3<3?{r+=1;}:missing();};r}',lambda n:[1//n+1] if n else (1,))
        self.execute('&^entry(n:#u8){r=n;{r=(#u8)1<<n;((#u8)0<<(n&7))==0?{r+=1;}:missing();};r}',lambda n:[((1<<n)+1)&255] if n<8 else (2,))

    def test_initialization_uses_numeric_relations(self):
        self.execute('&^entry(n:#u8){x:#u8;n/3<20?{x=n;};n<60?x:(#u8)0}',lambda n:[n if n<60 else 0])
        self.execute('&^entry(n:#u8){x:#u8;i:#u8=0;(i<3)!@{i==n%3?{x=n;};i+=1;};x}',lambda n:[n])

if __name__=='__main__':unittest.main(verbosity=2)
