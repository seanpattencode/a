# Setup a on new device via SSH

## Prerequisites
- `gh` CLI must be installed on the new device
- For fresh installs: need gh token from another device (chicken-and-egg problem)

## Linux/WSL/Termux

```bash
# 1. Clone repo
ssh user@host 'git clone https://github.com/seanpattencode/aio.git ~/projects/a'

# 2. Create symlink
ssh user@host 'mkdir -p ~/.local/bin && ln -sf ~/projects/a/a.py ~/.local/bin/a'

# 3. Update shell (adds PATH to bashrc/zshrc)
ssh user@host 'cd ~/projects/a && python3 a.py update shell'
# Note: open new shell session for PATH changes to take effect

# 4. Apply gh token first (required for sync on fresh installs)
# Get token from existing device: cat ~/projects/a-sync/login/gh_*.txt
ssh user@host 'echo "TOKEN_HERE" | gh auth login --with-token'
ssh user@host 'git config --global credential.helper "$(which gh) auth git-credential"'

# 5. Sync all repos
ssh user@host 'cd ~/projects/a && python3 a.py sync'

# 6. Register to SSH hosts (run ON the new device)
ssh user@host 'cd ~/projects/a && python3 a.py ssh self DEVICENAME PASSWORD'
```

## macOS

macOS needs extra steps since `gh` and `brew` may not be installed.

```bash
# 1. Clone repo
ssh user@host 'git clone https://github.com/seanpattencode/aio.git ~/projects/a'

# 2. Create symlink
ssh user@host 'mkdir -p ~/.local/bin && ln -sf ~/projects/a/a.py ~/.local/bin/a'

# 3. Install gh CLI (no brew required)
ssh user@host 'cd /tmp && curl -sLO https://github.com/cli/cli/releases/download/v2.63.2/gh_2.63.2_macOS_arm64.zip && unzip -o gh_2.63.2_macOS_arm64.zip && cp gh_2.63.2_macOS_arm64/bin/gh ~/.local/bin/ && chmod +x ~/.local/bin/gh'
# For Intel Mac use: gh_2.63.2_macOS_amd64.zip

# 4. Update shell
ssh user@host 'cd ~/projects/a && python3 a.py update shell'

# 5. Apply gh token and setup git credential helper
ssh user@host 'export PATH="$HOME/.local/bin:$PATH" && echo "TOKEN_HERE" | gh auth login --with-token && git config --global credential.helper "$(which gh) auth git-credential"'

# 6. Sync
ssh user@host 'export PATH="$HOME/.local/bin:$PATH" && cd ~/projects/a && python3 a.py sync'

# 7. Register to SSH hosts
ssh user@host 'export PATH="$HOME/.local/bin:$PATH" && cd ~/projects/a && python3 a.py ssh self DEVICENAME PASSWORD'
```

## Termux (Android) via ADB

For Android devices connected via USB with Termux installed and sshd running.

```bash
# 1. Forward Termux SSH port
adb forward tcp:8022 tcp:8022

# 2. Clear old host key (if needed)
ssh-keygen -R '[localhost]:8022'

# 3. If repo exists but outdated, update it
sshpass -p 'PASSWORD' ssh -o StrictHostKeyChecking=no -p 8022 localhost \
  'cd ~/projects/aio && git fetch origin && git reset --hard origin/main'

# Or clone fresh:
sshpass -p 'PASSWORD' ssh -o StrictHostKeyChecking=no -p 8022 localhost \
  'git clone https://github.com/seanpattencode/aio.git ~/projects/a'

# 4. Create symlinks (aio -> a for compatibility)
sshpass -p 'PASSWORD' ssh -p 8022 localhost \
  'ln -sf ~/projects/aio ~/projects/a; rm -f ~/.local/bin/aio*; mkdir -p ~/.local/bin && ln -sf ~/projects/a/a.py ~/.local/bin/a'

# 5. Install gh and update shell
sshpass -p 'PASSWORD' ssh -p 8022 localhost 'pkg install -y gh'
sshpass -p 'PASSWORD' ssh -p 8022 localhost 'cd ~/projects/a && python a.py update shell'

# 6. Apply gh token and credential helper
sshpass -p 'PASSWORD' ssh -p 8022 localhost \
  'echo "TOKEN_HERE" | gh auth login --with-token && git config --global credential.helper "$(command -v gh) auth git-credential"'

# 7. Sync
sshpass -p 'PASSWORD' ssh -p 8022 localhost 'cd ~/projects/a && python a.py sync'

# 8. Register to SSH hosts (auto-detects IP)
sshpass -p 'PASSWORD' ssh -p 8022 localhost 'cd ~/projects/a && python a.py ssh self DEVICENAME PASSWORD'
```

**Start sshd via ADB input (if not running):**
```bash
adb shell "am start -n com.termux/.app.TermuxActivity"
sleep 2 && adb shell "input text 'sshd'" && adb shell "input keyevent 66"
```

**Set Termux password via ADB (if needed):**
```bash
adb shell "input text 'passwd'" && adb shell "input keyevent 66"
sleep 1 && adb shell "input text 'YOUR_PASSWORD'" && adb shell "input keyevent 66"
sleep 1 && adb shell "input text 'YOUR_PASSWORD'" && adb shell "input keyevent 66"
```

## Termux Offline (no network on device)

For devices without wifi, push repos via ADB.

