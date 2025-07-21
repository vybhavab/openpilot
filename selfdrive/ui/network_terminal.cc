#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#include "common/swaglog.h"
#include "common/util.h"
#include "third_party/raylib/include/raylib.h"

const int SCREEN_WIDTH = 2160;
const int SCREEN_HEIGHT = 1080;
const int CHAR_WIDTH = 12;
const int CHAR_HEIGHT = 20;
const int COLS = SCREEN_WIDTH / CHAR_WIDTH;
const int ROWS = (SCREEN_HEIGHT - 100) / CHAR_HEIGHT;
const int SSH_PORT = 2222;

struct TerminalCell {
    char ch;
    Color fg_color;
    Color bg_color;
    bool bold;
};

class NetworkTerminal {
private:
    int master_fd;
    int server_socket;
    int client_socket;
    pid_t child_pid;
    std::vector<std::vector<TerminalCell>> screen;
    int cursor_row, cursor_col;
    bool running;
    bool client_connected;
    std::mutex screen_mutex;
    std::thread server_thread;
    std::thread pty_thread;

    Color current_fg = WHITE;
    Color current_bg = BLACK;
    bool current_bold = false;

    std::string escape_buffer;
    bool in_escape = false;

public:
    NetworkTerminal() : cursor_row(0), cursor_col(0), running(true), client_connected(false),
                       server_socket(-1), client_socket(-1), master_fd(-1), child_pid(-1) {
        screen.resize(ROWS, std::vector<TerminalCell>(COLS));
        clear_screen();
        LOG("Network Terminal initialized");
    }

    ~NetworkTerminal() {
        stop();
    }

    bool start_server() {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == -1) {
            LOGE("Failed to create socket");
            return false;
        }

        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(SSH_PORT);

