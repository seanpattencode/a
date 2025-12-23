#!/usr/bin/env python3
"""
AIO Setup Script
Automatically installs dependencies and the 'aio' command on Termux and Ubuntu proot.

Usage:
    python3 setup.py          # Full install (deps + command)
    python3 setup.py deps     # Install dependencies only
    python3 setup.py install  # Install aio command only
"""
import os
import sys
import subprocess as sp
import shutil
import platform

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
AIO_SCRIPT = os.path.join(SCRIPT_DIR, 'aio.py')

def detect_env():
    """Detect if running on Termux, Ubuntu proot, or native Linux."""
    # Check for Termux native
    if os.path.exists('/data/data/com.termux/files/usr/bin/pkg'):
        # Check if inside proot (Ubuntu filesystem)
        if os.path.exists('/etc/debian_version') and os.path.exists('/usr/bin/apt'):
            return 'proot'  # Ubuntu proot inside Termux
        return 'termux'
    # Native Ubuntu/Debian
    if os.path.exists('/etc/debian_version') or shutil.which('apt-get'):
        return 'ubuntu'
    return 'unknown'

def run_cmd(cmd, check=False, capture=True):
    """Run command and return (success, output)."""
    try:
        result = sp.run(cmd, capture_output=capture, text=True, check=check)
        return result.returncode == 0, result.stdout + result.stderr
    except Exception as e:
        return False, str(e)

def which(cmd):
    return shutil.which(cmd) is not None

def install_system_deps(env):
    """Install system dependencies based on environment."""
    print("\n=== Installing System Dependencies ===\n")

    if env == 'termux':
        pkg_cmd = ['pkg']
        packages = ['tmux', 'git', 'python', 'curl']
        update_cmd = ['pkg', 'update', '-y']
    elif env in ('proot', 'ubuntu'):
        need_sudo = os.geteuid() != 0 if hasattr(os, 'geteuid') else False
        pkg_cmd = ['sudo', 'apt-get'] if need_sudo else ['apt-get']
        packages = ['tmux', 'git', 'python3', 'python3-pip', 'curl']
        update_cmd = pkg_cmd + ['update']
    else:
        print("Unknown environment, skipping system deps")
        return

    # Update package cache
    print("Updating package cache...")
    run_cmd(update_cmd)

    for pkg in packages:
        if which(pkg.replace('python3-pip', 'pip3').replace('python3', 'python3').replace('python', 'python')):
            print(f"  [ok] {pkg}")
        else:
            print(f"  [installing] {pkg}...")
            if env == 'termux':
                ok, out = run_cmd(['pkg', 'install', '-y', pkg])
            else:
                ok, out = run_cmd(pkg_cmd + ['install', '-y', pkg])
            print(f"  {'[ok]' if ok else '[fail]'} {pkg}")

def install_python_deps(env):
    """Install Python dependencies."""
    print("\n=== Installing Python Dependencies ===\n")

    pip_deps = [
        ('pexpect', 'pexpect', 'python3-pexpect'),
        ('prompt_toolkit', 'prompt-toolkit', 'python3-prompt-toolkit')
    ]

    for import_name, pip_name, apt_name in pip_deps:
        try:
            __import__(import_name)
            print(f"  [ok] {pip_name}")
            continue
        except ImportError:
            pass

        print(f"  [installing] {pip_name}...")
        installed = False

        # Try pip first
        pip_cmds = [
            [sys.executable, '-m', 'pip', 'install', '--user', pip_name],
            ['pip3', 'install', '--user', pip_name],
            ['pip', 'install', '--user', pip_name],
        ]

        for pip_cmd in pip_cmds:
            if not which(pip_cmd[0]) and pip_cmd[0] != sys.executable:
                continue
            ok, _ = run_cmd(pip_cmd)
            if ok:
                print(f"  [ok] {pip_name} (pip)")
                installed = True
                break

        # Try pkg/apt if pip failed
        if not installed:
            if env == 'termux':
                ok, _ = run_cmd(['pkg', 'install', '-y', import_name])
                if ok:
                    print(f"  [ok] {pip_name} (pkg)")
                    installed = True
            elif env in ('proot', 'ubuntu'):
                need_sudo = os.geteuid() != 0 if hasattr(os, 'geteuid') else False
                apt_cmd = ['sudo', 'apt-get'] if need_sudo else ['apt-get']
                ok, _ = run_cmd(apt_cmd + ['install', '-y', apt_name])
                if ok:
                    print(f"  [ok] {pip_name} (apt)")
                    installed = True

        if not installed:
            print(f"  [warn] {pip_name} - install manually: pip install {pip_name}")

