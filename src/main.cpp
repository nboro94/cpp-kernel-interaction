#include <iostream>
#include "kernel_interaction.h"
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    std::cout << "Demonstrating basic interactions with the kernel..." << std::endl;

    // Example of a kernel interaction function
    if (!display_system_info()) {
        std::cerr << "Failed to display system information." << std::endl;
        return 1;
    }

    // Create and run the main window
    create_main_window(hInstance, nCmdShow);

    return 0;
}