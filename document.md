# IRC Chatbot

## Overview
This project implements an IRC chatbot in C, supporting multiple channels, cross-channel interaction, admin commands, and a narrative catalogue. It uses a multi-process architecture with shared memory, semaphores, pipes, and signals for inter-process communication.

## Features
- Connects to IRC server (RFC 1459 compliant)
- Joins multiple channels from configuration
- Handles multiple conversations in parallel
- Centralized narrative and log catalogue
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

---

## 1. Architecture
- **Main Process:**  
  - Loads configuration from `config/bot.conf` using [`load_config`](src/config.c).
  - Loads narratives from a plain text file (`catalogue/narratives.txt`) using [`load_narratives`](src/narrative.c).
  - Initializes shared memory and semaphores via [`init_shared_resources`](src/shared_mem.c).
  - Forks a child process for each channel in the config.
  - Handles IRC server connection and dispatches messages to children via pipes.

- **Child Processes:**  
  - Each child handles one IRC channel.
  - Joins its assigned channel and processes messages received from the main process.
  - Handles narrative responses, admin commands, user/channel mentions, and topic queries.
  - Uses shared memory for admin state and ignore lists.

- **Shared Memory:**  
  - Stores admin authentication state, ignore list, and current topic in a [`SharedData`](src/shared_mem.h) struct.
  - Protected by a semaphore for safe concurrent access.

- **Pipes/Signals:**  
  - Main process forwards IRC messages to children via pipes.
  - Signals (e.g., SIGINT, SIGTERM) are used for graceful shutdown.

## 2. Communication Protocol

### a. IRC Protocol (RFC 1459)
- Connects using `NICK`, `USER`, `JOIN`, `PRIVMSG`, `PING/PONG`, etc.
- Main process receives all IRC messages and routes them to the correct child process.

### b. Internal Protocol
- **Shared Memory Structure:**  
  - See [`SharedData`](src/shared_mem.h) for fields: admin state, ignore list, topic.
- **Semaphores:**  
  - Used to protect shared memory access (see [`sem_lock`](src/shared_mem.c), [`sem_unlock`](src/shared_mem.c)).
- **Pipes:**  
  - Forwards IRC messages from main to children.
- **Signals:**  
  - Used for process control and shutdown.

### c. Admin Commands (via #admin channel or private message)
- `!auth <password>`: Authenticate as admin (private message to bot).
- `!stop <channel>`: Stop bot responses in a channel.
- `!start <channel>`: Resume bot responses in a channel. Child **process must already exist** in that channel.
- `!ignore <user>`: Ignore a user.
- `!removeignore <user>`: Remove a user from ignore list.
- `!clearignore`: Clear all ignored users.
- `!settopic <topic>`: Set the current topic (shared across channels).
- Only allowed from authenticated admin users (see [`handle_admin_command`](src/admin.c)).

### d. Bot Loop Prevention
- Ignores messages from nicks matching `b[A-Za-z0-9]{8}` (see main process logic). Additionally check if last input message keeps repeating too fast and stops responding to it.

### e. Narrative Catalogue
- Narratives are loaded from a plain text file (`catalogue/narratives.txt`) in the format:  
  `channel|trigger|response`
- Wildcard triggers (`*`) are supported for default responses.

### f. Mentions & Alerts
- If a message mentions another channel, an alert is sent to that channel.
- If a message mentions a user (format: 4 letters + 4 digits), the bot checks if the user is present in current channel and sends an alert if not.

## 3. Example Message Flow
1. User sends a message in a channel.
2. Main process receives the IRC message and forwards it to the appropriate child process.
3. Child process:
   - Checks for admin commands, narrative triggers, channel/user mentions, and topic queries.
   - Responds as appropriate using IRC protocol.

## 4. Extensibility
- Add new narratives to [`catalogue/narratives.txt`](catalogue/narratives.txt).
- Add new admin commands in [`src/admin.c`](src/admin.c).
- Modify shared memory structure in [`src/shared_mem.h`](src/shared_mem.h).