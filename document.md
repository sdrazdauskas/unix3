# Unix3 IRC Chatbot Communication Protocol & Architecture

## 1. Architecture
- **Main Process:** Reads configuration, loads narratives, sets up shared memory, forks child processes.
- **Child Processes:** Each handles a channel or user session, connects to IRC, processes messages, interacts with shared memory.
- **Shared Memory:** Stores narrative catalogue, logs, and cross-channel alerts. Protected by semaphores.
- **Pipes/Signals:** Used for inter-process communication (e.g., cross-channel alerts, admin commands).

## 2. Communication Protocol
### a. IRC Protocol (RFC 1459)
- Connect to server: `NICK`, `USER`, `JOIN`, `PRIVMSG`, `PING/PONG`, etc.
- Messages parsed and routed to appropriate process/channel.

### b. Internal Protocol
- **Shared Memory Structure:**
  - Narratives: JSON or struct array
  - Logs: Circular buffer or append-only
  - Alerts: Struct with channel/user, message, timestamp
- **Semaphores:**
  - One for narratives, one for logs, one for alerts
- **Pipes:**
  - For admin commands and cross-channel notifications: The main process creates a pipe for each child. The parent writes admin/cross-channel commands to the pipe, and the child listens for commands while handling IRC.
- **Signals:**
  - For process control (e.g., reload config, shutdown)

### c. Admin Commands (via #admin channel)
- `!stop <channel>`: Stop bot in channel
- `!ignore <user>`: Ignore user
- `!topic <channel> <topic>`: Change topic
- `!reload`: Reload config/narratives
- Only allowed from admin users with password

### d. Bot Loop Prevention
- Ignore messages from nicks matching `b[A-Z0-9]{8}`

## 3. Example Message Flow
1. User mentions another channel/user: Child process writes alert to shared memory, signals relevant process. Main process can write a notification to the relevant child's pipe.
2. Admin issues command: Main process writes command to the relevant child's pipe, which parses and acts on it.

## 4. Extensibility
- Add new narratives to `catalogue/narratives.json`
- Add new admin commands in `admin.c`

---
See `README.md` for build/run instructions.
