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

// Include raylib after other headers to avoid macro conflicts
#include "third_party/raylib/include/raylib.h"

// Don't include hardware header due to macro conflicts - use direct calls instead

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
    bool underline;
};

class PTYTerminal {
private:
    int master_fd;
    pid_t child_pid;
    std::vector<std::vector<TerminalCell>> screen;
    int cursor_row, cursor_col;
    bool running;
    std::mutex screen_mutex;
    std::thread read_thread;

    Color current_fg = WHITE;
    Color current_bg = BLACK;
    bool current_bold = false;
    bool current_underline = false;

    std::string escape_buffer;
    bool in_escape = false;

public:
    PTYTerminal() : cursor_row(0), cursor_col(0), running(true), master_fd(-1), child_pid(-1) {
        try {
            screen.resize(ROWS, std::vector<TerminalCell>(COLS));
            clear_screen();
            LOG("PTY Terminal initialized successfully");
        } catch (const std::exception& e) {
            LOGE("Failed to initialize PTY Terminal: %s", e.what());
            running = false;
        }
    }

    ~PTYTerminal() {
        stop();
    }

    bool start_shell() {
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

            execl("/bin/bash", "bash", "-l", nullptr);
            exit(1);
        }

        fcntl(master_fd, F_SETFL, O_NONBLOCK);
        read_thread = std::thread(&PTYTerminal::read_from_pty, this);

        return true;
    }

    void stop() {
        running = false;
        if (read_thread.joinable()) {
            read_thread.join();
        }
        if (master_fd >= 0) {
            close(master_fd);
        }
        if (child_pid > 0) {
            kill(child_pid, SIGTERM);
            waitpid(child_pid, nullptr, 0);
        }
    }

    void write_to_pty(const std::string& data) {
        if (master_fd >= 0 && !data.empty()) {
            ssize_t written = write(master_fd, data.c_str(), data.length());
            if (written < 0) {
                LOGE("Failed to write to PTY: %s", strerror(errno));
            }
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
                cell.underline = false;
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
                screen[cursor_row][cursor_col] = {
                    ch, current_fg, current_bg, current_bold, current_underline
                };
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
            cell.underline = false;
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
                    {
                        std::lock_guard<std::mutex> lock(screen_mutex);
                        cursor_row = (nums.size() > 0 ? nums[0] - 1 : 0);
                        cursor_col = (nums.size() > 1 ? nums[1] - 1 : 0);
                        cursor_row = std::max(0, std::min(cursor_row, ROWS - 1));
                        cursor_col = std::max(0, std::min(cursor_col, COLS - 1));
                    }
                    break;
                case 'A':
                    {
                        std::lock_guard<std::mutex> lock(screen_mutex);
                        cursor_row = std::max(0, cursor_row - (nums.empty() ? 1 : nums[0]));
                    }
                    break;
                case 'B':
                    {
                        std::lock_guard<std::mutex> lock(screen_mutex);
                        cursor_row = std::min(ROWS - 1, cursor_row + (nums.empty() ? 1 : nums[0]));
                    }
                    break;
                case 'C':
                    {
                        std::lock_guard<std::mutex> lock(screen_mutex);
                        cursor_col = std::min(COLS - 1, cursor_col + (nums.empty() ? 1 : nums[0]));
                    }
                    break;
                case 'D':
                    {
                        std::lock_guard<std::mutex> lock(screen_mutex);
                        cursor_col = std::max(0, cursor_col - (nums.empty() ? 1 : nums[0]));
                    }
                    break;
                case 'J':
                    {
                        std::lock_guard<std::mutex> lock(screen_mutex);
                        if (nums.empty() || nums[0] == 0) {
                            for (int r = cursor_row; r < ROWS; r++) {
                                int start_col = (r == cursor_row) ? cursor_col : 0;
                                for (int c = start_col; c < COLS; c++) {
                                    if (r >= 0 && r < ROWS && c >= 0 && c < COLS) {
                                        screen[r][c] = {' ', WHITE, BLACK, false, false};
                                    }
                                }
                            }
                        } else if (nums[0] == 2) {
                            for (auto& row : screen) {
                                for (auto& cell : row) {
                                    cell = {' ', WHITE, BLACK, false, false};
                                }
                            }
                            cursor_row = cursor_col = 0;
                        }
                    }
                    break;
                case 'K':
                    {
                        std::lock_guard<std::mutex> lock(screen_mutex);
                        if (nums.empty() || nums[0] == 0) {
                            for (int c = cursor_col; c < COLS; c++) {
                                if (cursor_row >= 0 && cursor_row < ROWS && c >= 0 && c < COLS) {
                                    screen[cursor_row][c] = {' ', WHITE, BLACK, false, false};
                                }
                            }
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
                                current_underline = false;
                                break;
                            case 1:
                                current_bold = true;
                                break;
                            case 4:
                                current_underline = true;
                                break;
                            case 30: current_fg = BLACK; break;
                            case 31: current_fg = RED; break;
                            case 32: current_fg = GREEN; break;
                            case 33: current_fg = YELLOW; break;
                            case 34: current_fg = BLUE; break;
                            case 35: current_fg = MAGENTA; break;
                            case 36: current_fg = SKYBLUE; break;
                            case 37: current_fg = WHITE; break;
                            case 40: current_bg = BLACK; break;
                            case 41: current_bg = RED; break;
                            case 42: current_bg = GREEN; break;
                            case 43: current_bg = YELLOW; break;
                            case 44: current_bg = BLUE; break;
                            case 45: current_bg = MAGENTA; break;
                            case 46: current_bg = SKYBLUE; break;
                            case 47: current_bg = WHITE; break;
                        }
                    }
                    break;
            }
        }
    }

    void read_from_pty() {
        char buffer[4096];

        while (running) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(master_fd, &read_fds);

            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000;

            int result = select(master_fd + 1, &read_fds, nullptr, nullptr, &timeout);
            if (result > 0 && FD_ISSET(master_fd, &read_fds)) {
                ssize_t bytes_read = read(master_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    process_output(std::string(buffer, bytes_read));
                } else if (bytes_read == 0) {
                    running = false;
                    break;
                }
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

        for (int row = 0; row < ROWS && row < (int)screen.size(); row++) {
            for (int col = 0; col < COLS && col < (int)screen[row].size(); col++) {
                const auto& cell = screen[row][col];

                int x = col * CHAR_WIDTH;
                int y = 50 + row * CHAR_HEIGHT;

                if (cell.bg_color.r != 0 || cell.bg_color.g != 0 || cell.bg_color.b != 0) {
                    DrawRectangle(x, y, CHAR_WIDTH, CHAR_HEIGHT, cell.bg_color);
                }

                if (cell.ch != ' ' && cell.ch != '\0') {
                    Color text_color = cell.fg_color;
                    if (cell.bold) {
                        text_color.r = std::min(255, (int)(text_color.r * 1.3));
                        text_color.g = std::min(255, (int)(text_color.g * 1.3));
                        text_color.b = std::min(255, (int)(text_color.b * 1.3));
                    }

                    char str[2] = {cell.ch, '\0'};
                    DrawTextEx(font, str, {(float)x, (float)y}, 16, 1, text_color);

                    if (cell.underline) {
                        DrawLine(x, y + CHAR_HEIGHT - 2, x + CHAR_WIDTH, y + CHAR_HEIGHT - 2, text_color);
                    }
                }
            }
        }

        if (cursor_row >= 0 && cursor_row < ROWS && cursor_col >= 0 && cursor_col < COLS) {
            int cursor_x = cursor_col * CHAR_WIDTH;
            int cursor_y = 50 + cursor_row * CHAR_HEIGHT;
            DrawRectangle(cursor_x, cursor_y, 2, CHAR_HEIGHT, WHITE);
        }
    }

    bool is_running() const { return running; }
};

