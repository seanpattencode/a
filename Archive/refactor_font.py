
import os
import sys

new_code = r'''def handle_font_command(args):
    """Handle 'aio font' command."""
    import shutil, subprocess as sp, re
    term = "termux" if os.environ.get('TERMUX_VERSION') else \
           "gnome" if (os.environ.get('GNOME_TERMINAL_SCREEN') or os.environ.get('VTE_VERSION')) and shutil.which('gsettings') else \
           "alacritty" if os.environ.get('ALACRITTY_WINDOW_ID') else None

    if not term: print("✗ Unknown terminal"); return

    arg = args[0] if args else None
    delta = int(args[1]) if len(args)>1 else 2
    
    if term == 'termux':
        print("Termux: Use pinch-to-zoom or Ctrl+Alt+డాది"); return

    try:
        if term == 'gnome':
            p = sp.check_output(['gsettings','get','org.gnome.Terminal.ProfilesList','default'],text=True).strip().strip("'")
            k = f'org.gnome.Terminal.Legacy.Profile:/org/gnome/terminal/legacy/profiles:/:{p}/'
            f = sp.check_output(['gsettings','get',k,'font'],text=True).strip().strip("'")
            name, size = f.rsplit(' ', 1)
            sz = int(size)
            if not arg: print(f"Size: {sz}"); return
            if arg in ('+','up'): sz+=delta
            elif arg in ('-','down'): sz-=delta
            elif arg.isdigit(): sz=int(arg)
            sp.run(['gsettings','set',k,'font',f"{name} {sz}"])
            print(f"✓ Size: {sz}")

        elif term == 'alacritty':
            cfg = next((p for p in [os.path.expanduser(x) for x in ["~/.config/alacritty/alacritty.toml", "~/.alacritty.toml"]] if os.path.exists(p)), None)
            if not cfg: print("No alacritty config found"); return
            c = open(cfg).read()
            m = re.search(r'(size\s*[=:]\s*)(\d+)', c)
            sz = int(m.group(2)) if m else 12
            if not arg: print(f"Size: {sz}"); return
            if arg in ('+','up'): sz+=delta
            elif arg in ('-','down'): sz-=delta
            elif arg.isdigit(): sz=int(arg)
            
            if m: new_c = re.sub(r'(size\s*[=:]\s*)\d+', f'\\g<1>{sz}', c) # This is a raw string, so \g is fine
            else: new_c = c + f"\n[font]\nsize = {sz}\n"
            open(cfg,'w').write(new_c)
            print(f"✓ Size: {sz}")
    except Exception as e:
        print(f"✗ Error: {e}")
'''

with open('aio.py', 'r') as f:
    lines = f.readlines()

start = -1
end = -1
for i, line in enumerate(lines):
    if 'class FontController:' in line:
        start = i
    if 'def launch_in_new_window(' in line:
        end = i
        break

if start != -1 and end != -1:
    lines[start:end] = [new_code + '\n']
    with open('aio.py', 'w') as f:
        f.writelines(lines)
    print(f"Replaced lines {start} to {end}")
else:
    print("Could not find block")
