#!/usr/bin/env python3
"""LegendFrame: seed the Tomb Raider: Legend graphics registry values in a Wine user.reg.

Usage: seed-registry.py USER_REG [--dry-run] [--force]

Seeds EnableDepthOfField, EnableFullscreenEffects, EnableVSync, EnableShadows, EnableReflection,
EnableWaterFX and Fullscreen as 0 in
  [Software\\Crystal Dynamics\\Tomb Raider: Legend\\Graphics]
Only values that are ABSENT are added: a value that already exists (for example Fullscreen=1) is
left alone and listed. --force overwrites existing values too.
The caller must make sure no Wine process is using the prefix. user.reg is backed up first and
nothing is written when there is nothing to add.

Exit status: 0 changed or already correct, 3 the Graphics section does not exist yet (run the
game once), 2 other error. Prints what it added and what it left.
"""
import os, re, shutil, sys, time

SECTION = r'[Software\\Crystal Dynamics\\Tomb Raider: Legend\\Graphics]'
VALUES = ['EnableDepthOfField', 'EnableFullscreenEffects', 'EnableVSync', 'EnableShadows',
          'EnableReflection', 'EnableWaterFX', 'Fullscreen']


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('--')]
    dry = '--dry-run' in sys.argv
    force = '--force' in sys.argv
    if len(args) != 1:
        print(__doc__)
        return 2
    path = args[0]
    if not os.path.isfile(path):
        print('no user.reg at ' + path)
        return 3
    with open(path, 'r', encoding='utf-8', errors='surrogateescape', newline='') as f:
        lines = f.read().split('\n')
    start = next((i for i, l in enumerate(lines) if l.startswith(SECTION + ' ') or l == SECTION), None)
    if start is None:
        print('Graphics section not present yet')
        return 3
    end = start + 1
    while end < len(lines) and not lines[end].startswith('['):
        end += 1
    # keep the blank line(s) that end the section where they are
    insert_at = end
    while insert_at > start + 1 and lines[insert_at - 1].strip() == '':
        insert_at -= 1
    changed = []
    left = []
    for name in VALUES:
        want = '"%s"=dword:00000000' % name
        idx = next((i for i in range(start + 1, end) if lines[i].startswith('"%s"=' % name)), None)
        if idx is None:
            lines.insert(insert_at, want)
            insert_at += 1
            end += 1
            changed.append(name)
        elif lines[idx] != want:
            if force:
                lines[idx] = want
                changed.append(name)
            else:
                left.append('%s (%s)' % (name, lines[idx].split('=', 1)[1]))
    if left:
        print('registry: existing values left as they are: ' + ' '.join(left))
    if not changed:
        print('registry already seeded' if not left else 'registry: nothing to add')
        return 0
    if dry:
        print('would set: ' + ' '.join(changed))
        return 0
    bak = '%s.legendframe-bak-%s' % (path, time.strftime('%Y%m%d-%H%M%S'))
    shutil.copy2(path, bak)
    tmp = path + '.legendframe-tmp'
    with open(tmp, 'w', encoding='utf-8', errors='surrogateescape', newline='') as f:
        f.write('\n'.join(lines))
    os.replace(tmp, path)
    print('registry seeded (%s); backup %s' % (' '.join(changed), bak))
    return 0


if __name__ == '__main__':
    sys.exit(main())
