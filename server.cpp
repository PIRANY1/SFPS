#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdint>

// Automatic linker directive for MSVC
#pragma comment(lib, "Ws2_32.lib")

constexpr const char* DEFAULT_PORT = "52345";
constexpr const char* INTERNAL_SERVER_FLAG = "__hidden_server__";

// Thread-safe FIFO Queue tracking total statistics
class MessageQueue {
private:
    std::queue<std::string> q;
    std::mutex mtx;
    uint32_t total_received_count = 0;
    uint32_t total_processed_count = 0;
    uint32_t failed_get_count = 0;
    uint32_t peak_queued_count = 0;
public:
    void push(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(msg);
        total_received_count++;
        if (q.size() > peak_queued_count) {
            peak_queued_count = static_cast<uint32_t>(q.size());
        }
    }

    bool pop(std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        if (q.empty()) {
            failed_get_count++;
            return false;
        }
        msg = q.front();
        q.pop();
        total_processed_count++;
        return true;
    }

    void get_stats(uint32_t& current_queued, uint32_t& total_stored, 
                   uint32_t& total_processed, uint32_t& failed_gets, uint32_t& peak_queued) {
        std::lock_guard<std::mutex> lock(mtx);
        current_queued = static_cast<uint32_t>(q.size());
        total_stored = total_received_count;
        total_processed = total_processed_count;
        failed_gets = failed_get_count;
        peak_queued = peak_queued_count;
    }
};

