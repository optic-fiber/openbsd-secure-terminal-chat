# Void Chat — Complete Setup Guide

A self‑hosted, encrypted, multi‑user terminal chat for OpenBSD.
Users connect via SSH over a Tor Hidden Service and land directly
in a full‑screen chat – no shell access. All messages are encrypted
end‑to‑end with a shared passphrase. The server only sees Base64
ciphertext. Message history survives reconnects.

---

## Table of Contents

1. [How It Works](#how-it-works)  
2. [Encryption Details](#encryption-details)  
3. [Prerequisites](#prerequisites)  
4. [Server Preparation](#server-preparation)  
5. [Tor Hidden Service](#tor-hidden-service)  
6. [SSH Hardening](#ssh-hardening)  
7. [Firewall (pf)](#firewall-pf)  
8. [Chat Client Installation](#chat-client-installation)  
9. [Message Dispatcher](#message-dispatcher)  
10. [Creating the Admin User](#creating-the-admin-user)  
11. [Adding Chat Users](#adding-chat-users)  
12. [Intrusion Detection (Optional)](#intrusion-detection-optional)  
13. [Client Setup (Linux)](#client-setup-linux)  
14. [Client Setup (Windows)](#client-setup-windows)  
15. [Client Setup (macOS)](#client-setup-macos)  
16. [Testing the Chat](#testing-the-chat)  
17. [Troubleshooting](#troubleshooting)  
18. [Maintenance](#maintenance)  
19. [Security Checklist](#security-checklist)  

---

## How It Works

The chat consists of two main components: a **chat client** (`/usr/local/bin/chat`)
and a **message dispatcher** (`/usr/local/bin/chat-dispatcher`).

```
User "alice" ──┐
               ├──▶ /tmp/chat-pipe (named pipe, 660, _chatd:chatusers)
User "bob"   ──┘              │
                               ▼
                        chat-dispatcher
                        (runs as _chatd)
                               │
                    ┌──────────┴──────────┐
                    ▼                     ▼
           /tmp/chat-output        /tmp/chat.log
           (live messages)         (persistent history)
                    │
                    ▼
              All connected clients read
              new messages from here
```

1. A client writes `username:encrypted_base64` to the named pipe `/tmp/chat-pipe`.
2. The dispatcher reads the pipe line by line, appends each line to
   `/tmp/chat-output` (live feed) and `/tmp/chat.log` (persistent log).
3. Every client polls `/tmp/chat-output` every 100 ms for new data,
   decrypts it with the shared room key, and displays the message.
4. When a client starts, it first loads all historic messages from
   `/tmp/chat.log`, then positions itself at the end of the live feed
   to receive only new messages.

Because the dispatcher runs as the unprivileged user `_chatd` and the
pipe + output files are group‑accessible to `chatusers`, no chat user
ever needs shell access.

---

## Encryption Details

- All participants agree on a **shared passphrase** (exchanged out‑of‑band).
- The passphrase is hashed with **SHA256** to produce a 256‑bit key.
- For every message a random 12‑byte **nonce** is generated.
- The plaintext is prefixed with a 2‑byte big‑endian length.
- A keystream is created by repeatedly hashing `key || nonce || counter`
  with SHA256, and this keystream is XORed with `length || plaintext`.
- The final wire format is: `nonce (12 bytes) || encrypted data`.
- This binary blob is then **Base64‑encoded** before being written to the pipe.
- The server never sees the key or the plaintext. An administrator
  only sees Base64 garbage.
- The key and all plaintext buffers are explicitly wiped from memory
  when the client exits.

No external libraries are needed beyond OpenBSD’s base `libcrypto`.

---

## Prerequisites

### Server

- OpenBSD 7.x installed
- Root access or `doas` configured
- Internet connection for package installation
- `tor` package (from OpenBSD ports/pkg)

### Client (each user)

- Linux, macOS, or Windows with an SSH client
- Tor and `socat` installed locally
- Basic terminal knowledge

---

## Server Preparation

Update the system and install the only needed package:

```sh
doas syspatch
doas pkg_add -u
doas pkg_add tor
```

Everything else (C compiler, `libcrypto`, firewall, SSH server) is already
part of the OpenBSD base system.

---

## Tor Hidden Service

The server is only reachable through Tor. This hides the real IP address
and provides anonymity.

1. Edit `/etc/tor/torrc` and add:

```
HiddenServiceDir /var/tor/chat_service
HiddenServicePort 8420 127.0.0.1:8420
```

   This tells Tor to create a hidden service directory and forward all
   traffic arriving on the onion address to `127.0.0.1:8420` (our SSH port).

2. Enable and start Tor:

```sh
doas rcctl enable tor
doas rcctl start tor
```

3. Obtain the onion address:

```sh
doas cat /var/tor/chat_service/hostname
```

   The output looks like `abc123def456.onion`. **Treat this address as
   a secret** – it is the only way to reach your server.

---

## SSH Hardening

SSH is restricted to key‑based authentication and listens only on
localhost, so it can only be reached via Tor.

Edit `/etc/ssh/sshd_config` (uncomment and set the following):

```
Port 8420
ListenAddress 127.0.0.1:8420
PermitRootLogin no
PasswordAuthentication no
ChallengeResponseAuthentication no
PubkeyAuthentication yes
```

Explanation:
- `Port 8420` – move away from default 22.
- `ListenAddress 127.0.0.1:8420` – bind only to localhost; no direct network access.
- `PermitRootLogin no` – root cannot login via SSH.
- `PasswordAuthentication no` – only SSH keys are allowed.
- `ChallengeResponseAuthentication no` – disable keyboard‑interactive.
- `PubkeyAuthentication yes` – allow public key authentication.

**Important:** After this change you cannot connect directly from your local
network. Keep an existing session open until you have verified the Tor
connection works.

Finally, reload SSH:

```sh
doas rcctl reload sshd
```

---

## Firewall (pf)

OpenBSD’s packet filter (`pf`) is used to further restrict network traffic.

Edit `/etc/pf.conf`:

```
set skip on lo

block return
pass

# Allow Tor to forward to SSH
pass in on lo0 proto tcp from 127.0.0.1 to 127.0.0.1 port 8420

# Allow outgoing traffic (Tor needs this)
pass out all
```

Explanation:
- `set skip on lo` – skip filtering on the loopback interface (performance).
- `block return` – default deny.
- `pass in on lo0 … port 8420` – only allow connections to SSH from localhost.
- `pass out all` – permit all outgoing connections (required for Tor).

Enable and load the rules:

```sh
doas pfctl -f /etc/pf.conf
doas rcctl enable pf
doas rcctl start pf
```

---

## Chat Client Installation

The chat client is a single C file. It will be compiled and placed in
`/usr/local/bin/chat`.

1. Create a working directory and copy the source code (from the `src/`
   folder of this repository) into `chat.c`.

2. Compile:

```sh
cc -O2 -o chat chat.c -lcrypto
```

3. Install and set permissions:

```sh
doas cp chat /usr/local/bin/chat
doas chmod 755 /usr/local/bin/chat
```

---

## Message Dispatcher

The dispatcher is a shell script that runs as an unprivileged user.

### 1. Create the system user and group

```sh
doas groupadd chatusers
doas useradd -s /sbin/nologin -d /nonexistent _chatd
doas usermod -G chatusers _chatd
```

Why a separate user? If the dispatcher is compromised, the attacker only
gains the privileges of `_chatd` – no shell, no root access.

### 2. Install the dispatcher

Copy `src/chat-dispatcher` to `/usr/local/bin/chat-dispatcher` and make it
executable:

```sh
doas cp chat-dispatcher /usr/local/bin/chat-dispatcher
doas chmod 755 /usr/local/bin/chat-dispatcher
```

### 3. Create the persistent log file

The log file must survive dispatcher restarts but not a reboot (it lives
in `/tmp`, which is a RAM disk on OpenBSD).

```sh
doas touch /tmp/chat.log
doas chown _chatd:chatusers /tmp/chat.log
doas chmod 660 /tmp/chat.log
```

### 4. Start the dispatcher

```sh
doas -u _chatd /usr/local/bin/chat-dispatcher &
```

Verify it is running with `ps aux | grep chat-dispatcher`.

### 5. Auto‑start on boot

Append to `/etc/rc.local` (create the file if it does not exist):

```sh
echo "/usr/local/bin/chat-dispatcher &" | doas tee -a /etc/rc.local
doas chmod +x /etc/rc.local
```

---

## Creating the Admin User

The admin user keeps normal shell access for server management.

1. Create the admin user:

```sh
doas useradd -m -s /bin/ksh YOUR-ADMIN-USERNAME
doas usermod -G wheel,chatusers YOUR-ADMIN-USERNAME
```

2. Generate an SSH key **on your local machine** (with a passphrase!):

```sh
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_chat_admin -C "admin@chat"
```

3. Copy the public key to the server:

```sh
cat ~/.ssh/id_ed25519_chat_admin.pub
# Then on the server:
doas mkdir -p /home/YOUR-ADMIN-USERNAME/.ssh
doas tee /home/YOUR-ADMIN-USERNAME/.ssh/authorized_keys << 'EOF'
ssh-ed25519 PASTE-THE-KEY-HERE admin@chat
EOF
doas chown -R YOUR-ADMIN-USERNAME:YOUR-ADMIN-USERNAME /home/YOUR-ADMIN-USERNAME/.ssh
doas chmod 700 /home/YOUR-ADMIN-USERNAME/.ssh
doas chmod 600 /home/YOUR-ADMIN-USERNAME/.ssh/authorized_keys
```

4. In `/etc/ssh/sshd_config`, ensure the admin is **not** restricted:

```
Match User YOUR-ADMIN-USERNAME
    # Admin has full shell access
```

Reload SSH:

```sh
doas rcctl reload sshd
```

5. Test the admin connection:

```sh
ssh -i ~/.ssh/id_ed25519_chat_admin \
    -o 'ProxyCommand=socat STDIO SOCKS4A:127.0.0.1:%h:%p,socksport=9050' \
    -p 8420 \
    YOUR-ADMIN-USERNAME@YOUR-ONION.onion
```

You should get a normal shell prompt.

---

## Adding Chat Users

Chat users are **forced into the chat** and have no shell access.

### 1. User sends you their SSH public key

They generate a key on their own machine and send you the public key
(via a secure channel).

### 2. Create the user on the server

```sh
doas useradd -m -s /usr/local/bin/chat USERNAME
doas usermod -G chatusers USERNAME
```

The shell is set to `/usr/local/bin/chat`, but we also enforce it with
`ForceCommand` (next step).

### 3. Install their key

```sh
doas mkdir -p /home/USERNAME/.ssh
doas tee /home/USERNAME/.ssh/authorized_keys << 'EOF'
ssh-ed25519 PASTE-THEIR-KEY-HERE user@chat
EOF
doas chown -R USERNAME:USERNAME /home/USERNAME/.ssh
doas chmod 700 /home/USERNAME/.ssh
doas chmod 600 /home/USERNAME/.ssh/authorized_keys
```

### 4. Restrict access with ForceCommand

Add to `/etc/ssh/sshd_config`:

```
Match User USERNAME
    ForceCommand /usr/local/bin/chat
    AllowTcpForwarding no
    X11Forwarding no
```

- `ForceCommand` – whatever the user requests, only `/usr/local/bin/chat`
  is executed.
- `AllowTcpForwarding no` – no port forwarding.
- `X11Forwarding no` – no graphical forwarding.

Reload SSH:

```sh
doas rcctl reload sshd
```

---

## Intrusion Detection (Optional)

### File integrity with `mtree`

Create a baseline of critical directories:

```sh
doas sh -c 'mtree -c -K sha256digest -p /etc > /var/db/mtree.etc'
doas sh -c 'mtree -c -K sha256digest -p /usr/local/bin > /var/db/mtree.bin'
doas sh -c 'mtree -c -K sha256digest -p /usr/local/sbin > /var/db/mtree.sbin'
doas chmod 600 /var/db/mtree.*
```

A check script:

```sh
#!/bin/sh
# /usr/local/bin/check-integrity
echo "=== Integrity Check $(date) ==="
mtree -p /etc < /var/db/mtree.etc 2>/dev/null | grep -v "^$" > /tmp/mtree.diff
mtree -p /usr/local/bin < /var/db/mtree.bin 2>/dev/null | grep -v "^$" > /tmp/mtree.bin.diff
mtree -p /usr/local/sbin < /var/db/mtree.sbin 2>/dev/null | grep -v "^$" > /tmp/mtree.sbin.diff
if [ -s /tmp/mtree.diff ] || [ -s /tmp/mtree.bin.diff ] || [ -s /tmp/mtree.sbin.diff ]; then
    echo "WARNING: Changes detected!"
    cat /tmp/mtree.diff /tmp/mtree.bin.diff /tmp/mtree.sbin.diff
else
    echo "All files unchanged."
fi
```

### SSH login monitoring

A script that counts failed and successful logins:

```sh
#!/bin/sh
# /usr/local/bin/ssh-watch
LOG=/var/log/authlog
REPORT=/var/log/ssh-report.log
echo "=== SSH Monitor $(date) ===" >> "$REPORT"
FAILS=$(grep "Failed password\|Failed publickey\|Invalid user" "$LOG" 2>/dev/null | wc -l)
SUCCESS=$(grep "Accepted publickey" "$LOG" 2>/dev/null | wc -l)
echo "Failed: $FAILS | Successful: $SUCCESS" >> "$REPORT"
grep "Invalid user" "$LOG" 2>/dev/null | awk '{print $NF}' | sort | uniq -c | sort -rn | head -5 >> "$REPORT"
echo "---" >> "$REPORT"
```

Make them executable:

```sh
doas chmod +x /usr/local/bin/check-integrity /usr/local/bin/ssh-watch
```

Schedule with cron by appending to `/etc/crontab`:

```
0       3       *       *       *       root    /usr/local/bin/check-integrity >> /var/log/integrity.log 2>&1
@hourly                                 root    /usr/local/bin/ssh-watch
```

---

## Client Setup (Linux)

1. Install Tor and socat:

   ```sh
   # Fedora
   sudo dnf install tor socat
   # Debian/Ubuntu
   sudo apt install tor socat
   # Arch
   sudo pacman -S tor socat
   ```

2. Start Tor:

   ```sh
   sudo systemctl start tor
   sudo systemctl enable tor
   ```

3. Generate an SSH key (with passphrase!):

   ```sh
   ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_chat -C "yourname@chat"
   ```

4. Send the public key to the admin:

   ```sh
   cat ~/.ssh/id_ed25519_chat.pub
   ```

5. Create an SSH config entry (optional but recommended):

   ```
   Host chat
       Hostname YOUR-ONION.onion
       Port 8420
       User YOUR-USERNAME
       IdentityFile ~/.ssh/id_ed25519_chat
       ProxyCommand socat STDIO SOCKS4A:127.0.0.1:%h:%p,socksport=9050
   ```

   Then connect with:

   ```sh
   ssh chat
   ```

   Alternatively, the full command:

   ```sh
   ssh -i ~/.ssh/id_ed25519_chat \
       -o 'ProxyCommand=socat STDIO SOCKS4A:127.0.0.1:%h:%p,socksport=9050' \
       -p 8420 \
       YOUR-USERNAME@YOUR-ONION.onion
   ```

You land directly in the chat.

---

## Client Setup (Windows)

1. Install Tor Browser from [torproject.org](https://www.torproject.org/download/).  
   Run it – it provides a SOCKS proxy on `127.0.0.1:9050`.

2. Download `socat.exe` from a trusted source (e.g., GitHub) and place it in
   a directory that is in your `PATH` (or `C:\Windows\System32`).

3. Open PowerShell and generate an SSH key:

   ```powershell
   ssh-keygen -t ed25519 -f C:\Users\YOURNAME\.ssh\id_ed25519_chat -C "yourname@chat"
   ```

4. Show the public key and send it to the admin:

   ```powershell
   type C:\Users\YOURNAME\.ssh\id_ed25519_chat.pub
   ```

5. Connect:

   ```powershell
   ssh -i C:\Users\YOURNAME\.ssh\id_ed25519_chat -o "ProxyCommand=socat STDIO SOCKS4A:127.0.0.1:%h:%p,socksport=9050" -p 8420 YOUR-USERNAME@YOUR-ONION.onion
   ```

---

## Client Setup (macOS)

1. Install Tor and socat:

   ```sh
   brew install tor socat
   brew services start tor
   ```

2. Follow steps 3–6 of the Linux client setup.

---

## Testing the Chat

1. Admin connects and starts the chat:

   ```sh
   ssh chat-admin
   /usr/local/bin/chat
   ```

2. User connects – they land directly in the chat (no command needed).

3. Both type messages; they appear in real time with coloured nicknames.

4. Exit with `/quit` or `Ctrl+C`. The terminal is restored.

---

## Troubleshooting

**“Permission denied (publickey)”**

- Verify the public key on the server matches the user’s private key.
- Check permissions:

  ```sh
  doas ls -la /home/USERNAME/.ssh/
  # Must show: drwx------ .ssh, -rw------- authorized_keys
  ```

- The user’s shell must be `/usr/local/bin/chat`.

**Messages don’t appear**

- All users must enter exactly the same room passphrase (case‑sensitive).
- Ensure the dispatcher is running:

  ```sh
  pgrep -f chat-dispatcher
  ```

- Check pipe permissions: `ls -la /tmp/chat-pipe` – must be `_chatd:chatusers 660`.

**“Text file busy” when re‑compiling**

- Kill any running chat instances first: `doas pkill -9 chat`

**Terminal broken after exit**

- Type `stty sane` blindly or reconnect via SSH.

---

## Maintenance

- **Restart the chat system:**

  ```sh
  doas pkill -f chat-dispatcher
  sleep 1
  doas -u _chatd /usr/local/bin/chat-dispatcher &
  ```

- **Clear the history (wipe all messages):**

  ```sh
  :> /tmp/chat-output
  :> /tmp/chat.log
  ```

- **Update the client:**

  Recompile and copy: `cc -O2 -o chat chat.c -lcrypto && doas cp chat /usr/local/bin/chat`

- **Remove a user:**

  ```sh
  doas userdel -r USERNAME
  # Remove their Match block from /etc/ssh/sshd_config
  doas rcctl reload sshd
  ```

---

## Security Checklist

After installation, verify every point:

| # | Check | Command |
|---|-------|---------|
| 1 | SSH only on localhost | `doas netstat -an -p tcp \| grep 8420` → shows `127.0.0.1` |
| 2 | Password auth disabled | `doas grep PasswordAuth /etc/ssh/sshd_config` → `no` |
| 3 | Root login disabled | `doas grep PermitRoot /etc/ssh/sshd_config` → `no` |
| 4 | Tor running | `doas rcctl check tor` → `tor(ok)` |
| 5 | Firewall active | `doas pfctl -si \| grep Status` → `Enabled` |
| 6 | Chat users have no shell | `doas grep USERNAME /etc/passwd` → shows `/usr/local/bin/chat` |
| 7 | ForceCommand set | `doas grep -A3 "Match User" /etc/ssh/sshd_config` |
| 8 | Dispatcher runs as `_chatd` | `ps aux \| grep chat-dispatcher` → shows `_chatd` |
| 9 | Pipes not world‑readable | `ls -la /tmp/chat-pipe` → `rw-rw----` |
|10 | mtree baseline exists | `ls -la /var/db/mtree.*` |

---

The complete source code (`chat.c`, `chat-dispatcher`, `chat-clean`) can be
found in the `src/` directory of this repository. Follow the build
instructions above to deploy on your own OpenBSD server.
