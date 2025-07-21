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
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#include "common/swaglog.h"
#include "common/util.h"
#include "third_party/raylib/include/raylib.h"

const int SCREEN_WIDTH = 2160;
const int SCREEN_HEIGHT = 1080;
const int CHAR_WIDTH = 12;
const int CHAR_HEIGHT = 20;
const int COLS = SCREEN_WIDTH / CHAR_WIDTH;
const int ROWS = (SCREEN_HEIGHT - 100) / CHAR_HEIGHT;

struct TerminalCell {
    char ch;
    Color fg_color;
    Color bg_color;
    bool bold;
};

class TerminalDisplay {
private:
    int master_fd;
    pid_t child_pid;
    std::vector<std::vector<TerminalCell>> screen;
    int cursor_row, cursor_col;
    bool running;
    std::mutex screen_mutex;
    std::thread pty_thread;

    Color current_fg = WHITE;
    Color current_bg = BLACK;
    bool current_bold = false;

    std::string escape_buffer;
    bool in_escape = false;

public:
    TerminalDisplay() : cursor_row(0), cursor_col(0), running(true),
                       master_fd(-1), child_pid(-1) {
        screen.resize(ROWS, std::vector<TerminalCell>(COLS));
        clear_screen();
        LOG("Terminal Display initialized");
    }

    ~TerminalDisplay() {
        stop();
    }

    bool start_shell() {
        struct winsize ws;
        ws.ws_row = ROWS;
        ws.ws_col = COLS;
        ws.ws_xpixel = SCREEN_WIDTH;
        ws.ws_ypixel = SCREEN_HEIGHT;

        if (openpty(&master_fd, nullptr, nullptr, nullptr, &ws) == -1) {
            LOGE("Failed to create PTY: %s", strerror(errno));
            return false;
        }

        child_pid = fork();
        if (child_pid == -1) {
            LOGE("Failed to fork: %s", strerror(errno));
            close(master_fd);
            return false;
        }

        if (child_pid == 0) {
            // Child process - exec shell
            setsid();

            char *slave_name = ptsname(master_fd);
            if (slave_name == nullptr) {
                LOGE("Failed to get slave PTY name");
                exit(1);
            }
            
            int slave_fd = open(slave_name, O_RDWR);
            if (slave_fd == -1) {
                LOGE("Failed to open slave PTY: %s", strerror(errno));
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

        // Parent process
        fcntl(master_fd, F_SETFL, O_NONBLOCK);
        
        LOG("Shell started with PID %d", child_pid);
        pty_thread = std::thread(&TerminalDisplay::handle_pty_output, this);
        return true;
    }

    void handle_pty_output() {
        char buffer[4096];

        while (running) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(master_fd, &read_fds);

            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000; // 50ms

            int result = select(master_fd + 1, &read_fds, nullptr, nullptr, &timeout);
            if (result > 0 && FD_ISSET(master_fd, &read_fds)) {
                ssize_t bytes_read = read(master_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    process_output(std::string(buffer, bytes_read));
                } else if (bytes_read == 0) {
                    LOG("Shell process ended");
                    break;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    LOGE("PTY read error: %s", strerror(errno));
                    break;
                }
            }
        }
    }

    void stop() {
        running = false;

        if (pty_thread.joinable()) {
            pty_thread.join();
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
            if (i + 1 < static_cast<int>(screen.size())) {
                screen[i] = screen[i + 1];
            }
        }
        
        if (ROWS - 1 < static_cast<int>(screen.size())) {
            for (auto& cell : screen[ROWS - 1]) {
                cell.ch = ' ';
                cell.fg_color = WHITE;
                cell.bg_color = BLACK;
                cell.bold = false;
            }
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
                        for (int r = cursor_row; r < ROWS && r < static_cast<int>(screen.size()); r++) {
                            int start_col = (r == cursor_row) ? cursor_col : 0;
                            for (int c = start_col; c < COLS && c < static_cast<int>(screen[r].size()); c++) {
                                screen[r][c] = {' ', WHITE, BLACK, false};
                            }
                        }
                    } else if (nums[0] == 2) {
                        clear_screen();
                    }
                    break;
                case 'K':
                    if ((nums.empty() || nums[0] == 0) && cursor_row >= 0 && cursor_row < static_cast<int>(screen.size())) {
                        for (int c = cursor_col; c < COLS && c < static_cast<int>(screen[cursor_row].size()); c++) {
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

        // Validate screen dimensions
        if (screen.empty() || screen.size() != ROWS) {
            return;
        }

        for (int row = 0; row < ROWS; row++) {
            if (row >= static_cast<int>(screen.size()) || screen[row].size() != COLS) {
                continue;
            }
            
            for (int col = 0; col < COLS; col++) {
                if (col >= static_cast<int>(screen[row].size())) {
                    continue;
                }
                
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

        // Draw cursor
        if (cursor_row >= 0 && cursor_row < ROWS && cursor_col >= 0 && cursor_col < COLS) {
            int cursor_x = cursor_col * CHAR_WIDTH;
            int cursor_y = 80 + cursor_row * CHAR_HEIGHT;
            DrawRectangle(cursor_x, cursor_y, 2, CHAR_HEIGHT, WHITE);
        }
    }

    bool is_running() const { return running; }
    
    std::string get_pty_name() const {
        if (master_fd != -1) {
            char* name = ptsname(master_fd);
            return name ? std::string(name) : "";
        }
        return "";
    }
};

int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Terminal Display");
    SetTargetFPS(60);

    Font font = GetFontDefault();
    LOG("Terminal Display started");

    TerminalDisplay terminal;
    if (!terminal.start_shell()) {
        LOGE("Failed to start terminal");
        CloseWindow();
        return 1;
    }

    std::string pty_name = terminal.get_pty_name();
    LOG("PTY available at: %s", pty_name.c_str());

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

        DrawTextEx(font, "Terminal Display - Touch top to exit", {20, 10}, 20, 1, WHITE);
        if (!pty_name.empty()) {
            std::string pty_info = "PTY: " + pty_name;
            DrawTextEx(font, pty_info.c_str(), {20, 35}, 16, 1, GRAY);
        }
        DrawTextEx(font, "SSH to this device and use this terminal", {20, 55}, 16, 1, GREEN);

        terminal.render(font);

        EndDrawing();
    }

    terminal.stop();
    CloseWindow();

    return 0;
}