bool send_all(SOCKET s, const char* buf, int len) {
    int total = 0;
    while (total < len) {
        int n = send(s, buf + total, len - total, 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

bool recv_all(SOCKET s, char* buf, int len) {
    int total = 0;
    while (total < len) {
        int n = recv(s, buf + total, len - total, 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

// Handles individual client connections persistently
void handle_client(SOCKET client_sock, MessageQueue& mq) {
    char cmd = 0;
    
    // SCHLEIFE: Hält die Verbindung offen, bis der Client sie schließt
    while (recv_all(client_sock, &cmd, 1)) {
        
        if (cmd == 'S') { // SEND Command
            uint32_t len = 0;
            // Wenn etwas beim Lesen/Schreiben schiefgeht, brechen wir sauber ab
            if (!recv_all(client_sock, reinterpret_cast<char*>(&len), 4)) break;
            
            std::string msg(len, '\0');
            if (!recv_all(client_sock, &msg[0], len)) break;
            
            mq.push(msg);
            char ack = 'O'; // Acknowledge 'OK'
            if (!send_all(client_sock, &ack, 1)) break;
        } 
        else if (cmd == 'G') { // GET Command
            std::string msg;
            uint32_t len = 0;
            if (mq.pop(msg)) {
                len = static_cast<uint32_t>(msg.size());
                if (!send_all(client_sock, reinterpret_cast<const char*>(&len), 4)) break;
                if (!send_all(client_sock, msg.data(), len)) break;
            } else {
                if (!send_all(client_sock, reinterpret_cast<const char*>(&len), 4)) break;
            }
        }
        else if (cmd == 'T') { // STATS Command
            uint32_t current_queued, total_stored, total_processed, failed_gets, peak_queued;
            mq.get_stats(current_queued, total_stored, total_processed, failed_gets, peak_queued);
            
            if (!send_all(client_sock, reinterpret_cast<const char*>(&current_queued), 4) ||
                !send_all(client_sock, reinterpret_cast<const char*>(&total_stored), 4) ||
                !send_all(client_sock, reinterpret_cast<const char*>(&total_processed), 4) ||
                !send_all(client_sock, reinterpret_cast<const char*>(&failed_gets), 4) ||
                !send_all(client_sock, reinterpret_cast<const char*>(&peak_queued), 4)) {
                break;
            }
        }
        else if (cmd == 'X') { // EXIT Command
            char ack = 'O';
            send_all(client_sock, &ack, 1);
            closesocket(client_sock);
            ExitProcess(0); 
        }
        else {
            // Unbekannter Befehl -> Protokoll-Fehler, Verbindung kappen
            break; 
        }
    }

    closesocket(client_sock);
}

// RAII Wrapper for Winsock initialization
class WinsockContext {
public:
    WinsockContext() {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "Error: Winsock initialization failed.\n";
            exit(1);
        }
    }
    ~WinsockContext() {
        WSACleanup();
    }
};

SOCKET connect_to_server() {
    struct addrinfo hints, *result = nullptr;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo("127.0.0.1", DEFAULT_PORT, &hints, &result) != 0) {
        return INVALID_SOCKET;
    }

    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    if (connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) == SOCKET_ERROR) {
        closesocket(sock);
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    freeaddrinfo(result);
    return sock;
}

int run_server() {
    WinsockContext wsctx;
    struct addrinfo hints, *result = nullptr;
    
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;       
    hints.ai_socktype = SOCK_STREAM; 
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;     

    if (getaddrinfo("127.0.0.1", DEFAULT_PORT, &hints, &result) != 0) {
        std::cerr << "Error: Could not resolve addrinfo.\n";
        return 1;
    }

    SOCKET listen_sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (listen_sock == INVALID_SOCKET) {
        std::cerr << "Error: Failed to create socket.\n";
        freeaddrinfo(result);
        return 1;
    }

    if (bind(listen_sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) == SOCKET_ERROR) {
        std::cerr << "Error: Port " << DEFAULT_PORT << " is already in use or bind failed.\n";
        closesocket(listen_sock);
        freeaddrinfo(result);
        return 1;
    }

    freeaddrinfo(result);

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Error: Listen failed.\n";
        closesocket(listen_sock);
        return 1;
    }

    MessageQueue mq;

    while (true) {
        SOCKET client_sock = accept(listen_sock, nullptr, nullptr);
        if (client_sock == INVALID_SOCKET) {
            continue; 
        }
        
        std::thread([client_sock, &mq]() {
            handle_client(client_sock, mq);
        }).detach();
    }

    closesocket(listen_sock);
    return 0;
}

int init_background_server(const std::string& exe_path) {
    {
        WinsockContext wsctx;
        SOCKET test_sock = connect_to_server();
        if (test_sock != INVALID_SOCKET) {
            closesocket(test_sock);
            std::cerr << "Info: Server is already running.\n";
            return 0;
        }
    }

    std::string cmd = "\"" + exe_path + "\" " + INTERNAL_SERVER_FLAG;
    std::vector<char> cmd_buffer(cmd.begin(), cmd.end());
    cmd_buffer.push_back('\0');

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessA(NULL, cmd_buffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::cerr << "Success: Broker server initialized in the background.\n";
        return 0;
    } else {
        std::cerr << "Error: Failed to spawn background process (Error Code: " << GetLastError() << ").\n";
        return 1;
    }
}

// Sende eine EINE Nachricht (Einmalig)
int run_send(const std::string& message) {
    WinsockContext wsctx;
    SOCKET sock = connect_to_server();
    if (sock == INVALID_SOCKET) {
        std::cerr << "Error: Server is not running or connection refused.\n";
        return 1;
    }

    char cmd = 'S';
    uint32_t len = static_cast<uint32_t>(message.size());

    if (!send_all(sock, &cmd, 1) ||
        !send_all(sock, reinterpret_cast<const char*>(&len), 4) ||
        !send_all(sock, message.data(), len)) {
        std::cerr << "Error: Failed to transmit message.\n";
        closesocket(sock);
        return 1;
    }

    char ack = 0;
    recv_all(sock, &ack, 1); 
    closesocket(sock);
    return 0;
}

// NEU: Hält die Verbindung und liest kontinuierlich aus der Konsole (Pipe)
int run_send_stream() {
    WinsockContext wsctx;
    SOCKET sock = connect_to_server();
    if (sock == INVALID_SOCKET) {
        std::cerr << "Error: Server is not running or connection refused.\n";
        return 1;
    }

    std::string line;
    // Liest Zeile für Zeile bis EOF (Pipe wird geschlossen)
    while (std::getline(std::cin, line)) {
        char cmd = 'S';
        uint32_t len = static_cast<uint32_t>(line.size());

        if (!send_all(sock, &cmd, 1) ||
            !send_all(sock, reinterpret_cast<const char*>(&len), 4) ||
            !send_all(sock, line.data(), len)) {
            std::cerr << "Error: Connection lost while sending.\n";
            break;
        }

        char ack = 0;
        if (!recv_all(sock, &ack, 1)) {
            break; // Server hat die Verbindung abgebrochen
        }
    }

    closesocket(sock);
    return 0;
}

int run_get() {
    WinsockContext wsctx;
    SOCKET sock = connect_to_server();
    if (sock == INVALID_SOCKET) {
        std::cerr << "Error: Server is not running.\n";
        return 1;
    }

    char cmd = 'G';
    if (!send_all(sock, &cmd, 1)) {
        closesocket(sock);
        return 1;
    }

    uint32_t len = 0;
    if (!recv_all(sock, reinterpret_cast<char*>(&len), 4)) {
        closesocket(sock);
        return 1;
    }

    if (len == 0) {
        closesocket(sock);
        return 2; 
    }

    std::string msg(len, '\0');
    if (!recv_all(sock, &msg[0], len)) {
        closesocket(sock);
        return 1;
    }

    std::cout << msg;
    closesocket(sock);
    return 0;
}

int run_stats() {
    // [Code bleibt exakt gleich, zur Kürze hier gekürzt gedacht, füge deinen bestehenden Code ein]
    WinsockContext wsctx;
    SOCKET sock = connect_to_server();
    if (sock == INVALID_SOCKET) return 1;

    char cmd = 'T'; // Stats
    send_all(sock, &cmd, 1);

    uint32_t current_queued = 0, total_stored = 0, total_processed = 0, failed_gets = 0, peak_queued = 0;
    if (!recv_all(sock, reinterpret_cast<char*>(&current_queued), 4) ||
        !recv_all(sock, reinterpret_cast<char*>(&total_stored), 4) ||
        !recv_all(sock, reinterpret_cast<char*>(&total_processed), 4) ||
        !recv_all(sock, reinterpret_cast<char*>(&failed_gets), 4) ||
        !recv_all(sock, reinterpret_cast<char*>(&peak_queued), 4)) {
        closesocket(sock);
        return 1;
    }

    std::cout << "Current queued messages:  " << current_queued << "\n"
              << "Peak queued (Max Load):   " << peak_queued << "\n"
              << "Total received messages:  " << total_stored << "\n"
              << "Total processed (get):    " << total_processed << "\n"
              << "Failed 'get' attempts:    " << failed_gets << "\n";

    closesocket(sock);
    return 0;
}

int run_exit() {
    WinsockContext wsctx;
    SOCKET sock = connect_to_server();
    if (sock == INVALID_SOCKET) return 1;

    char cmd = 'X';
    send_all(sock, &cmd, 1);
    char ack = 0;
    recv_all(sock, &ack, 1);
    closesocket(sock);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " init              -> Starts the headless background broker server\n"
                  << "  " << argv[0] << " send \"message\"   -> Pushes a message to the queue\n"
                  << "  " << argv[0] << " send --keep-alive -> Reads continuously from stdin (pipe mode)\n"
                  << "  " << argv[0] << " get               -> Retrieves the oldest message from the queue\n"
                  << "  " << argv[0] << " stats             -> Displays queue analytics\n"
                  << "  " << argv[0] << " exit              -> Shuts down the background server\n";
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "init") {
        return init_background_server(argv[0]);
    } else if (mode == INTERNAL_SERVER_FLAG) {
        return run_server();
    } else if (mode == "send") {
        if (argc >= 3) {
            std::string arg = argv[2];
            if (arg == "--keep-alive") {
                return run_send_stream(); // NEU: Stream-Modus
            } else {
                return run_send(arg);     // ALT: One-Shot-Modus
            }
        } else {
            std::cerr << "Error: Missing message content or --keep-alive flag.\n";
            return 1;
        }
    } else if (mode == "get") {
        return run_get();
    } else if (mode == "stats") {
        return run_stats();
    } else if (mode == "exit") { 
        return run_exit();       
    } else {
        std::cerr << "Error: Unknown command: " << mode << "\n";
        return 1;
    }
}