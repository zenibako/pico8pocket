"""Check release metadata using the real SD-tree assembler and synthetic inputs."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

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
            'src/app/pico8pocket.ini': b'ELF=pico8pocket.elf\n',
            'assets/p8p_slot_pad.bin': b'padding fixture',
            'assets/cards/README.txt': b'Provide your own carts.',
            'LICENSE': b'license fixture',
            'THIRD_PARTY_NOTICES.md': b'notice fixture',
            '.deps/openfgpaSDK/LICENSE': b'SDK license fixture',
            '.deps/openfgpaSDK/NOTICE': b'SDK notice fixture',
            '.deps/fake-08/LICENSE.MD': b'Fake-08 license fixture',
        }
        for name, data in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        env = dict(os.environ, SDK_ROOT=str(root / '.deps/openfpgaos-sdk'), BUNDLE_LOCAL_CARTS='0')
        env.pop('CORE_VERSION', None)
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
        self.assertEqual(labels['pad_btn_start'], 'Cart Menu')
        self.assertEqual(labels['pad_trig_l'], 'L')
        self.assertEqual(labels['pad_trig_r'], 'R')

    def test_ai_declaration_is_in_assembled_tree(self):
        """Keep the public AI declaration where catalog updaters read it."""
        path = self.tree / 'Cores/zenibako.pico8/updaters.json'
        self.assertEqual(json.loads(path.read_text())['declare_ai'], ['OpenAI Codex', 'Claude Code'])

    def test_plugin_recommendation_survives_public_packaging(self):
        """Keep the installer recommendation in the installable public core."""
        metadata = 'Cores/zenibako.pico8/updaters.json'
        expected = json.loads((ROOT / 'dist/core/updaters.json').read_text())
        self.assertEqual(expected['plugins'], ['https://github.com/zenibako/pico8pocket'])
        root = Path(self.temporary.name)
        public_zip = Path(shutil.make_archive(str(root / 'public'), 'zip', self.tree))
        with zipfile.ZipFile(public_zip) as packaged:
            self.assertEqual(json.loads(packaged.read(metadata)), expected)

    def test_plugin_recommendation_survives_variant_packaging(self):
        """Retain updater metadata when the real variant script renames a core."""
        root = Path(self.temporary.name)
        env = dict(os.environ)
        realpath = shutil.which('realpath')
        supports_missing = realpath and subprocess.run(
            [realpath, '-m', str(root / 'variant.zip')], capture_output=True).returncode == 0
        if not supports_missing:
            grealpath = shutil.which('grealpath')
            if not grealpath:
                self.skipTest('Variant packaging requires GNU realpath (-m)')
            tools = root / 'tools'
            tools.mkdir()
            (tools / 'realpath').symlink_to(grealpath)
            env['PATH'] = str(tools) + os.pathsep + env.get('PATH', '')
        public_zip = Path(shutil.make_archive(str(root / 'public'), 'zip', self.tree))
        variant_zip = root / 'variant.zip'
        subprocess.run(['bash', str(ROOT / 'scripts/variant-core.sh'), str(public_zip),
                        'b', 'Metadata test variant', str(variant_zip)],
                       env=env, check=True, capture_output=True)
        expected = json.loads((ROOT / 'dist/core/updaters.json').read_text())
        with zipfile.ZipFile(variant_zip) as packaged:
            self.assertEqual(json.loads(packaged.read('Cores/zenibako.pico8b/updaters.json')), expected)

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
        self.assertEqual([slot['id'] for slot in slots], [0,1,2,3,4,5,6,7,8,10,11,12,13,14,15,16,17,18,19,20])
        self.assertEqual(slots[0]['filename'], 'pico8pocket.json')
        self.assertEqual(slots[0]['parameters'], 275)
        self.assertEqual(selected['parameters'], 1)
        # Fixed-name slots must not masquerade as user-selectable game formats.
        for slot in slots:
            if slot['id'] in (0,1,2,3,5,6,7):
                self.assertNotIn('extensions', slot)
        instance = json.loads((self.tree / 'Assets/pico8/zenibako.pico8/pico8pocket.json').read_text())['instance']
        names = {slot['id']: slot['filename'] for slot in instance['data_slots']}
        offline = slots[-1]
        self.assertEqual(offline['id'], 20)
        self.assertIs(offline['required'], False)
        self.assertEqual(offline['parameters'], 8)
        self.assertIs(offline['deferload'], True)
        self.assertEqual(offline['filename'], 'cards/bbs.p8pack')
        self.assertEqual(offline['extensions'], ['p8pack'])
        self.assertEqual(names[20], 'cards/bbs.p8pack')
        self.assertFalse((self.tree / 'Assets/pico8/common/cards/bbs.p8pack').exists())
        self.assertEqual(names[8], 'pico8pocket.cfg')
        for index in range(10):
            self.assertEqual(names[10+index], f'pico8pocket_{index}.sav')
        subprocess.run(['bash', str(ROOT / 'scripts/validate-pocket.sh'), str(self.tree)],
                       env=dict(os.environ, BUNDLE_LOCAL_CARTS='0'), check=True, capture_output=True)

    def test_test_build_version_replaces_only_the_packaged_version(self):
        """A test build reports its -rc.N version; the committed core.json keeps the release."""
        root = Path(self.temporary.name)
        script = root / 'scripts/assemble-pocket.sh'
        env = dict(os.environ, SDK_ROOT=str(root / '.deps/openfpgaos-sdk'), BUNDLE_LOCAL_CARTS='0')
        env.pop('CORE_VERSION', None)
        released = json.loads((self.tree / 'Cores/zenibako.pico8/core.json').read_text())
        self.assertEqual(released['core']['metadata']['version'],
                         json.loads((ROOT / 'dist/core/core.json').read_text())['core']['metadata']['version'])
        subprocess.run(['bash', str(script)], env=dict(env, CORE_VERSION='9.8.7-rc.6'),
                       check=True, capture_output=True)
        tested = json.loads((self.tree / 'Cores/zenibako.pico8/core.json').read_text())
        self.assertEqual(tested['core']['metadata']['version'], '9.8.7-rc.6')
        released['core']['metadata']['version'] = '9.8.7-rc.6'
        self.assertEqual(tested, released)
        bad = subprocess.run(['bash', str(script)], env=dict(env, CORE_VERSION='9.8.7 test'),
                             capture_output=True)
        self.assertNotEqual(bad.returncode, 0)
        long = subprocess.run(['bash', str(script)], env=dict(env, CORE_VERSION='1.2.3-rc.' + '9' * 23),
                              capture_output=True)
        self.assertNotEqual(long.returncode, 0)
        # Rejected before the previous tree is removed.
        self.assertTrue((self.tree / 'Cores/zenibako.pico8/core.json').is_file())


class PackageTestVersionTests(unittest.TestCase):
    """make package-test takes its version from pocket-dev-tools."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.tools = Path(self.temp.name)

    def package_test(self, tools):
        return subprocess.run(["make", "-C", str(ROOT), "package-test", f"POCKET_DEV_TOOLS_ROOT={tools}"], capture_output=True, text=True)

    def test_explains_a_missing_checkout(self):
        result = self.package_test(self.tools / "missing")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("POCKET_DEV_TOOLS_ROOT", result.stderr)

    def test_stops_when_the_shared_script_fails(self):
        # A stand-in; pocket-dev-tools tests the real script.
        (self.tools / "pocket_version.py").write_text(
            "import sys\nprint('pocket_version: no tag', file=sys.stderr)\nsys.exit(1)\n")
        result = self.package_test(self.tools)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("pocket_version: no tag", result.stderr)
        self.assertNotIn("make[1]", result.stdout)  # package never ran


if __name__ == '__main__':
    unittest.main()
