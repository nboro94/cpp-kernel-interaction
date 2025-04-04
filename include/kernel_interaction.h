#ifndef KERNEL_INTERACTION_H
#define KERNEL_INTERACTION_H

#include <string>
#include <windows.h> // Include the Windows API header

// Function to get the current working directory
std::string getCurrentWorkingDirectory();

// Function to demonstrate a simple system call
void demonstrateSystemCall();

// Function to display system information
bool display_system_info();

// Function to continuously display RAM usage
void display_ram_usage();

// Function to create and run the main window
void create_main_window(HINSTANCE hInstance, int nCmdShow);

#endif // KERNEL_INTERACTION_H