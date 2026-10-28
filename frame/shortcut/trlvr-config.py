#!/usr/bin/env python3
# LegendFrame: add (or remove) the "Tomb Raider: Legend VR" non-Steam shortcut on a Steam Frame.
#
# Run while Steam is stopped. install.sh queues it as a steam.service drop-in so that it runs
# right before the next Steam start; it can also be run by hand.
#
# The shortcut (OpenVR=1) runs trlvr_launcher.exe from the Steam copy of Tomb Raider: Legend under
# Proton 11 ARM64 in the store game's own prefix (compatdata/7000). The store entry (app 7000) is
# mapped to the same Proton tool so that both share one prefix and one set of settings.
#
# Usage:
#   trlvr-config.py --game-dir DIR [--steam-dir DIR] [--userid N] [--tool NAME] [--name NAME]
#                   [--dropin FILE] [--dry-run] [--force]
#   trlvr-config.py --remove  --game-dir DIR [...]    remove the shortcut and its own tool mapping
#   trlvr-config.py --check   --game-dir DIR [...]    exit 0 when the shortcut is already in place
#   trlvr-config.py --detect  [--steam-dir DIR]       print the Steam user id and exit
#
# --steam-dir may point at a copy of the Steam tree for a dry run. Art is read from the real
# library cache under --game-dir's library, and written to the (possibly copied) userdata grid.
import argparse, glob, os, re, shutil, subprocess, sys, time, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vdf

APPID = '7000'
DEFAULT_NAME = 'Tomb Raider: Legend VR'
DEFAULT_TOOL = 'proton_11-arm64'
# Fields of a fresh shortcut, in the order Steam writes them.
TEMPLATE = {
    'appid': 0, 'AppName': '', 'Exe': '', 'StartDir': '', 'icon': '', 'ShortcutPath': '',
    'LaunchOptions': '', 'IsHidden': 0, 'AllowDesktopConfig': 1, 'AllowOverlay': 1, 'OpenVR': 1,
    'Devkit': 0, 'DevkitGameID': '', 'DevkitOverrideAppID': 0, 'LastPlayTime': 0,
    'FlatpakAppID': '', 'sortas': '', 'tags': {},
}


def die(msg):
    print('error: ' + msg, file=sys.stderr)
    sys.exit(2)


def detect_userid(steam):
    ud = os.path.join(steam, 'userdata')
    with_sc = sorted(p for p in glob.glob(os.path.join(ud, '*', 'config', 'shortcuts.vdf'))
                     if os.path.basename(os.path.dirname(os.path.dirname(p))).isdigit())
    ids = [os.path.basename(os.path.dirname(os.path.dirname(p))) for p in with_sc]
    if len(ids) != 1:
        # shortcuts.vdf only exists once a shortcut has been added; fall back to account folders.
        ids = sorted(d for d in os.listdir(ud) if d.isdigit() and d != '0'
                     and os.path.isdir(os.path.join(ud, d, 'config'))) if os.path.isdir(ud) else []
    if len(ids) != 1:
        die('cannot pick a Steam user automatically (%d candidates under %s); pass --userid'
            % (len(ids), ud))
    return ids[0]


# ---- Steam text VDF (config.vdf) ----------------------------------------------------------
TOK = re.compile(r'"((?:[^"\\]|\\.)*)"|([{}])|(//[^\n]*)|\s+')


def parse(s):
    toks = [m for m in TOK.finditer(s) if m.group(1) is not None or m.group(2)]

    def obj(i):
        o = {}
        while i < len(toks):
            m = toks[i]
            if m.group(2) == '}':
                return o, i + 1
            k = m.group(1)
            n = toks[i + 1]
            if n.group(2) == '{':
                o[k], i = obj(i + 2)
            else:
                o[k] = n.group(1)
                i += 2
        return o, i
    return obj(0)[0]


def dump(o, d=0):
    t = '\t' * d
    out = ''
    for k, v in o.items():
        if isinstance(v, dict):
            out += f'{t}"{k}"\n{t}{{\n{dump(v, d + 1)}{t}}}\n'
        else:
            out += f'{t}"{k}"\t\t"{v}"\n'
    return out


def path(o, *keys):
    for k in keys:
        # Steam's key case varies ("Steam" vs "steam"); match case-insensitively
        o = next((v for kk, v in o.items() if kk.lower() == k.lower()), None) or o.setdefault(k, {})
    return o


def save_text(p, root):
    shutil.copy2(p, p + f'.bak-{int(time.time())}')
    open(p + '.tmp', 'w').write(dump(root))
    os.replace(p + '.tmp', p)


def steam_running():
    return subprocess.run(['pgrep', '-x', 'steam'], stdout=subprocess.DEVNULL).returncode == 0