        if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
            LOGE("Failed to bind socket");
            close(server_socket);
            return false;
        }

        if (listen(server_socket, 1) == -1) {
            LOGE("Failed to listen on socket");
            close(server_socket);
            return false;
        }

        LOG("Network Terminal listening on port %d", SSH_PORT);
        server_thread = std::thread(&NetworkTerminal::accept_connections, this);
        return true;
    }

    void accept_connections() {
        while (running) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server_socket, &read_fds);

            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int result = select(server_socket + 1, &read_fds, nullptr, nullptr, &timeout);
            if (result > 0 && FD_ISSET(server_socket, &read_fds)) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);

                int new_client = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
                if (new_client != -1) {
                    if (client_connected) {
                        close(new_client);
                        continue;
                    }

                    client_socket = new_client;
                    client_connected = true;

                    LOG("Client connected from %s", inet_ntoa(client_addr.sin_addr));

                    if (start_pty()) {
                        pty_thread = std::thread(&NetworkTerminal::handle_pty_io, this);
                    } else {
                        close(client_socket);
                        client_connected = false;
                    }
                }
            }
        }
    }

    bool start_pty() {
        struct winsize ws;
        ws.ws_row = ROWS;
        ws.ws_col = COLS;
        ws.ws_xpixel = SCREEN_WIDTH;
        ws.ws_ypixel = SCREEN_HEIGHT;

        if (openpty(&master_fd, nullptr, nullptr, nullptr, &ws) == -1) {
            LOGE("Failed to create PTY");
            return false;
        }

        child_pid = fork();
        if (child_pid == -1) {
            LOGE("Failed to fork");
            close(master_fd);
            return false;
        }

        if (child_pid == 0) {
            setsid();

            char *slave_name = ptsname(master_fd);
            if (slave_name == nullptr) {
                exit(1);
            }
            
            int slave_fd = open(slave_name, O_RDWR);
            if (slave_fd == -1) {
                exit(1);
            }

            dup2(slave_fd, STDIN_FILENO);
            dup2(slave_fd, STDOUT_FILENO);
            dup2(slave_fd, STDERR_FILENO);
            close(slave_fd);
            close(master_fd);

            setenv("TERM", "xterm-256color", 1);
            setenv("COLUMNS", std::to_string(COLS).c_str(), 1);
            setenv("LINES", std::to_string(ROWS).c_str(), 1);
            setenv("PS1", "comma3:$ ", 1);

            execl("/bin/bash", "bash", "-l", nullptr);
            exit(1);
        }

        fcntl(master_fd, F_SETFL, O_NONBLOCK);
        fcntl(client_socket, F_SETFL, O_NONBLOCK);

        return true;
    }

    void handle_pty_io() {
        char buffer[4096];

        while (running && client_connected) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(master_fd, &read_fds);
            FD_SET(client_socket, &read_fds);

            int max_fd = std::max(master_fd, client_socket);

            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000;

            int result = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
            if (result > 0) {
                // Data from PTY to client and screen
                if (FD_ISSET(master_fd, &read_fds)) {
                    ssize_t bytes_read = read(master_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        process_output(std::string(buffer, bytes_read));
                        
                        // Send to client
                        send(client_socket, buffer, bytes_read, MSG_NOSIGNAL);
                    } else if (bytes_read == 0) {
                        break;
                    }
                }
                
                // Data from client to PTY
                if (FD_ISSET(client_socket, &read_fds)) {
                    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer));
                    if (bytes_read > 0) {
                        write(master_fd, buffer, bytes_read);
                    } else if (bytes_read == 0) {
                        break;
                    }
                }
            }
        }

        cleanup_client();
    }

    void cleanup_client() {
        client_connected = false;
        
        if (client_socket != -1) {
            close(client_socket);
            client_socket = -1;
        }

        if (child_pid > 0) {
            kill(child_pid, SIGTERM);
            waitpid(child_pid, nullptr, 0);
            child_pid = -1;
        }

        if (master_fd != -1) {
            close(master_fd);
            master_fd = -1;
        }

        clear_screen();
        LOG("Client disconnected");
    }

    void stop() {
        running = false;

        cleanup_client();

        if (server_thread.joinable()) {
            server_thread.join();
        }

        if (pty_thread.joinable()) {
            pty_thread.join();
        }

        if (server_socket != -1) {
            close(server_socket);
            server_socket = -1;
        }
    }

    void clear_screen() {
        std::lock_guard<std::mutex> lock(screen_mutex);
        for (auto& row : screen) {
            for (auto& cell : row) {
                cell.ch = ' ';
                cell.fg_color = WHITE;
                cell.bg_color = BLACK;
                cell.bold = false;
            }
        }
        cursor_row = cursor_col = 0;
    }

    void put_char(char ch) {
        std::lock_guard<std::mutex> lock(screen_mutex);

        if (ch == '\n') {
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= ROWS) {
                scroll_up();
                cursor_row = ROWS - 1;
            }
        } else if (ch == '\r') {
            cursor_col = 0;
        } else if (ch == '\b') {
            if (cursor_col > 0) cursor_col--;
        } else if (ch == '\t') {
            cursor_col = ((cursor_col / 8) + 1) * 8;
            if (cursor_col >= COLS) {
                cursor_col = 0;
                cursor_row++;
                if (cursor_row >= ROWS) {
                    scroll_up();
                    cursor_row = ROWS - 1;
                }
            }
        } else if (ch >= 32) {
            if (cursor_col >= COLS) {
                cursor_col = 0;
                cursor_row++;
                if (cursor_row >= ROWS) {
                    scroll_up();
                    cursor_row = ROWS - 1;
                }
            }

            if (cursor_row >= 0 && cursor_row < ROWS && cursor_col >= 0 && cursor_col < COLS) {
                screen[cursor_row][cursor_col] = {ch, current_fg, current_bg, current_bold};
                cursor_col++;
            }
        }
    }

    void scroll_up() {
        for (int i = 0; i < ROWS - 1; i++) {
            screen[i] = screen[i + 1];
        }
        for (auto& cell : screen[ROWS - 1]) {
            cell.ch = ' ';
            cell.fg_color = WHITE;
            cell.bg_color = BLACK;
            cell.bold = false;
        }
    }

    void process_escape_sequence(const std::string& seq) {
        if (seq.empty()) return;

        if (seq[0] == '[') {
            std::string params = seq.substr(1);
            char cmd = params.back();
            params.pop_back();

            std::vector<int> nums;
            std::string current_num;
            for (char c : params) {
                if (c == ';') {
                    nums.push_back(current_num.empty() ? 0 : std::stoi(current_num));
                    current_num.clear();
                } else if (isdigit(c)) {
                    current_num += c;
                }
            }
            if (!current_num.empty()) {
                nums.push_back(std::stoi(current_num));
            }

            switch (cmd) {
                case 'H':
                case 'f':
                    cursor_row = (nums.size() > 0 ? nums[0] - 1 : 0);
                    cursor_col = (nums.size() > 1 ? nums[1] - 1 : 0);
                    cursor_row = std::max(0, std::min(cursor_row, ROWS - 1));
                    cursor_col = std::max(0, std::min(cursor_col, COLS - 1));
                    break;
                case 'A':
                    cursor_row = std::max(0, cursor_row - (nums.empty() ? 1 : nums[0]));
                    break;
                case 'B':
                    cursor_row = std::min(ROWS - 1, cursor_row + (nums.empty() ? 1 : nums[0]));
                    break;
                case 'C':
                    cursor_col = std::min(COLS - 1, cursor_col + (nums.empty() ? 1 : nums[0]));
                    break;
                case 'D':
                    cursor_col = std::max(0, cursor_col - (nums.empty() ? 1 : nums[0]));
                    break;
                case 'J':
                    if (nums.empty() || nums[0] == 0) {
                        for (int r = cursor_row; r < ROWS; r++) {
                            int start_col = (r == cursor_row) ? cursor_col : 0;
                            for (int c = start_col; c < COLS; c++) {
                                screen[r][c] = {' ', WHITE, BLACK, false};
                            }
                        }
                    } else if (nums[0] == 2) {
                        clear_screen();
                    }
                    break;
                case 'K':
                    if (nums.empty() || nums[0] == 0) {
                        for (int c = cursor_col; c < COLS; c++) {
                            screen[cursor_row][c] = {' ', WHITE, BLACK, false};
                        }
                    }
                    break;
                case 'm':
                    for (int param : nums) {
                        switch (param) {
                            case 0:
                                current_fg = WHITE;
                                current_bg = BLACK;
                                current_bold = false;
                                break;
                            case 1:
                                current_bold = true;
                                break;
                            case 30: current_fg = BLACK; break;
                            case 31: current_fg = RED; break;
                            case 32: current_fg = GREEN; break;
                            case 33: current_fg = YELLOW; break;
                            case 34: current_fg = BLUE; break;
                            case 35: current_fg = MAGENTA; break;
                            case 36: current_fg = SKYBLUE; break;
                            case 37: current_fg = WHITE; break;
                        }
                    }
                    break;
            }
        }
    }

    void process_output(const std::string& data) {
        for (char ch : data) {
            if (ch == '\033') {
                in_escape = true;
                escape_buffer.clear();
            } else if (in_escape) {
                escape_buffer += ch;
                if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                    process_escape_sequence(escape_buffer);
                    in_escape = false;
                    escape_buffer.clear();
                }
            } else {
                put_char(ch);
            }
        }
    }

    void render(Font& font) {
        std::lock_guard<std::mutex> lock(screen_mutex);

        for (int row = 0; row < ROWS; row++) {
            for (int col = 0; col < COLS; col++) {
                const auto& cell = screen[row][col];

                int x = col * CHAR_WIDTH;
                int y = 80 + row * CHAR_HEIGHT;

                if (cell.bg_color.r != 0 || cell.bg_color.g != 0 || cell.bg_color.b != 0) {
                    DrawRectangle(x, y, CHAR_WIDTH, CHAR_HEIGHT, cell.bg_color);
                }

                if (cell.ch != ' ') {
                    Color text_color = cell.fg_color;
                    if (cell.bold) {
                        text_color.r = std::min(255, (int)(text_color.r * 1.3));
                        text_color.g = std::min(255, (int)(text_color.g * 1.3));
                        text_color.b = std::min(255, (int)(text_color.b * 1.3));
                    }

                    char str[2] = {cell.ch, '\0'};
                    DrawTextEx(font, str, {(float)x, (float)y}, 16, 1, text_color);
                }
            }
        }

        if (client_connected) {
            int cursor_x = cursor_col * CHAR_WIDTH;
            int cursor_y = 80 + cursor_row * CHAR_HEIGHT;
            DrawRectangle(cursor_x, cursor_y, 2, CHAR_HEIGHT, WHITE);
        }
    }

    bool is_running() const { return running; }
    bool has_client() const { return client_connected; }
};