// Font loading disabled for now to avoid segfaults

int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "PTY Terminal");
    SetTargetFPS(60);

    Font font = GetFontDefault();
    LOG("Using default raylib font");

    PTYTerminal terminal;
    if (!terminal.start_shell()) {
        LOGE("Failed to start shell");
        CloseWindow();
        return 1;
    }
    
    // Keep display on using direct system call
    std::system("echo 0 > /sys/class/backlight/panel0-backlight/bl_power 2>/dev/null || true");

    while (!WindowShouldClose() && terminal.is_running()) {
        // Handle touch/mouse input for screen wake-up and exit
        bool touch_detected = false;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            touch_detected = true;
            Vector2 mousePos = GetMousePosition();
            if (mousePos.y < 50 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                break;  // Exit if touching top area
            }
        }

        // Handle character input
        int key = GetCharPressed();
        while (key > 0) {
            if (key >= 32 && key <= 126) {
                char ch = (char)key;
                terminal.write_to_pty(std::string(1, ch));
            }
            key = GetCharPressed();
        }

        // Handle special keys and detect input activity
        bool input_activity = false;
        if (IsKeyPressed(KEY_ENTER)) {
            terminal.write_to_pty("\r");
            input_activity = true;
        } else if (IsKeyPressed(KEY_BACKSPACE)) {
            terminal.write_to_pty("\x7f");
            input_activity = true;
        } else if (IsKeyPressed(KEY_TAB)) {
            terminal.write_to_pty("\t");
            input_activity = true;
        } else if (IsKeyPressed(KEY_UP)) {
            terminal.write_to_pty("\033[A");
            input_activity = true;
        } else if (IsKeyPressed(KEY_DOWN)) {
            terminal.write_to_pty("\033[B");
            input_activity = true;
        } else if (IsKeyPressed(KEY_RIGHT)) {
            terminal.write_to_pty("\033[C");
            input_activity = true;
        } else if (IsKeyPressed(KEY_LEFT)) {
            terminal.write_to_pty("\033[D");
            input_activity = true;
        } else if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_C)) {
            terminal.write_to_pty("\003");
            input_activity = true;
        } else if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_D)) {
            terminal.write_to_pty("\004");
            input_activity = true;
        }
        
        // Keep display active if there's any input activity or touch
        static auto last_activity = std::chrono::steady_clock::now();
        if (input_activity || touch_detected || key > 0) {
            last_activity = std::chrono::steady_clock::now();
            // Keep display on using direct system call
            std::system("echo 0 > /sys/class/backlight/panel0-backlight/bl_power 2>/dev/null || true");
        }
        
        // Refresh display power every 30 seconds to prevent sleep
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_activity).count() < 300) {
            static auto last_refresh = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_refresh).count() > 30) {
                // Keep display on using direct system call
                std::system("echo 0 > /sys/class/backlight/panel0-backlight/bl_power 2>/dev/null || true");
                last_refresh = now;
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        DrawTextEx(font, "PTY Terminal - Touch top area to exit", {20, 10}, 20, 1, WHITE);

        terminal.render(font);

        EndDrawing();
    }

    terminal.stop();
    CloseWindow();

    return 0;
}
