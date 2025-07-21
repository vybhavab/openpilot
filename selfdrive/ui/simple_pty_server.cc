#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstring>
#include <errno.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#include "common/swaglog.h"

const int PORT = 2222;

volatile bool running = true;

void signal_handler(int sig) {
    running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    LOG("Starting simple PTY server on port %d", PORT);
    
    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOGE("socket failed: %s", strerror(errno));
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        LOGE("bind failed: %s", strerror(errno));
        close(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 1) < 0) {
        LOGE("listen failed: %s", strerror(errno));
        close(server_fd);
        return 1;
    }
    
    LOG("PTY server listening on port %d", PORT);
    
    while (running) {
        // Accept connection
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int result = select(server_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (result <= 0) continue;
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            LOGE("accept failed: %s", strerror(errno));
            continue;
        }
        
        LOG("Client connected");
        
        // Create PTY
        int master_fd;
        pid_t pid = forkpty(&master_fd, nullptr, nullptr, nullptr);
        
        if (pid < 0) {
            LOGE("forkpty failed: %s", strerror(errno));
            close(client_fd);
            continue;
        }
        
        if (pid == 0) {
            // Child process - exec shell
            setenv("TERM", "xterm", 1);
            setenv("PS1", "comma3:$ ", 1);
            execl("/bin/bash", "bash", "-l", nullptr);
            exit(1);
        }
        
        // Parent process - relay data
        char buffer[4096];
        
        while (running) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(master_fd, &fds);
            FD_SET(client_fd, &fds);
            
            int max_fd = (master_fd > client_fd) ? master_fd : client_fd;
            
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            
            result = select(max_fd + 1, &fds, nullptr, nullptr, &timeout);
            if (result <= 0) continue;
            
            // Data from PTY to client
            if (FD_ISSET(master_fd, &fds)) {
                ssize_t n = read(master_fd, buffer, sizeof(buffer));
                if (n <= 0) break;
                
                ssize_t sent = 0;
                while (sent < n) {
                    ssize_t s = write(client_fd, buffer + sent, n - sent);
                    if (s <= 0) break;
                    sent += s;
                }
                if (sent != n) break;
            }
            
            // Data from client to PTY
            if (FD_ISSET(client_fd, &fds)) {
                ssize_t n = read(client_fd, buffer, sizeof(buffer));
                if (n <= 0) break;
                
                ssize_t sent = 0;
                while (sent < n) {
                    ssize_t s = write(master_fd, buffer + sent, n - sent);
                    if (s <= 0) break;
                    sent += s;
                }
                if (sent != n) break;
            }
        }
        
        // Cleanup
        close(client_fd);
        close(master_fd);
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        
        LOG("Client disconnected");
    }
    
    close(server_fd);
    LOG("Server stopped");
    return 0;
}