def install_node_npm(env):
    """Install Node.js and npm if not present."""
    print("\n=== Checking Node.js/npm ===\n")

    if which('npm'):
        print("  [ok] npm already installed")
        return True

    node_dir = os.path.expanduser('~/.local/node')
    node_bin = os.path.join(node_dir, 'bin')
    npm_path = os.path.join(node_bin, 'npm')

    if os.path.exists(npm_path):
        print("  [ok] npm installed at ~/.local/node")
        return True

    # On Termux, use pkg
    if env == 'termux':
        print("  [installing] nodejs via pkg...")
        ok, _ = run_cmd(['pkg', 'install', '-y', 'nodejs'])
        if ok:
            print("  [ok] nodejs (pkg)")
            return True

    # Download binary for other envs
    print("  [installing] nodejs (binary download)...")
    try:
        import urllib.request
        import tarfile
        import lzma

        arch = 'x64' if platform.machine() in ('x86_64', 'AMD64') else 'arm64'
        url = f'https://nodejs.org/dist/v22.11.0/node-v22.11.0-linux-{arch}.tar.xz'
        xz_path = '/tmp/node.tar.xz'

        print(f"  Downloading {url}...")
        urllib.request.urlretrieve(url, xz_path)

        os.makedirs(os.path.expanduser('~/.local'), exist_ok=True)
        with lzma.open(xz_path) as xz:
            with tarfile.open(fileobj=xz) as tar:
                tar.extractall(os.path.expanduser('~/.local'), filter='data')

        extracted = os.path.expanduser(f'~/.local/node-v22.11.0-linux-{arch}')
        if os.path.exists(node_dir):
            shutil.rmtree(node_dir)
        os.rename(extracted, node_dir)
        os.remove(xz_path)

        # Symlink to bin
        bin_dir = os.path.expanduser('~/.local/bin')
        os.makedirs(bin_dir, exist_ok=True)
        for cmd in ['node', 'npm', 'npx']:
            src = os.path.join(node_bin, cmd)
            dst = os.path.join(bin_dir, cmd)
            if os.path.exists(dst):
                os.remove(dst)
            os.symlink(src, dst)

        print("  [ok] nodejs installed to ~/.local/node")
        return True
    except Exception as e:
        print(f"  [fail] nodejs: {e}")
        return False

def install_ai_tools():
    """Install AI CLI tools (codex, claude, gemini)."""
    print("\n=== Installing AI CLI Tools ===\n")

    # Find npm
    npm_cmd = 'npm'
    local_npm = os.path.expanduser('~/.local/node/bin/npm')
    if os.path.exists(local_npm):
        npm_cmd = local_npm
    elif not which('npm'):
        print("  [skip] npm not found, skipping AI tools")
        return

    tools = [
        ('codex', '@openai/codex'),
        ('claude', '@anthropic-ai/claude-code'),
        ('gemini', '@google/gemini-cli'),
    ]

    for cmd, pkg in tools:
        if which(cmd):
            print(f"  [ok] {cmd}")
        else:
            print(f"  [installing] {cmd}...")
            ok, out = run_cmd([npm_cmd, 'install', '-g', pkg])
            print(f"  {'[ok]' if ok else '[fail]'} {cmd}")

def install_aio_command():
    """Install the aio command to ~/.local/bin."""
    print("\n=== Installing aio Command ===\n")

    bin_dir = os.path.expanduser("~/.local/bin")
    aio_link = os.path.join(bin_dir, "aio")

    # Create bin directory
    os.makedirs(bin_dir, exist_ok=True)

    # Make aio.py executable
    os.chmod(AIO_SCRIPT, 0o755)

    # Remove existing symlink
    if os.path.exists(aio_link):
        if os.path.islink(aio_link):
            os.remove(aio_link)
        else:
            print(f"  [fail] {aio_link} exists and is not a symlink")
            return False

    # Create symlink
    os.symlink(AIO_SCRIPT, aio_link)
    print(f"  [ok] Created: {aio_link} -> {AIO_SCRIPT}")

    # Check PATH
    user_path = os.environ.get('PATH', '')
    if bin_dir not in user_path:
        print(f"\n  [action needed] Add ~/.local/bin to PATH")

        # Try to add to bashrc/profile automatically
        added = False
        for rc_file in ['~/.bashrc', '~/.profile', '~/.bash_profile']:
            rc_path = os.path.expanduser(rc_file)
            try:
                if os.path.exists(rc_path):
                    with open(rc_path, 'r') as f:
                        content = f.read()
                    if '.local/bin' not in content:
                        with open(rc_path, 'a') as f:
                            f.write('\n# Added by aio setup\nexport PATH="$HOME/.local/bin:$PATH"\n')
                        print(f"  [ok] Added PATH to {rc_file}")
                        added = True
                        break
                    else:
                        print(f"  [ok] PATH already in {rc_file}")
                        added = True
                        break
            except:
                pass

        # Create bashrc if nothing exists
        if not added:
            rc_path = os.path.expanduser('~/.bashrc')
            try:
                with open(rc_path, 'w') as f:
                    f.write('# Created by aio setup\nexport PATH="$HOME/.local/bin:$PATH"\n')
                print(f"  [ok] Created ~/.bashrc with PATH")
            except Exception as e:
                print(f"  [warn] Could not create ~/.bashrc: {e}")
                print(f'    Run manually: echo \'export PATH="$HOME/.local/bin:$PATH"\' >> ~/.bashrc')
    else:
        print(f"  [ok] ~/.local/bin is already in PATH")

    return True

def main():
    print("=" * 50)
    print("  AIO Setup - All-in-One AI CLI Manager")
    print("=" * 50)

    env = detect_env()
    print(f"\nDetected environment: {env}")

    # Check if aio.py exists
    if not os.path.exists(AIO_SCRIPT):
        print(f"Error: aio.py not found at {AIO_SCRIPT}")
        sys.exit(1)

    args = sys.argv[1:] if len(sys.argv) > 1 else ['all']

    if 'all' in args or 'deps' in args:
        install_system_deps(env)
        install_python_deps(env)
        install_node_npm(env)
        install_ai_tools()

    if 'all' in args or 'install' in args:
        install_aio_command()

    print("\n" + "=" * 50)
    print("  Setup Complete!")
    print("=" * 50)
    print("\nRun 'aio' to get started, or 'aio help' for commands.")
    print("If 'aio' is not found, run: source ~/.bashrc")

if __name__ == '__main__':
    main()