def main():
    ap = argparse.ArgumentParser(description='Add or remove the LegendFrame Tomb Raider: Legend VR Steam shortcut.')
    ap.add_argument('--steam-dir', default=os.path.expanduser('~/.local/share/Steam'))
    ap.add_argument('--game-dir', help='Tomb Raider: Legend install folder (steamapps/common/...)')
    ap.add_argument('--userid', help='Steam user id (folder under userdata); auto when only one')
    ap.add_argument('--tool', default=DEFAULT_TOOL, help='Proton tool name (default %(default)s)')
    ap.add_argument('--name', default=DEFAULT_NAME)
    ap.add_argument('--dropin', help='steam.service drop-in to delete once applied')
    ap.add_argument('--remove', action='store_true')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--detect', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--force', action='store_true', help='run even while Steam is running')
    a = ap.parse_args()

    steam = os.path.abspath(a.steam_dir)
    userid = a.userid or detect_userid(steam)
    if a.detect:
        print(userid)
        return 0
    if not a.game_dir:
        die('--game-dir is required')
    game = os.path.abspath(a.game_dir)
    library = os.path.dirname(os.path.dirname(game))            # .../steamapps
    compat = os.path.join(library, 'compatdata', APPID)
    real_steam = os.path.expanduser('~/.local/share/Steam')
    cache = os.path.join(steam, 'appcache/librarycache', APPID)
    if not os.path.isdir(cache):
        cache = os.path.join(real_steam, 'appcache/librarycache', APPID)

    config = os.path.join(steam, 'config/config.vdf')
    cfgdir = os.path.join(steam, 'userdata', userid, 'config')
    shortcuts = os.path.join(cfgdir, 'shortcuts.vdf')
    grid = os.path.join(cfgdir, 'grid')

    exe = f'"{game}/trlvr_launcher.exe"'
    sid = zlib.crc32((exe + a.name).encode()) | 0x80000000
    icon = os.path.join(grid, f'{sid}_icon.jpg')
    # presentInterval=0: the flat mirror-window present must not block the game after WaitGetPoses.
    # The mod's d3d9.dll proxy must load native (d3d9=n,b); SteamAppId=7000 so Steamworks sees TRL.
    opts = (f'STEAM_COMPAT_DATA_PATH="{compat}" WINEDLLOVERRIDES="d3d9=n,b" '
            'DXVK_CONFIG="d3d9.presentInterval = 0" SteamAppId=7000 %command%')

    sc_root = vdf.load(shortcuts) if os.path.exists(shortcuts) else {'shortcuts': {}}
    sc = sc_root['shortcuts']
    have = next((k for k, e in sc.items() if e.get('appid') == sid), None)

    if a.check:
        ok = have is not None and sc[have].get('LaunchOptions') == opts and sc[have].get('Exe') == exe
        if ok and os.path.exists(config):
            m = path(parse(open(config).read()), 'InstallConfigStore', 'Software', 'Valve', 'Steam',
                     'CompatToolMapping')
            ok = all(m.get(k, {}).get('name') == a.tool for k in (str(sid), APPID))
        return 0 if ok else 1

    if steam_running() and not a.force and not a.dry_run \
            and os.path.realpath(steam) == os.path.realpath(real_steam):
        die('Steam is running; it would overwrite the change. Quit Steam first (or pass --force).')

    if a.dry_run:
        print('dry run: user %s, shortcut appid %d, %s' % (userid, sid, 'remove' if a.remove else 'add'))
        return 0

    if a.remove:
        if os.path.exists(config):
            c = parse(open(config).read())
            m = path(c, 'InstallConfigStore', 'Software', 'Valve', 'Steam', 'CompatToolMapping')
            if str(sid) in m:
                del m[str(sid)]
                save_text(config, c)
        if have is not None:
            del sc[have]
            # Steam keys shortcuts by consecutive index; renumber what is left.
            sc_root['shortcuts'] = {str(i): e for i, e in enumerate(sc.values())}
            shutil.copy2(shortcuts, shortcuts + f'.bak-{int(time.time())}')
            open(shortcuts + '.tmp', 'wb').write(b'\0shortcuts\0' + vdf.dump(sc_root['shortcuts']) + b'\x08')
            os.replace(shortcuts + '.tmp', shortcuts)
        for suffix in ('p.jpg', '.jpg', '_hero.jpg', '_logo.png', '_icon.jpg'):
            try:
                os.remove(os.path.join(grid, f'{sid}{suffix}'))
            except FileNotFoundError:
                pass
        print('legendframe shortcut removed', sid)
    else:
        if not os.path.exists(config):
            die('Steam config not found: ' + config)
        c = parse(open(config).read())
        m = path(c, 'InstallConfigStore', 'Software', 'Valve', 'Steam', 'CompatToolMapping')
        for k in (str(sid), APPID):
            m[k] = {'name': a.tool, 'config': '', 'priority': '250'}
        save_text(config, c)

        entry = dict(TEMPLATE, appid=sid, AppName=a.name, Exe=exe, StartDir=f'"{game}/"', icon=icon,
                     LaunchOptions=opts, OpenVR=1, tags={})
        if have is None:
            sc[str(len(sc))] = entry
        else:
            keep = {k: sc[have].get(k, entry[k]) for k in ('LastPlayTime', 'tags', 'IsHidden', 'sortas')}
            sc[have] = dict(sc[have], **dict(entry, **keep))
        os.makedirs(cfgdir, exist_ok=True)
        if os.path.exists(shortcuts):
            shutil.copy2(shortcuts, shortcuts + f'.bak-{int(time.time())}')
        open(shortcuts + '.tmp', 'wb').write(b'\0shortcuts\0' + vdf.dump(sc) + b'\x08')
        os.replace(shortcuts + '.tmp', shortcuts)

        hashed = sorted(glob.glob(os.path.join(cache, '[0-9a-f]' * 40 + '.jpg')))
        art = {f'{sid}p.jpg': f'{cache}/library_600x900.jpg', f'{sid}.jpg': f'{cache}/header.jpg',
               f'{sid}_hero.jpg': f'{cache}/library_hero.jpg', f'{sid}_logo.png': f'{cache}/logo.png'}
        if hashed:
            art[f'{sid}_icon.jpg'] = hashed[0]
        os.makedirs(grid, exist_ok=True)
        for dst, src in art.items():
            if os.path.exists(src) and not os.path.exists(os.path.join(grid, dst)):
                shutil.copy2(src, os.path.join(grid, dst))
        print('legendframe shortcut applied', sid)

    if a.dropin:
        try:
            os.remove(a.dropin)
        except FileNotFoundError:
            pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
