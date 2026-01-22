// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

// Global storage for command line args (for tcApp access)
static int g_argc = 0;
static char** g_argv = nullptr;

int getArgCount() { return g_argc; }
char** getArgValues() { return g_argv; }

int main(int argc, char* argv[]) {
    g_argc = argc;
    g_argv = argv;

    bool hasArgs = argc > 1;
    bool forceGui = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--gui") {
            forceGui = true;
            break;
        }
    }

    if (hasArgs && !forceGui) {
        tc::HeadlessSettings settings;
        settings.setFps(60.0f);
        return tc::runHeadlessApp<tcApp>(settings);
    }

    tc::WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("GvEncoder");
    return tc::runApp<tcApp>(settings);
}
