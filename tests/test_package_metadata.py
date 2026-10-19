"""Check release metadata using the real SD-tree assembler and synthetic inputs."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PackageMetadataTests(unittest.TestCase):
    """Verify declarations survive assembly without requiring a private cart or compiler."""

    def setUp(self):
        """Assemble the real metadata with synthetic runtime and program files."""
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        for relative in ('dist/core', 'dist/platforms', 'dist/instance'):
            shutil.copytree(ROOT / relative, root / relative)
        script = root / 'scripts/assemble-pocket.sh'
        script.parent.mkdir()
        shutil.copyfile(ROOT / 'scripts/assemble-pocket.sh', script)
        files = {
            '.obj/pico8pocket/app.elf': b'synthetic program fixture',
            '.deps/openfpgaos-sdk/runtime/pocket/os20.rbf_r': b'bitstream fixture',
            '.deps/openfpgaos-sdk/runtime/pocket/loader.bin': b'loader fixture',
            '.deps/openfpgaos-sdk/runtime/pocket/os.bin': b'runtime fixture',
            'src/app/pico8pocket.ini': b'config fixture',
            'assets/p8p_slot_pad.bin': b'padding fixture',
            'assets/cards/README.txt': b'Provide your own carts.',
            'LICENSE': b'license fixture',
            'THIRD_PARTY_NOTICES.md': b'notice fixture',
            '.deps/openfgpaSDK/LICENSE': b'SDK license fixture',
            '.deps/openfgpaSDK/NOTICE': b'SDK notice fixture',
            '.deps/fake-08/LICENSE.MD': b'Fake-08 license fixture',
            '.deps/fake-08/libs/miniz/LICENSE': b'miniz license fixture',
            '.deps/fake-08/libs/z8lua/lua.h': b'Lua header and copyright fixture',
            '.deps/fake-08/libs/z8lua/eris.c': b'/*\nEris copyright fixture\n*/\n\nint private_implementation;\n',
            '.deps/openfpgaCore/NOTICE': b'pinned runtime notice fixture',
            '.deps/openfpgaCore/LICENSES/MIT-0.txt': b'pinned runtime license fixture',
        }
        for name, data in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        env = dict(os.environ, SDK_ROOT=str(root / '.deps/openfpgaos-sdk'), BUNDLE_LOCAL_CARTS='0')
        subprocess.run(['bash', str(script)], env=env, check=True, capture_output=True)
        self.tree = root / 'build/pocket/pico8pocket'

    def test_release_metadata_does_not_advertise_cartridge_hardware(self):
        """Publish the install URL and describe the actual system-menu controls."""
        core = json.loads((self.tree / 'Cores/zenibako.pico8/core.json').read_text())['core']
        self.assertEqual(core['framework']['hardware']['cartridge_adapter'], -1)
        self.assertEqual(core['metadata']['url'], 'https://github.com/zenibako/pico8pocket')
        mappings = json.loads((self.tree / 'Cores/zenibako.pico8/input.json').read_text())['input']['controllers'][0]['mappings']
        labels = {item['key']: item['name'] for item in mappings}
        self.assertEqual(labels['pad_btn_select'], 'System Menu')
        self.assertEqual(labels['pad_trig_l'], 'L')
        self.assertEqual(labels['pad_trig_r'], 'R')

    def test_ai_declaration_is_in_assembled_tree(self):
        """Keep the public AI declaration where catalog updaters read it."""
        path = self.tree / 'Cores/zenibako.pico8/updaters.json'
        self.assertEqual(json.loads(path.read_text())['declare_ai'], ['OpenAI Codex', 'Claude Code'])

    def test_dependency_notices_survive_assembly(self):
        """Retain the dependency notices already shipped in v0.0.34."""
        core = self.tree / 'Cores/zenibako.pico8'
        expected = {
            'miniz-LICENSE.txt': b'miniz license fixture',
            'z8lua-LICENSE-SOURCE.txt': b'Lua header and copyright fixture',
            'Eris-LICENSE.txt': b'/*\nEris copyright fixture\n*/\n',
            'openfpgaCore-NOTICE.txt': b'pinned runtime notice fixture',
            'runtime-licenses/MIT-0.txt': b'pinned runtime license fixture',
        }
        for name, contents in expected.items():
            with self.subTest(notice=name):
                self.assertTrue((core / name).is_file(), f'missing packaged notice: {name}')
                self.assertEqual((core / name).read_bytes(), contents)

    def test_validation_rejects_missing_dependency_notice(self):
        """Reject a package after a required dependency notice is removed."""
        (self.tree / 'Cores/zenibako.pico8/Eris-LICENSE.txt').unlink(missing_ok=True)
        result = subprocess.run(['bash', str(ROOT / 'scripts/validate-pocket.sh'), str(self.tree)],
                                env=dict(os.environ, BUNDLE_LOCAL_CARTS='0'), capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'Missing release notice: Eris-LICENSE.txt', result.stderr)

    def test_validation_rejects_missing_runtime_licenses(self):
        """Reject an otherwise complete package without runtime license texts."""
        shutil.rmtree(self.tree / 'Cores/zenibako.pico8/runtime-licenses', ignore_errors=True)
        result = subprocess.run(['bash', str(ROOT / 'scripts/validate-pocket.sh'), str(self.tree)],
                                env=dict(os.environ, BUNDLE_LOCAL_CARTS='0'), capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'Missing runtime license texts', result.stderr)

    def test_game_association_selects_carts_not_internal_resources(self):
        """Expose real carts to Pocket Sync's first-required-filtered-slot selector."""
        path = self.tree / 'Cores/zenibako.pico8/data.json'
        slots = json.loads(path.read_text())['data']['data_slots']
        # Pocket Sync CoreFolderItem chooses the first required slot with filters.
        selected = next(slot for slot in slots if slot['required'] and 'extensions' in slot)
        self.assertEqual(selected['id'], 4)
        self.assertEqual(selected['extensions'], ['p8', 'png'])
        candidates = ['game.p8', 'game.p8.png', 'pico8pocket.json', 'app.elf', 'game.zip']
        accepted = [name for name in candidates if any(name.endswith('.' + ext) for ext in selected['extensions'])]
        self.assertEqual(accepted, ['game.p8', 'game.p8.png'])
        self.assertEqual([slot['id'] for slot in slots], [0,1,2,3,4,5,6,7,8,10,11,12,13,14,15,16,17,18,19])
        self.assertEqual(slots[0]['filename'], 'pico8pocket.json')
        self.assertEqual(slots[0]['parameters'], 275)
        self.assertEqual(selected['parameters'], 129)
        # Fixed-name slots must not masquerade as user-selectable game formats.
        for slot in slots:
            if slot['id'] in (0,1,2,3,5,6,7):
                self.assertNotIn('extensions', slot)
        instance = json.loads((self.tree / 'Assets/pico8/zenibako.pico8/pico8pocket.json').read_text())['instance']
        names = {slot['id']: slot['filename'] for slot in instance['data_slots']}
        self.assertEqual(names[8], 'pico8pocket.cfg')
        for index in range(10):
            self.assertEqual(names[10+index], f'pico8pocket_{index}.sav')
        subprocess.run(['bash', str(ROOT / 'scripts/validate-pocket.sh'), str(self.tree)],
                       env=dict(os.environ, BUNDLE_LOCAL_CARTS='0'), check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
