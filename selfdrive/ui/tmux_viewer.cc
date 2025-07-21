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
const int LINE_HEIGHT = 28;
const int MAX_LINES = (SCREEN_HEIGHT - 100) / LINE_HEIGHT;

std::string tmux_session = "default";
std::string tmux_content;
bool running = true;
bool session_exists = true;

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
        std::string check_cmd = "tmux has-session -t " + tmux_session + " 2>/dev/null";
        if (std::system(check_cmd.c_str()) != 0) {
            session_exists = false;
            running = false;
            break;
        }

        std::string cmd = "tmux capture-pane -t " + tmux_session + " -e -p";
        tmux_content = runCommand(cmd);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Tmux Viewer");
    SetTargetFPS(30);

    Font font = GetFontDefault();

    std::string check_cmd = "tmux has-session -t " + tmux_session + " 2>/dev/null";
    if (std::system(check_cmd.c_str()) != 0) {
        std::string create_cmd = "tmux new-session -d -s " + tmux_session;
        std::system(create_cmd.c_str());
    }

    std::thread update_thread(updateTmuxContent);

    while (!WindowShouldClose() && session_exists) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mousePos = GetMousePosition();
            if (mousePos.y < 100) {
                break;
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        DrawTextEx(font, ("Tmux Session: " + tmux_session).c_str(), {20, 20}, 24, 1, WHITE);
        DrawTextEx(font, "Touch top area to exit", {20, SCREEN_HEIGHT - 40}, 16, 1, GRAY);

        if (!session_exists) {
            DrawTextEx(font, "Session closed - exiting...", {20, 60}, 20, 1, RED);
        } else {
            std::istringstream stream(tmux_content);
            std::string line;
            int line_num = 0;
            int y_offset = 60;

            while (std::getline(stream, line) && line_num < MAX_LINES) {
                if (line.length() > 100) {
                    line = line.substr(0, 100);
                }

                for (char &c : line) {
                    if (c < 32 && c != '\t') c = ' ';
                }

                DrawTextEx(font, line.c_str(), {20, (float)(y_offset + (line_num * LINE_HEIGHT))}, 18, 1, WHITE);
                line_num++;
            }
        }

        EndDrawing();
    }

    running = false;
    update_thread.join();
    CloseWindow();

    return 0;
}