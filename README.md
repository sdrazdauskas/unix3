# Unix3 IRC Chatbot Project

## Overview
This project implements an IRC chatbot in C, supporting multiple channels, cross-channel interaction, admin commands, and a narrative catalogue. It uses a multi-process architecture with shared memory, semaphores, pipes, and signals for inter-process communication.

## Features
- Connects to IRC server (RFC 1459 compliant)
- Joins multiple channels from configuration
- Handles multiple conversations in parallel
- Centralized narrative and log catalogue (shared memory)
- Admin commands via #admin channel
- Prevents bot loops (ignores bAAAA9999 nicks)
- Cross-channel/user alerts
- Forks processes for channel/user sessions
- **!topic command available in all channels**

## File Structure
- `src/` - Source code
- `catalogue/` - Narrative catalogue (plain text)
- `config/` - Bot configuration
- `document.md` - Protocol and architecture description
- `Makefile` - Build instructions

## Build & Run
1. Edit `config/bot.conf` and `catalogue/narratives.txt` as needed.
2. Build: `make`
3. Run: `./irc_bot`

## Dependencies
- POSIX C libraries (for fork, shm, sem, etc.)
- Sockets

## Communication Protocol
See `document.md` for details.
