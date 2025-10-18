# 💬 Advanced Chat System v2.0

A sophisticated deadlock-free chat system that uses shared memory and semaphores for inter-process communication, designed to work like a modern messaging app.

## 🌟 Features

- ✅ **Deadlock-Free**: Strict semaphore ordering prevents deadlocks
- ✅ **Race Condition Free**: Mutual exclusion via semaphores  
- ✅ **Flexible Startup**: Either user can start first
- ✅ **Colored Output**: Beautiful ANSI color-coded interface
- ✅ **Message History**: Automatic logging to `chat_history.log`
- ✅ **Multiple Exit Commands**: `exit`, `bye`, `quit`, `q`
- ✅ **Graceful Exit**: Proper cleanup of resources
- ✅ **Signal Handling**: Handles Ctrl+C gracefully
- ✅ **Interactive CLI**: Modern, user-friendly interface

## 👥 Users

- **Jaineel** (Cyan color) - First user
- **Gul** (Green color) - Second user

## 🏗️ Architecture

### Communication Flow
```
┌─────────────┐    Shared Memory    ┌─────────────┐
│   Jaineel   │◄──────────────────►│     Gul     │
│             │                    │             │
│  (Process)   │    Semaphores      │  (Process)  │
└─────────────┘                    └─────────────┘
```

### Components
- **Shared Memory**: Stores message buffer with multiple messages
- **Semaphores**: 3 semaphores for write, read, and system control
- **Message Buffer**: Circular buffer for up to 10 messages
- **Logging System**: Timestamped message history

### Deadlock Prevention
- **Strict Ordering**: Jaineel waits on write semaphore, signals read
- **No Circular Wait**: Gul waits on read semaphore, signals write  
- **One Release Per Acquire**: Each semaphore operation is paired
- **System Semaphore**: Controls initialization and cleanup

## 📁 Files

| File | Description |
|------|-------------|
| `chat_common.h` | Common definitions, colors, and helper functions |
| `jaineel.c` | Jaineel's chat client |
| `gul.c` | Gul's chat client |
| `Makefile` | Build system with helpful targets |
| `chat_history.log` | Message history log (auto-generated) |

## 🚀 Quick Start

### 1. Compile the System
```bash
make all
```

### 2. Start Chatting
**Option A: Jaineel starts first**
```bash
# Terminal 1
./jaineel

# Terminal 2 (after Jaineel is running)
./gul
```

**Option B: Gul starts first**
```bash
# Terminal 1
./gul

# Terminal 2 (after Gul is running)  
./jaineel
```

### 3. Start Messaging!
- Type your messages and press Enter
- Messages appear in real-time with timestamps
- Use `exit`, `bye`, `quit`, or `q` to leave

## 🎨 Interface

### Welcome Screen
```
╔══════════════════════════════════════════════════════════════╗
║                    CHAT SYSTEM v2.0                        ║
║                                                              ║
║  Welcome Jaineel!                                            ║
║                                                              ║
║  Commands: exit, bye, quit, q                              ║
║  Type your messages below...                               ║
╚══════════════════════════════════════════════════════════════╝
```

### Message Display
```
[14:30] Jaineel: Hello Gul!
[14:30] Gul: Hi Jaineel, how are you?
[14:31] Jaineel: I'm doing great, thanks!
```

## 🛠️ Build System

### Available Targets
```bash
make all              # Compile both programs
make jaineel          # Compile Jaineel only
make gul              # Compile Gul only
make clean            # Remove executables
make clean-resources  # Clean shared memory & semaphores
make distclean        # Clean everything
make run-jaineel      # Compile and run Jaineel
make run-gul          # Compile and run Gul
make help             # Show help
```

### Cleanup Commands
```bash
# Clean up system resources
make clean-resources

# Clean everything
make distclean
```

## 📝 Message History

All messages are automatically logged to `chat_history.log`:

```
[2024-01-15 14:30:15] Jaineel: Hello Gul!
[2024-01-15 14:30:20] Gul: Hi Jaineel, how are you?
[2024-01-15 14:30:25] SYSTEM: Jaineel process started
[2024-01-15 14:30:30] SYSTEM: Gul connected to chat system
```

## 🎯 Technical Details

### Shared Memory Structure
```c
struct chat_message {
    char content[200];
    char sender[20];
    int type;        // MSG_TYPE_NORMAL, MSG_TYPE_EXIT, MSG_TYPE_SYSTEM
    int message_id;
};

struct shmseg {
    struct chat_message messages[10];  // Message buffer
    int message_count;                 // Current message count
    int current_message;               // Current message index
    int last_message_id;               // Last message ID
    int system_ready;                  // System initialization flag
};
```

### Semaphore Usage
- **Semaphore 0**: Write control (Jaineel waits, Gul signals)
- **Semaphore 1**: Read control (Gul waits, Jaineel signals)  
- **Semaphore 2**: System control (initialization and cleanup)

### Color Scheme
- **Jaineel**: Cyan (`\033[36m`)
- **Gul**: Green (`\033[32m`)
- **System**: Yellow (`\033[33m`)
- **Errors**: Red (`\033[31m`)
- **Info**: Blue (`\033[34m`)
- **Success**: Green (`\033[32m`)

## 🔧 System Requirements

- **OS**: Linux/Unix with POSIX support
- **Compiler**: GCC or Clang
- **Libraries**: Standard C library
- **Permissions**: Shared memory and semaphore access

## 🐛 Troubleshooting

### Common Issues

**1. "shmget failed" error**
```bash
# Clean up existing resources
make clean-resources
# Try again
```

**2. "semget failed" error**
```bash
# Make sure the other user is running first
# Or clean up resources and restart
make clean-resources
```

**3. Colors not displaying**
- Ensure your terminal supports ANSI color codes
- Try running in a different terminal

**4. Permission denied**
```bash
# Check shared memory permissions
ipcs -m
ipcs -s
```

### Debug Commands
```bash
# Check shared memory
ipcs -m

# Check semaphores  
ipcs -s

# Remove specific resources
ipcrm -m <shmid>
ipcrm -s <semid>
```

## 🔒 Security Notes

- Shared memory is created with 0666 permissions
- No authentication between users
- Messages are stored in plain text
- Log files contain all conversation history

## 📊 Performance

- **Latency**: Near real-time message delivery
- **Memory**: ~2KB shared memory per session
- **CPU**: Minimal overhead with semaphore synchronization
- **Scalability**: Designed for 2 users (can be extended)

## 🚀 Future Enhancements

- [ ] File sharing capabilities
- [ ] Message encryption
- [ ] Multiple chat rooms
- [ ] User authentication
- [ ] Message persistence
- [ ] GUI interface
- [ ] Network support

## 📄 License

This project is for educational purposes. Feel free to modify and extend as needed.

---

**Happy Chatting! 💬**