int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Network Terminal");
    SetTargetFPS(60);

    Font font = GetFontDefault();
    LOG("Network Terminal started");

    NetworkTerminal terminal;
    if (!terminal.start_server()) {
        LOGE("Failed to start network terminal");
        CloseWindow();
        return 1;
    }

    while (!WindowShouldClose() && terminal.is_running()) {
        // Handle touch input for exit
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mousePos = GetMousePosition();
            if (mousePos.y < 50) {
                break;
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        DrawTextEx(font, "Network Terminal - Touch top to exit", {20, 10}, 20, 1, WHITE);
        DrawTextEx(font, ("Port: " + std::to_string(SSH_PORT)).c_str(), {20, 35}, 16, 1, GRAY);

        if (terminal.has_client()) {
            DrawTextEx(font, "Client Connected", {20, 55}, 16, 1, GREEN);
            terminal.render(font);
        } else {
            DrawTextEx(font, "Waiting for connection...", {20, 55}, 16, 1, YELLOW);
            DrawTextEx(font, "Connect with: python3 simple_pty_client.py <comma3_ip>", {20, 400}, 16, 1, WHITE);
            DrawTextEx(font, "Then you can SSH to other systems from this terminal", {20, 430}, 16, 1, LIGHTGRAY);
        }

        EndDrawing();
    }

    terminal.stop();
    CloseWindow();

    return 0;
}