import importlib.util
from pathlib import Path
import unittest

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('native_sources', root / 'android/tools/native_sources.py')
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


class NativeSourcesTests(unittest.TestCase):
    def test_save_pointer_adaptation_is_scoped(self):
        source = (root / 'src/engine/save/SaveData2.cpp').read_text()
        result = native.save_io(source)
        self.assertIn('const void* b', result)
        self.assertNotIn('(int)&sum', result)
        self.assertNotIn('(int)data,', result)
        self.assertIn('u32 slot', result)
        self.assertIn('slot << 7', result)
        self.assertIn('half + off', result)
        self.assertIn('int ok = 0;', result)

    def test_upstream_changes_are_not_silently_accepted(self):
        with self.assertRaises(ValueError):
            native.save_io('int func_02060484(int a, long b);')

    def test_only_typed_shared_file_functions_are_selected(self):
        source = (root / 'src/SharedFilePtr.cpp').read_text()
        result = native.shared_file(source)
        self.assertIn('void SharedFilePtr::Release()', result)
        self.assertIn('numRefs >= 0xff', result)
        self.assertNotIn('void *SharedFilePtr::Load()', result)
        self.assertNotIn('UpdateFileOffsets', result)
        self.assertNotIn('(int)Memory::Allocate', result)


if __name__ == '__main__':
    unittest.main()
