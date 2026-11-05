import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('packed_sources', root/'android/tools/packed_sources.py')
packed_sources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packed_sources)


class PackedSourceTests(unittest.TestCase):
    def test_renamed_and_removed_units_do_not_survive_regeneration(self):
        source = 'src/GetOwnerLanguage.c'
        row = {'path': source, 'sha256': hashlib.sha256((root/source).read_bytes()).hexdigest(),
               'header': False, 'unit': 'old_name', 'source': 'int generated_callback() { return 7; }'}
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)/'generated'
            manifest = Path(directory)/'manifest.json'
            def generate(rows):
                manifest.write_text(json.dumps({'version': 1, 'entries': rows}))
                return packed_sources.generate(out, manifest)
            generate([row])
            row['unit'] = 'new_name'
            self.assertEqual(generate([row]), [out/'new_name.cpp'])
            self.assertFalse((out/'old_name.cpp').exists())
            self.assertTrue((out/'new_name.cpp').exists())
            self.assertEqual(generate([]), [])
            self.assertFalse((out/'new_name.cpp').exists())


if __name__ == '__main__':
    unittest.main()
