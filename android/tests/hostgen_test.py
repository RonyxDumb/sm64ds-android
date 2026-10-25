import importlib.util
from pathlib import Path
import unittest
root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('hostgen', root/'port/tools/hostgen.py')
hostgen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hostgen)
class HostgenTests(unittest.TestCase):
    def test_member_pointer_roundtrip(self):
        original='#define LADR(p) ((void *)(unsigned int)(p))\n'
        rewritten, count=hostgen.transform(original)
        self.assertEqual(rewritten,'#define LADR(p) ((void *)(p))\n')
        self.assertEqual(count,1)
    def test_guest_values_are_not_widened(self):
        original='unsigned int address;\n#define OTHER(p) ((void *)(unsigned int)(p))\n'
        self.assertEqual(hostgen.transform(original),(original,0))
    def test_mmio_still_routes(self):
        rewritten,count=hostgen.transform('*(volatile u32 *)0x04000000 = value;')
        self.assertIn('NTR_MMIO(u32, 0x04000000)',rewritten)
        self.assertEqual(count,1)
if __name__=='__main__':unittest.main()
