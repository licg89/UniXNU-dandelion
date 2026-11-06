#!/usr/bin/env python3
"""Tests for scripts/build-data-seed.py.

The parts that need `hdiutil` cannot run here; everything else can, and one of
it was wrong: the copy loop wrote `Path.symlink` where the method is
`symlink_to`, which only shows up once the OS volume's tree actually holds a
symlink. That is what these cover.

Run: python3 scripts/test_build_data_seed.py -v
"""
from __future__ import annotations

import importlib.util
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('build_data_seed', HERE / 'build-data-seed.py')
seed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(seed)


def make_tree(root: Path) -> None:
    """An OS volume's private/var with a file, a symlink, a mode and a skipped dir."""
    (root / 'private/var/mobile/Downloads').mkdir(parents=True)
    (root / 'private/var/staged_system_apps').mkdir()
    (root / 'private/var/preferences').mkdir()
    (root / 'sbin').mkdir()
    (root / 'sbin/launchd').write_bytes(b'launchd-binary')
    (root / 'private/var/mobile/file.bin').write_bytes(b'x' * 4096)
    (root / 'private/var/mobile/link').symlink_to('file.bin')
    (root / 'private/var/staged_system_apps/huge.bin').write_bytes(b'y' * 99999)
    (root / 'private/var/preferences/.GlobalPreferences.plist').write_bytes(b'<plist/>')
    os.chmod(root / 'private/var/mobile/Downloads', 0o755)


class TreeBytesTest(unittest.TestCase):
    def test_counts_files_but_not_the_skipped_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_tree(root)

            got = seed.tree_bytes(root / 'private/var')

            self.assertEqual(got, 4096 + len(b'<plist/>'))

    def test_symlinks_are_not_counted_as_content(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_tree(root)
            before = seed.tree_bytes(root / 'private/var')
            (root / 'private/var/mobile/another').symlink_to('file.bin')

            self.assertEqual(seed.tree_bytes(root / 'private/var'), before)


class CopyVarTest(unittest.TestCase):
    def test_copies_files_recreates_symlinks_and_skips_the_named_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, out = Path(tmp) / 'src', Path(tmp) / 'dst'
            make_tree(root)
            out.mkdir()

            count, total = seed.copy_var(root / 'private/var', out)

            self.assertEqual(count, 2)
            self.assertEqual(total, 4096 + len(b'<plist/>'))
            self.assertEqual((out / 'mobile/file.bin').read_bytes(), b'x' * 4096)
            self.assertTrue((out / 'mobile/link').is_symlink())
            self.assertEqual(os.readlink(out / 'mobile/link'), 'file.bin')
            self.assertFalse((out / 'staged_system_apps').exists())

    def test_preserves_the_source_directory_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, out = Path(tmp) / 'src', Path(tmp) / 'dst'
            make_tree(root)
            out.mkdir()

            seed.copy_var(root / 'private/var', out)

            # Compare against the source rather than a literal: Windows does
            # not carry POSIX mode bits, so the literal would only pass on macOS.
            want = stat.S_IMODE((root / 'private/var/mobile/Downloads').stat().st_mode)
            self.assertEqual(stat.S_IMODE((out / 'mobile/Downloads').stat().st_mode), want)


class CheckMountTest(unittest.TestCase):
    def test_accepts_a_plausible_os_volume(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_tree(root)

            digest = seed.check_mount(root)

            self.assertEqual(len(digest), 64)

    def test_rejects_a_mount_without_launchd(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_tree(root)
            (root / 'sbin/launchd').unlink()

            with self.assertRaises(SystemExit):
                seed.check_mount(root)

    def test_rejects_a_mount_with_an_empty_var_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_tree(root)
            for name in ('mobile', 'preferences', 'staged_system_apps'):
                (root / 'private/var' / name).rename(root / name)

            with self.assertRaises(SystemExit):
                seed.check_mount(root)


class ContainerTest(unittest.TestCase):
    def write(self, path: Path, blocks: int) -> None:
        header = bytearray(4096)
        header[32:36] = b'NXSB'
        header[36:40] = (4096).to_bytes(4, 'little')
        header[40:48] = blocks.to_bytes(8, 'little')
        path.write_bytes(header)

    def test_accepts_a_container_that_fills_the_image(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / 'seed.img'
            self.write(image, 256 * 256)

            self.assertEqual(seed.verify_container(image, 256), 256 * 1024 * 1024)

    def test_rejects_an_image_without_a_container(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / 'seed.img'
            image.write_bytes(bytes(4096))

            with self.assertRaises(SystemExit):
                seed.verify_container(image, 256)

    def test_rejects_a_container_that_does_not_fill_the_image(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / 'seed.img'
            self.write(image, 256 * 256)

            with self.assertRaises(SystemExit):
                seed.verify_container(image, 512)


if __name__ == '__main__':
    unittest.main()
