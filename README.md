# openbsd-secure-terminal-chat

![License](https://img.shields.io/badge/license-MIT-blue)
![Platform](https://img.shields.io/badge/platform-OpenBSD-orange)
![Language](https://img.shields.io/badge/language-C%20%2B%20Shell-green)
![Lines](https://img.shields.io/badge/size-~180%20LOC-lightgrey)

A self-built, multi-user terminal chat for OpenBSD.  
Connections only via Tor onion service. SSH key authentication, no passwords, no shell for chat users.

---

## What is this?

This project provides a **private, self-contained group chat** that runs entirely on an OpenBSD server.  
Users connect through the Tor network and land directly in a full-screen terminal chat — no shell access, no cloud, no tracking.

**Why?** Because privacy doesn't need big tech. Less than 200 lines of C and shell give you a messenger under your full control.

---

## Features

- 🧅 **Tor Hidden Service** — Server IP never exposed
- 🔑 **SSH keys only** — No passwords, no brute-force
- 🔒 **No shell access** — Chat users can only chat
- 🎨 **Full-screen UI** — Colored nicknames, real-time messages
- 💾 **RAM-only logs** — Nothing persistent on disk
- 🛡️ **Hardened** — ForceCommand, unprivileged dispatcher, pf firewall

---

## Quick Start

See **[INSTALL.md](INSTALL.md)** for the complete step-by-step guide.

### Minimal setup (after OpenBSD + Tor):

```bash
cc -o chat src/chat.c
doas cp chat /usr/local/bin/chat
doas cp src/dispatcher.sh /usr/local/bin/chat-dispatcher
doas chmod +x /usr/local/bin/chat-dispatcher
doas -u _chatd /usr/local/bin/chat-dispatcher &
