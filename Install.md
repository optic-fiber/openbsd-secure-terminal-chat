# INSTALL.md — Complete Installation Guide

## A self-built, multi-user terminal chat for OpenBSD

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Server Preparation](#server-preparation)
3. [Tor Hidden Service](#tor-hidden-service)
4. [SSH Hardening](#ssh-hardening)
5. [Firewall Configuration (pf)](#firewall-configuration-pf)
6. [Chat Client Installation](#chat-client-installation)
7. [Message Dispatcher](#message-dispatcher)
8. [Creating the Admin User](#creating-the-admin-user)
9. [Adding Chat Users](#adding-chat-users)
10. [Intrusion Detection (Optional)](#intrusion-detection-optional)
11. [Client Setup (Linux)](#client-setup-linux)
12. [Client Setup (Windows)](#client-setup-windows)
13. [Client Setup (macOS)](#client-setup-macos)
14. [Testing the Chat](#testing-the-chat)
15. [Troubleshooting](#troubleshooting)
16. [Maintenance](#maintenance)
17. [Security Checklist](#security-checklist)

---

## Prerequisites

### Server
- OpenBSD 7.x installed
- Root access (or `doas` configured)
- Internet connection for package installation

### Client (each user)
- Linux, macOS, or Windows with SSH client
- Tor and socat installed
- Basic terminal knowledge

---

## Server Preparation

### Step 1: Update the system

```bash
doas syspatch
doas pkg_add -u
```

### Step 2: Install required packages

```bash
doas pkg_add tor
```

That's it — only `tor` is needed. Everything else is in OpenBSD's base system.

---

## Tor Hidden Service

### Step 1: Configure Tor

Edit the Tor configuration file:

```bash
doas vi /etc/tor/torrc
```

Add these lines at the **end** of the file:

```
HiddenServiceDir /var/tor/ssh
HiddenServicePort 8420 127.0.0.1:8420
```

> **What this does:** Tor creates a hidden service directory and forwards all traffic from the `.onion` address to your local SSH port 8420.

### Step 2: Enable and start Tor

```bash
doas rcctl enable tor
doas rcctl start tor
```

### Step 3: Get your .onion address

```bash
doas cat /var/tor/ssh/hostname
```

Output looks like:
```
abc123def4567890xyz.onion
```

> ⚠️ **CRITICAL:** This `.onion` address is your server's only public access point. Guard it like a password. Only share it with trusted users through secure channels.

---

## SSH Hardening

### Step 1: Edit SSH configuration

```bash
doas vi /etc/ssh/sshd_config
```

Find and modify these lines (uncomment if they have a `#` in front):

```
Port 8420
ListenAddress 127.0.0.1:8420
PermitRootLogin no
PasswordAuthentication no
ChallengeResponseAuthentication no
PubkeyAuthentication yes
```

> **What each setting does:**
> - `Port 8420` — Changes SSH from default port 22 (obscurity)
> - `ListenAddress 127.0.0.1:8420` — SSH only listens on localhost, not the network (only Tor can reach it)
> - `PermitRootLogin no` — Root cannot login via SSH
> - `PasswordAuthentication no` — Passwords are rejected (keys only)
> - `ChallengeResponseAuthentication no` — No keyboard-interactive auth
> - `PubkeyAuthentication yes` — SSH keys are accepted

> ⚠️ **DANGER:** After setting `ListenAddress 127.0.0.1:8420`, you CANNOT connect to SSH directly from your local network anymore. Only Tor connections work. Keep your current SSH session open until you've fully tested the new setup!

### Step 2: Add admin user exception

At the **end** of `/etc/ssh/sshd_config`, add:

```
Match User YOUR-ADMIN-USERNAME
    # Admin has full shell access
```

Replace `YOUR-ADMIN-USERNAME` with your actual admin username (e.g., `optic`).

### Step 3: Reload SSH

```bash
doas rcctl reload sshd
```

### Step 4: Test in a second terminal

**Before closing your current session**, open a second terminal and test:

```bash
ssh -p 8420 YOUR-USERNAME@127.0.0.1
```

If this works, your SSH configuration is correct.

---

## Firewall Configuration (pf)

### Step 1: Edit pf configuration

```bash
doas vi /etc/pf.conf
```

Replace the content with:

```
set skip on lo

block return
pass

block return in on ! lo0 proto tcp to port 6000:6010
block return out log proto {tcp udp} user _pbuild

# Allow Tor to forward to SSH
pass in on lo0 proto tcp from 127.0.0.1 to 127.0.0.1 port 8420

# Allow outgoing connections (required for Tor to work)
pass out all
```

> **What this does:**
> - `set skip on lo` — Skip filtering on localhost (performance)
> - `block return` — Default: block everything
> - `pass in on lo0 ... port 8420` — Only allow SSH from localhost (where Tor forwards)
> - `pass out all` — Allow outgoing (Tor needs this to connect to the network)

### Step 2: Enable and start pf

```bash
doas pfctl -f /etc/pf.conf
doas rcctl enable pf
doas rcctl start pf
```

### Step 3: Verify firewall rules

```bash
doas pfctl -sr
```

You should see your rules listed.

---

## Chat Client Installation

### Step 1: Create source directory

```bash
mkdir -p ~/chat
cd ~/chat
```

### Step 2: Create the C source file

Copy the file `src/chat.c` from this repository, or create it:

```bash
vi chat.c
```

(Paste the complete source code from `src/chat.c`)

### Step 3: Compile

```bash
cc -o chat chat.c
```

> **Note:** The linker may show a warning about `strcpy()`. This is a false positive — the code uses `strlcpy()` which is the safe OpenBSD variant.

### Step 4: Install

```bash
doas cp chat /usr/local/bin/chat
doas chmod 755 /usr/local/bin/chat
```

### Step 5: Verify installation

```bash
ls -la /usr/local/bin/chat
/usr/local/bin/chat
```

You should see the chat UI with the "Chat" ASCII header. Press `Ctrl+C` to exit.

---

## Message Dispatcher

The dispatcher is a shell script that reads messages from a named pipe and distributes them to all connected users.

### Step 1: Create system user and group

```bash
doas groupadd chatusers
doas useradd -s /sbin/nologin -d /nonexistent _chatd
doas usermod -G chatusers _chatd
```

> **Why a separate user?** The dispatcher runs as `_chatd`, an unprivileged user with no shell. If the dispatcher has a security bug, an attacker only gains `_chatd` privileges — not root.

### Step 2: Install the dispatcher

Copy the file `src/dispatcher.sh` from this repository, or create it:

```bash
doas vi /usr/local/bin/chat-dispatcher
```

(Paste the complete source code from `src/dispatcher.sh`)

```bash
doas chmod +x /usr/local/bin/chat-dispatcher
```

### Step 3: Start the dispatcher

```bash
doas -u _chatd /usr/local/bin/chat-dispatcher &
```

You should see: `Chat-Dispatcher started`

### Step 4: Verify it's running

```bash
ps aux | grep chat-dispatcher
```

Look for a process running as `_chatd`.

### Step 5: Auto-start on boot

```bash
doas tee -a /etc/rc.local << 'EOF'
/usr/local/bin/chat-dispatcher &
EOF
```

> **Note:** `/etc/rc.local` must exist and be executable. If it doesn't exist:
> ```bash
> doas touch /etc/rc.local
> doas chmod +x /etc/rc.local
> ```

---

## Creating the Admin User

The admin user has normal shell access and can manage the server.

### Step 1: Create the user

```bash
doas useradd -m -s /bin/ksh YOUR-ADMIN-USERNAME
doas usermod -G wheel,chatusers YOUR-ADMIN-USERNAME
```

> `wheel` group allows `doas` access. `chatusers` group allows writing to the chat pipes.

### Step 2: Create SSH key for admin

On your **local machine** (not the server):

```bash
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_chat_admin -C "admin@chat"
```

**Set a passphrase!** This protects the key if your laptop is stolen.

### Step 3: Copy the public key to the server

Display the public key:

```bash
cat ~/.ssh/id_ed25519_chat_admin.pub
```

Copy the output. On the **server**:

```bash
doas mkdir -p /home/YOUR-ADMIN-USERNAME/.ssh
doas tee /home/YOUR-ADMIN-USERNAME/.ssh/authorized_keys << 'EOF'
ssh-ed25519 PASTE-THE-KEY-HERE admin@chat
EOF
doas chown -R YOUR-ADMIN-USERNAME:YOUR-ADMIN-USERNAME /home/YOUR-ADMIN-USERNAME/.ssh
doas chmod 700 /home/YOUR-ADMIN-USERNAME/.ssh
doas chmod 600 /home/YOUR-ADMIN-USERNAME/.ssh/authorized_keys
```

### Step 4: Add to SSH config

In `/etc/ssh/sshd_config`, ensure the admin has NO `ForceCommand`:

```
Match User YOUR-ADMIN-USERNAME
    # Admin has full shell access
```

Reload SSH:

```bash
doas rcctl reload sshd
```

### Step 5: Test admin connection

On your local machine:

```bash
ssh -i ~/.ssh/id_ed25519_chat_admin \
    -o ProxyCommand='socat STDIO SOCKS4A:127.0.0.1:%h:%p,socksport=9050' \
    -p 8420 \
    YOUR-ADMIN-USERNAME@YOUR-ONION.onion
```

You should get a normal shell prompt.

---

## Adding Chat Users

Chat users can ONLY chat. They have no shell access.

### Step 1: User sends you their SSH public key

They generate a key on their machine:

```bash
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_chat -C "theirname@chat"
```

> **Recommendation:** They should set a passphrase on the key. This prevents access if the key file is stolen.

They display the public key:

```bash
cat ~/.ssh/id_ed25519_chat.pub
```

They send you this output through a **secure channel** (Signal, Matrix, or directly in the chat once they have temporary access).

### Step 2: Create the user on the server

```bash
doas useradd -m -s /usr/local/bin/chat USERNAME
doas usermod -G chatusers USERNAME
```

> **Important:** The shell is set to `/usr/local/bin/chat`. This means they land directly in the chat when connecting. Combined with `ForceCommand` (next step), they can NEVER get a shell.

### Step 3: Install their SSH key

```bash
doas mkdir -p /home/USERNAME/.ssh
doas tee /home/USERNAME/.ssh/authorized_keys << 'EOF'
ssh-ed25519 PASTE-THEIR-KEY-HERE theirname@chat
EOF
doas chown -R USERNAME:USERNAME /home/USERNAME/.ssh
doas chmod 700 /home/USERNAME/.ssh
doas chmod 600 /home/USERNAME/.ssh/authorized_keys
```

### Step 4: Restrict user to chat only

Edit `/etc/ssh/sshd_config`:

```bash
doas vi /etc/ssh/sshd_config
```

Add at the end:

```
Match User USERNAME
    ForceCommand /usr/local/bin/chat
    AllowTcpForwarding no
    X11Forwarding no
```

> **What this does:**
> - `ForceCommand /usr/local/bin/chat` — No matter what command the user tries, only `chat` runs
> - `AllowTcpForwarding no` — No port forwarding
> - `X11Forwarding no` — No graphical forwarding
>
> **The user CANNOT escape to a shell**, even if `chat` crashes.

Reload SSH:

```bash
doas rcctl reload sshd
```

### Step 5: Verify user can connect

```bash
doas cat /home/USERNAME/.ssh/authorized_keys
doas grep -A3 "Match User USERNAME" /etc/ssh/sshd_config
```

Both should show the correct key and `ForceCommand /usr/local/bin/chat`.

---

## Intrusion Detection (Optional)

### File Integrity with mtree

`mtree` creates a cryptographic baseline of system files and alerts if anything changes.

#### Create the baseline

```bash
doas sh -c 'mtree -c -K sha256digest -p /etc > /var/db/mtree.etc'
doas sh -c 'mtree -c -K sha256digest -p /usr/local/bin > /var/db/mtree.bin'
doas sh -c 'mtree -c -K sha256digest -p /usr/local/sbin > /var/db/mtree.sbin'
doas chmod 600 /var/db/mtree.*
```

#### Create the check script

```bash
doas tee /usr/local/bin/check-integrity << 'EOF'
#!/bin/sh
echo "=== Integrity Check $(date) ==="
mtree -p /etc < /var/db/mtree.etc 2>/dev/null | grep -v "^$" | grep -v "^#"> /tmp/mtree.diff
mtree -p /usr/local/bin < /var/db/mtree.bin 2>/dev/null | grep -v "^$" | grep -v "^#"> /tmp/mtree.bin.diff
mtree -p /usr/local/sbin < /var/db/mtree.sbin 2>/dev/null | grep -v "^$" | grep -v "^#"> /tmp/mtree.sbin.diff
if [ -s /tmp/mtree.diff ] || [ -s /tmp/mtree.bin.diff ] || [ -s /tmp/mtree.sbin.diff ]; then
    echo "WARNING: Changes detected!"
    cat /tmp/mtree.diff /tmp/mtree.bin.diff /tmp/mtree.sbin.diff
else
    echo "All files unchanged."
fi
EOF
doas chmod +x /usr/local/bin/check-integrity
```

#### SSH Login Monitoring

```bash
doas tee /usr/local/bin/ssh-watch << 'EOF'
#!/bin/sh
LOG=/var/log/authlog
REPORT=/var/log/ssh-report.log
echo "=== SSH Monitor $(date) ===" >> "$REPORT"
FAILS=$(grep "Failed password\|Failed publickey\|Invalid user" "$LOG" 2>/dev/null | wc -l)
SUCCESS=$(grep "Accepted publickey" "$LOG" 2>/dev/null | wc -l)
echo "Failed: $FAILS | Successful: $SUCCESS" >> "$REPORT"
grep "Invalid user" "$LOG" 2>/dev/null | awk '{print $NF}' | sort | uniq -c | sort -rn | head -5 >> "$REPORT"
echo "---" >> "$REPORT"
EOF
doas chmod +x /usr/local/bin/ssh-watch
```

#### Schedule with cron

Append to `/etc/crontab`:

```bash
doas tee -a /etc/crontab << 'EOF'

# Chat system monitoring
0       3       *       *       *       root    /usr/local/bin/check-integrity >> /var/log/integrity.log 2>&1
@hourly                                 root    /usr/local/bin/ssh-watch
EOF
```

> **Note:** After any legitimate system changes, recreate the mtree baseline. Otherwise the check will report false positives.

---

## Client Setup (Linux)

### Step 1: Install packages

**Fedora:**
```bash
sudo dnf install tor socat
```

**Ubuntu/Debian:**
```bash
sudo apt install tor socat
```

**Arch:**
```bash
sudo pacman -S tor socat
```

### Step 2: Start Tor

```bash
sudo systemctl start tor
sudo systemctl enable tor
```

### Step 3: Create SSH key

```bash
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_chat -C "yourname@chat"
```

**Set a passphrase!**

### Step 4: Send public key to admin

```bash
cat ~/.ssh/id_ed25519_chat.pub
```

Send this output to the server admin through a secure channel.

### Step 5: Create SSH configuration

```bash
cat >> ~/.ssh/config << 'EOF'
Host chat
    Hostname YOUR-ONION-ADDRESS.onion
    Port 8420
    User YOUR-USERNAME
    IdentityFile ~/.ssh/id_ed25519_chat
    ProxyCommand socat STDIO SOCKS4A:127.0.0.1:%h:%p,socksport=9050
EOF
chmod 600 ~/.ssh/config
```

> Replace `YOUR-ONION-ADDRESS` with the actual `.onion` from the admin.
> Replace `YOUR-USERNAME` with your username on the server.

### Step 6: Connect

```bash
ssh chat
```

Enter your key passphrase if prompted. You land directly in the chat!

### Alternative: Full command (no SSH config)

```bash
ssh -i ~/.ssh/id_ed25519_chat \
    -o ProxyCommand='socat STDIO SOCKS4A:127.0.0.1:%h:%p,socksport=9050' \
    -p 8420 \
    YOUR-USERNAME@YOUR-ONION.onion
```

---

## Client Setup (Windows)

### Step 1: Install Tor

Download and install Tor Browser from: https://www.torproject.org/download/

### Step 2: Install socat

Download socat for Windows from: https://github.com/tech128/socat-windows/releases

Extract `socat.exe` to `C:\Windows\System32\` or a folder in your PATH.

### Step 3: Create SSH key

Open PowerShell:

```powershell
ssh-keygen -t ed25519 -f C:\Users\YOURNAME\.ssh\id_ed25519_chat -C "yourname@chat"
```

### Step 4: Send public key to admin

```powershell
type C:\Users\YOURNAME\.ssh\id_ed25519_chat.pub
```

Send this output to the server admin.

### Step 5: Connect

```powershell
ssh -i C:\Users\YOURNAME\.ssh\id_ed25519_chat -o "ProxyCommand=socat STDIO SOCKS4A:127.0.0.1:%h:%p,socksport=9050" -p 8420 YOUR-USERNAME@YOUR-ONION.onion
```

> **Note:** Tor Browser must be running in the background for the SOCKS proxy on port 9050 to work.

---

## Client Setup (macOS)

### Step 1: Install packages

```bash
brew install tor socat
```

### Step 2: Start Tor

```bash
brew services start tor
```

### Step 3-6: Same as Linux

Follow steps 3-6 from the Linux client setup.

---

## Testing the Chat

### Step 1: Admin connects

```bash
ssh chat-admin
```

Then start the chat:

```bash
chat
```

### Step 2: User connects

On their machine:

```bash
ssh chat
```

They land directly in the chat.

### Step 3: Send messages

Both users type messages and press Enter. Messages appear in real time with color-coded nicknames.

### Step 4: Exit

- `/quit` + Enter
- Or `Ctrl+C`

The terminal is properly restored.

---

## Troubleshooting

### User can't connect ("Permission denied (publickey)")

**Check the log on the server:**

```bash
doas tail -50 /var/log/authlog | grep "USERNAME\|Failed"
```

**Common causes:**
1. The public key on the server doesn't match the user's private key
2. File permissions are wrong:
   ```bash
   doas ls -la /home/USERNAME/.ssh/
   # Must show:
   # drwx------  .ssh
   # -rw-------  authorized_keys
   ```
3. `ForceCommand` points to a non-existent file:
   ```bash
   ls -la /usr/local/bin/chat
   ```

### User gets "shell does not exist" error

```bash
doas chsh -s /usr/local/bin/chat USERNAME
```

### Chat messages don't appear between users

**Check if dispatcher is running:**

```bash
ps aux | grep chat-dispatcher
```

If not running:

```bash
doas -u _chatd /usr/local/bin/chat-dispatcher &
```

**Check pipe permissions:**

```bash
ls -la /tmp/chat-pipe /tmp/chat-output
```

Must show `_chatd:chatusers` with `rw-rw----`.

### Terminal broken after chat crash

On the server, type blindly:

```bash
stty sane
```

Or just reconnect via SSH (closing the session resets the terminal).

### mtree shows false positives

After any legitimate system change, recreate the baseline:

```bash
doas sh -c 'mtree -c -K sha256digest -p /etc > /var/db/mtree.etc'
doas sh -c 'mtree -c -K sha256digest -p /usr/local/bin > /var/db/mtree.bin'
doas sh -c 'mtree -c -K sha256digest -p /usr/local/sbin > /var/db/mtree.sbin'
```

---

## Maintenance

### Restart the chat system

```bash
doas pkill -9 chat-dispatcher
sleep 1
doas rm -f /tmp/chat-pipe /tmp/chat-output /tmp/chat.log
doas -u _chatd /usr/local/bin/chat-dispatcher &
```

### Update the chat client

```bash
cd ~/chat
cc -o chat chat.c
doas cp chat /usr/local/bin/chat
```

### View monitoring logs

```bash
cat /var/log/integrity.log
cat /var/log/ssh-report.log
```

### Remove a user

```bash
doas userdel -r USERNAME
doas vi /etc/ssh/sshd_config  # Remove their Match block
doas rcctl reload sshd
```

---

## Security Checklist

After installation, verify each point:

| # | Check | Command |
|---|-------|---------|
| 1 | SSH only on localhost | `doas netstat -an -p tcp \| grep 8420` → shows `127.0.0.1` |
| 2 | Password auth disabled | `doas grep PasswordAuth /etc/ssh/sshd_config` → `no` |
| 3 | Root login disabled | `doas grep PermitRoot /etc/ssh/sshd_config` → `no` |
| 4 | Tor running | `doas rcctl check tor` → `tor(ok)` |
| 5 | pf active | `doas pfctl -si \| grep Status` → `Enabled` |
| 6 | Chat users have no shell | `doas grep USERNAME /etc/passwd` → shows `/usr/local/bin/chat` |
| 7 | ForceCommand set | `doas grep -A3 "Match User" /etc/ssh/sshd_config` |
| 8 | Dispatcher as _chatd | `ps aux \| grep chat-dispatcher` → shows `_chatd` |
| 9 | Pipes not world-readable | `ls -la /tmp/chat-pipe` → `rw-rw----` |
| 10 | mtree baseline exists | `ls -la /var/db/mtree.*` |

---

This completes the installation. Your secure terminal chat is now operational.
