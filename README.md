# C++ Message Broker Tool

A lightweight local message queue broker written in C++ for Windows.

The tool provides a simple TCP-based interface for sending, receiving, and monitoring messages through a background server process. The server runs hidden in the background and communicates with client commands through localhost.

## Features

- ✅ Background broker server (hidden window)
- ✅ Thread-safe FIFO message queue
- ✅ Multiple simultaneous client connections
- ✅ Send and retrieve messages
- ✅ Queue statistics monitoring
- ✅ Graceful server shutdown
- ✅ No external dependencies besides Windows Winsock
- ✅ Script-friendly output

---

# Compile

``` cmd
g++ -std=c++17 broker.cpp -o broker.exe -lws2_32
```


# Usage

The application has two modes:

1. Client mode
2. Internal server mode

The internal server mode is automatically started in the background and should not be called manually.

---

# Starting the Server

Initialize the background broker:

```cmd
MessageBroker.exe init
```

The server now runs hidden and listens on:

```
127.0.0.1:52345
```

---

# Sending Messages

Send a message into the queue:

```cmd
MessageBroker.exe send "Hello World"
```

Example:

```cmd
MessageBroker.exe send "Temperature sensor value: 24C"
```

The message is stored in the FIFO queue.

---

# Receiving Messages

Retrieve the oldest queued message:

```cmd
MessageBroker.exe get
```

Example output:

```
Hello World
```

The output intentionally contains no newline character, making it suitable for scripting.

If the queue is empty:

```
Info: No messages available in queue.
```

Exit code:

```
2
```

---

# Queue Statistics

Display queue information:

```cmd
MessageBroker.exe stats
```

Example output:

```
Current queued messages:  3
Peak queued (Max Load):   12
Total received messages:  50
Total processed (get):    47
Failed 'get' attempts:    5
```

Statistics:

| Value | Description |
|---|---|
| Current queued messages | Messages currently waiting |
| Peak queued | Maximum queue size reached |
| Total received messages | Total messages added |
| Total processed | Messages removed using `get` |
| Failed get attempts | Attempts to read from an empty queue |

---

# Stopping the Server

Shutdown the background broker:

```cmd
MessageBroker.exe exit
```


# Security Notes

This tool is designed for **local machine communication only**.

The server:

- listens only on `127.0.0.1`
- has no authentication
- has no encryption
- should not be exposed to external networks

Do not bind it to public interfaces without adding authentication and encryption.

---
