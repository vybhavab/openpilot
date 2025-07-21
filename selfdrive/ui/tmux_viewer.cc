#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>

#include "third_party/raylib/include/raylib.h"
#include "common/swaglog.h"
#include "common/util.h"

const int SCREEN_WIDTH = 2160;
const int SCREEN_HEIGHT = 1080;
const int FONT_SIZE = 24;
const int LINE_HEIGHT = 28;
const int MAX_LINES = (SCREEN_HEIGHT - 100) / LINE_HEIGHT;

std::string tmux_session = "default";
std::string tmux_content;
bool running = true;

std::string runCommand(const std::string &cmd) {
    std::string temp_file = "/tmp/tmux_raylib_output.txt";
    std::string full_cmd = cmd + " > " + temp_file + " 2>/dev/null";
    
    int result = std::system(full_cmd.c_str());
    if (result != 0) {
        return "";
    }
    
    std::ifstream file(temp_file);
    if (!file.is_open()) {
        return "";
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    std::system(("rm -f " + temp_file).c_str());
    
    return content;
}

void updateTmuxContent() {
    while (running) {
        std::string cmd = "tmux capture-pane -t " + tmux_session + " -e -p";
        tmux_content = runCommand(cmd);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void drawText(const std::string &text, int x, int y, int fontSize, Color color) {
    DrawText(text.c_str(), x, y, fontSize, color);
}

int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Tmux Viewer");
    SetTargetFPS(30);
    
    std::string check_cmd = "tmux has-session -t " + tmux_session + " 2>/dev/null";
    if (std::system(check_cmd.c_str()) != 0) {
        std::string create_cmd = "tmux new-session -d -s " + tmux_session;
        std::system(create_cmd.c_str());
    }
    
    std::thread update_thread(updateTmuxContent);
    
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BLACK);
        
        drawText("Tmux Viewer - Session: " + tmux_session, 20, 20, FONT_SIZE, WHITE);
        drawText("Press ESC to exit", 20, SCREEN_HEIGHT - 40, 20, GRAY);
        
        std::istringstream stream(tmux_content);
        std::string line;
        int line_num = 0;
        int y_offset = 80;
        
        while (std::getline(stream, line) && line_num < MAX_LINES) {
            if (line.length() > 120) {
                line = line.substr(0, 120) + "...";
            }
            drawText(line, 20, y_offset + (line_num * LINE_HEIGHT), 20, WHITE);
            line_num++;
        }
        
        EndDrawing();
        
        if (IsKeyPressed(KEY_ESCAPE)) {
            break;
        }
    }
    
    running = false;
    update_thread.join();
    CloseWindow();
    
    return 0;
}