```bash
# 1. Forward port and start sshd (see above)
adb forward tcp:8022 tcp:8022

# 2. Create tarballs on host machine
cd ~/projects
tar czf /tmp/a-repo.tar.gz a
tar czf /tmp/a-sync.tar.gz a-sync

# 3. Push to device (uses /data/local/tmp which is accessible)
adb push /tmp/a-repo.tar.gz /data/local/tmp/
adb push /tmp/a-sync.tar.gz /data/local/tmp/

# 4. Extract in Termux
sshpass -p 'PASSWORD' ssh -p 8022 localhost \
  'mkdir -p ~/projects && cd ~/projects && tar xzf /data/local/tmp/a-repo.tar.gz && tar xzf /data/local/tmp/a-sync.tar.gz'

# 5. Create symlink and update shell
sshpass -p 'PASSWORD' ssh -p 8022 localhost \
  'mkdir -p ~/.local/bin && ln -sf ~/projects/a/a.py ~/.local/bin/a && cd ~/projects/a && python a.py update shell'

# 6. Apply gh token (gh must be pre-installed: pkg install gh)
sshpass -p 'PASSWORD' ssh -p 8022 localhost \
  'echo "TOKEN_HERE" | gh auth login --with-token && git config --global credential.helper "$(command -v gh) auth git-credential"'

# 7. Clean up temp files
adb shell "rm /data/local/tmp/a-repo.tar.gz /data/local/tmp/a-sync.tar.gz"

# 8. Once wifi connected, register to SSH hosts
sshpass -p 'PASSWORD' ssh -p 8022 localhost 'cd ~/projects/a && python a.py ssh self DEVICENAME PASSWORD'
```

Note: Sync and SSH registration require network. Pushed repos include full git history.

## Via `a ssh` (after host is added)

```bash
a ssh <#> 'git clone https://github.com/seanpattencode/aio.git ~/projects/a'
a ssh <#> 'mkdir -p ~/.local/bin && ln -sf ~/projects/a/a.py ~/.local/bin/a'
a ssh <#> 'cd ~/projects/a && python3 a.py update shell'
a ssh <#> 'cd ~/projects/a && python3 a.py sync'
```

## Troubleshooting

**Shell has `local` errors**: Old corrupted rc file. Fix with:
```bash
ssh user@host 'cat > ~/.bashrc << EOF
export PATH="\$HOME/.local/bin:\$PATH"
a() { command python3 ~/.local/bin/a "\$@"; }
EOF'
```

**git can't authenticate**: Setup credential helper:
```bash
ssh user@host 'git config --global credential.helper "$(which gh) auth git-credential"'
```

**gh not in PATH for subprocess**: Prefix commands with:
```bash
export PATH="$HOME/.local/bin:$PATH" && ...
```

**Sync fails on fresh install**: Need to manually apply gh token first (step 4 above). The sync repos require gh auth, but the token is stored in sync repos.

## After Setup

**Register device to SSH hosts (run on the new device):**
```bash
a ssh self <name> <password>
# Example: a ssh self tablet Focus999.
# Auto-detects IP, adds to synced SSH hosts
```

Then from any other device:
- `a ssh <#>` or `a ssh <name>` to connect
- `a ssh` to list all hosts

Other commands:
- `a <#>` auto-clones missing projects using synced gh token
- `a sync` pulls/pushes all sync repos
- `a login` shows gh token status across devices

# ADB Termux Control Methods

## Prerequisites
- USB debugging enabled on Android
- `adb` installed on host
- Termux installed on Android

## Basic ADB Commands

```bash
# Check device connected
adb devices

# Open Termux
adb shell "am start -n com.termux/.app.TermuxActivity"

# Take screenshot
adb shell "screencap -p /sdcard/screen.png"
adb pull /sdcard/screen.png /tmp/screen.png

# Send text input (escape spaces with \)
adb shell "input text 'hello'"
adb shell "input text 'hello\ world'"  # with space

# Press Enter
adb shell "input keyevent 66"

# Swipe/scroll (x1 y1 x2 y2)
adb shell "input swipe 360 1000 360 400"  # scroll down

# Tap at coordinates
adb shell "input tap 360 500"
```

## Running Termux Commands via ADB

### Method 1: Push script then run
```bash
# Create script locally
echo 'your commands here' > /tmp/script.sh

# Push to shared storage
adb push /tmp/script.sh /sdcard/script.sh

# Grant Termux storage access first (run once)
adb shell "input text 'termux-setup-storage'" && adb shell "input keyevent 66"
# Then allow in Android settings

# Run script from Termux
adb shell "input text 'bash\ ~/storage/shared/script.sh'" && adb shell "input keyevent 66"
```

### Method 2: Direct input (simple commands)
```bash
# Open Termux
adb shell "am start -n com.termux/.app.TermuxActivity"
sleep 1

# Type command (escape spaces)
adb shell "input text 'ls\ -la'"
adb shell "input keyevent 66"  # Enter

# Wait and screenshot
sleep 2
adb shell "screencap -p /sdcard/screen.png"
adb pull /sdcard/screen.png /tmp/screen.png
```

### Method 3: Output to pullable file
```bash
# Run command, save output to /sdcard/
adb shell "input text 'ls\ -la\ >\ ~/storage/shared/output.txt'"
adb shell "input keyevent 66"
sleep 2

# Pull output
adb pull /sdcard/output.txt /tmp/output.txt
cat /tmp/output.txt
```

## Termux Storage Paths
- `/sdcard/` = `~/storage/shared/` (after termux-setup-storage)
- Termux home: `/data/data/com.termux/files/home/`
- Termux bin: `/data/data/com.termux/files/usr/bin/`

## Common Issues

### Escaping
- Spaces: use `\ ` or `%s`
- Special chars: `\&\&` for `&&`, `\|` for `|`

### Permissions
- Termux can't access /sdcard by default
- Run `termux-setup-storage` and allow in Android settings
- Then use `~/storage/shared/` path

### Command not found
- Shell functions not loaded in non-interactive adb input
- Use full paths: `python3 ~/.local/bin/aio` instead of `aio`
- Or source bashrc first: `source\ ~/.bashrc\ &&\ aio`
