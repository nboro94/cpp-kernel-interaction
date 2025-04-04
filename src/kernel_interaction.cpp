#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <vector>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#endif
#include "kernel_interaction.h"

#ifdef _WIN32
std::string ramUsageInfo;
HWND g_hwnd; // Global variable to store the window handle
std::vector<double> cpuUsageHistory;  // Changed from ramUsageHistory
const int MAX_HISTORY_POINTS = 60;    // Store 1 minute of data (with 1-second updates)
const int GRAPH_HEIGHT = 100;         // Height of the graph in pixels
ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
const int SMOOTHING_WINDOW = 5;  // Number of samples for moving average
std::vector<double> smoothedHistory;  // Store smoothed values

const COLORREF WINDOW_BG_COLOR = RGB(240, 240, 240);  // Light gray background
const COLORREF GRAPH_LINE_COLOR = RGB(0, 120, 215);   // Windows blue
const COLORREF TEXT_COLOR = RGB(60, 60, 60);          // Dark gray text
const COLORREF GRID_COLOR = RGB(200, 200, 200);       // Light gray grid
const int PADDING = 20;                               // Padding around elements
const int GRAPH_WIDTH = 400;                          // Wider graph
const HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
#endif

void print_process_info() {
    #ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
    std::cout << "Current Process ID: " << pid << std::endl;

    DWORD ppid = GetCurrentProcessId(); // No direct parent process ID function on Windows
    std::cout << "Parent Process ID: " << ppid << std::endl;
    #else
    pid_t pid = getpid();
    std::cout << "Current Process ID: " << pid << std::endl;

    // Using syscall to get the parent process ID
    pid_t ppid = syscall(SYS_getppid);
    std::cout << "Parent Process ID: " << ppid << std::endl;
    #endif
}

void print_system_info() {
    #ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    std::cout << "Number of processors: " << sysinfo.dwNumberOfProcessors << std::endl;
    #else
    long num_processors = sysconf(_SC_NPROCESSORS_ONLN);
    std::cout << "Number of processors: " << num_processors << std::endl;
    #endif
}

bool display_system_info() {
    try {
        print_process_info();
        print_system_info();
        return true;
    } catch (...) {
        return false;
    }
}

#ifdef _WIN32
DWORD get_process_with_highest_memory_usage() {
    DWORD processes[1024], count, cbNeeded;
    if (!EnumProcesses(processes, sizeof(processes), &cbNeeded)) {
        return 0;
    }

    count = cbNeeded / sizeof(DWORD);
    DWORD highestMemoryProcess = 0;
    SIZE_T highestMemoryUsage = 0;

    for (unsigned int i = 0; i < count; i++) {
        if (processes[i] == 0) continue;

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processes[i]);
        if (!hProcess) continue;

        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)) && pmc.WorkingSetSize > highestMemoryUsage) {
            highestMemoryUsage = pmc.WorkingSetSize;
            highestMemoryProcess = processes[i];
        }
        CloseHandle(hProcess);
    }

    return highestMemoryProcess;
}

double calculate_moving_average(const std::vector<double>& values, size_t end, int window) {
    double sum = 0;
    int count = 0;
    
    for (int i = 0; i < window && end >= i; i++) {
        sum += values[end - i];
        count++;
    }
    
    return count > 0 ? sum / count : 0;
}

double get_cpu_usage() {
    static ULARGE_INTEGER lastIdleTime = {0};
    static ULARGE_INTEGER lastKernelTime = {0};
    static ULARGE_INTEGER lastUserTime = {0};

    FILETIME idleTime, kernelTime, userTime;
    GetSystemTimes((LPFILETIME)&idleTime, (LPFILETIME)&kernelTime, (LPFILETIME)&userTime);

    ULONGLONG idle = ((ULONGLONG)idleTime.dwHighDateTime << 32) | idleTime.dwLowDateTime;
    ULONGLONG kernel = ((ULONGLONG)kernelTime.dwHighDateTime << 32) | kernelTime.dwLowDateTime;
    ULONGLONG user = ((ULONGLONG)userTime.dwHighDateTime << 32) | userTime.dwLowDateTime;

    if (lastIdleTime.QuadPart == 0) {
        lastIdleTime.QuadPart = idle;
        lastKernelTime.QuadPart = kernel;
        lastUserTime.QuadPart = user;
        return 0.0;
    }

    ULONGLONG idleDiff = idle - lastIdleTime.QuadPart;
    ULONGLONG kernelDiff = kernel - lastKernelTime.QuadPart;
    ULONGLONG userDiff = user - lastUserTime.QuadPart;
    ULONGLONG totalDiff = kernelDiff + userDiff;

    lastIdleTime.QuadPart = idle;
    lastKernelTime.QuadPart = kernel;
    lastUserTime.QuadPart = user;

    return totalDiff > 0 ? ((totalDiff - idleDiff) * 100.0) / totalDiff : 0.0;
}

void update_ram_usage() {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    DWORDLONG totalPhysMem = memInfo.ullTotalPhys;
    DWORDLONG physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;

    DWORD highestMemoryProcess = get_process_with_highest_memory_usage();
    char processName[MAX_PATH] = "<unknown>";
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, highestMemoryProcess);
    if (hProcess) {
        HMODULE hMod;
        DWORD cbNeeded;
        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
            GetModuleBaseName(hProcess, hMod, processName, sizeof(processName) / sizeof(char));
        }
        CloseHandle(hProcess);
    }

    double totalPhysMemGB = totalPhysMem / (1024.0 * 1024 * 1024);
    double physMemUsedGB = physMemUsed / (1024.0 * 1024 * 1024);
    
    // Add current RAM usage percentage to history
    double usagePercentage = (physMemUsedGB / totalPhysMemGB) * 100.0;
    cpuUsageHistory.push_back(usagePercentage);
    
    // Keep only MAX_HISTORY_POINTS most recent values
    if (cpuUsageHistory.size() > MAX_HISTORY_POINTS) {
        cpuUsageHistory.erase(cpuUsageHistory.begin());
    }

    double cpuUsage = get_cpu_usage();
    cpuUsageHistory.push_back(cpuUsage);
    
    if (cpuUsageHistory.size() > MAX_HISTORY_POINTS) {
        cpuUsageHistory.erase(cpuUsageHistory.begin());
    }

    // Format with fixed precision for display
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "CPU Usage: " << cpuUsage << "%, ";
    ss << "Total Physical Memory: " << totalPhysMemGB << " GB, ";
    ss << "Used Physical Memory: " << physMemUsedGB << " GB";
    ramUsageInfo = ss.str();
}

void ram_usage_thread() {
    while (true) {
        update_ram_usage();
        InvalidateRect(g_hwnd, NULL, TRUE); // Request a redraw of the window
        std::this_thread::sleep_for(std::chrono::seconds(1)); // Update every 5 seconds
    }
}
#endif

void display_ram_usage() {
    while (true) {
        #ifdef _WIN32
        update_ram_usage();
        InvalidateRect(g_hwnd, NULL, TRUE); // Request a redraw of the window
        #else
        struct sysinfo memInfo;
        sysinfo(&memInfo);
        long long totalPhysMem = memInfo.totalram;
        totalPhysMem *= memInfo.mem_unit;
        long long physMemUsed = memInfo.totalram - memInfo.freeram;
        physMemUsed *= memInfo.mem_unit;

        double totalPhysMemGB = totalPhysMem / (1024.0 * 1024 * 1024);
        double physMemUsedGB = physMemUsed / (1024.0 * 1024 * 1024);

        std::cout << "\rTotal Physical Memory: " << totalPhysMemGB << " GB, Used Physical Memory: " << physMemUsedGB << " GB" << std::flush;
        #endif

        std::this_thread::sleep_for(std::chrono::seconds(1)); // Update every 5 seconds
    }
}

#ifdef _WIN32
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            InvalidateRect(hwnd, NULL, TRUE);  // Force redraw when window is resized
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Set up DC
            SetBkMode(hdc, TRANSPARENT);
            HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
            SetTextColor(hdc, TEXT_COLOR);

            // Get window dimensions
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            
            // Fill background
            HBRUSH bgBrush = CreateSolidBrush(WINDOW_BG_COLOR);
            FillRect(hdc, &clientRect, bgBrush);
            DeleteObject(bgBrush);
            
            // Draw RAM usage text at the top
            TextOut(hdc, PADDING, PADDING, ramUsageInfo.c_str(), ramUsageInfo.length());

            // Create pens
            HPEN graphPen = CreatePen(PS_SOLID, 2, GRAPH_LINE_COLOR);
            HPEN gridPen = CreatePen(PS_DOT, 1, GRID_COLOR);
            HPEN oldPen = (HPEN)SelectObject(hdc, gridPen);
            
            // Draw graph area
            int graphTop = 60;
            
            // Draw grid lines
            for (int i = 0; i <= 100; i += 20) {
                int y = graphTop + GRAPH_HEIGHT - (i * GRAPH_HEIGHT / 100);
                MoveToEx(hdc, PADDING, y, NULL);
                LineTo(hdc, PADDING + GRAPH_WIDTH, y);
            }
            
            for (int i = 0; i <= GRAPH_WIDTH; i += 50) {
                MoveToEx(hdc, PADDING + i, graphTop, NULL);
                LineTo(hdc, PADDING + i, graphTop + GRAPH_HEIGHT);
            }

            // Draw axes
            SelectObject(hdc, graphPen);
            MoveToEx(hdc, PADDING, graphTop, NULL);
            LineTo(hdc, PADDING, graphTop + GRAPH_HEIGHT);
            LineTo(hdc, PADDING + GRAPH_WIDTH, graphTop + GRAPH_HEIGHT);

            // Draw graph if we have data
            if (!cpuUsageHistory.empty()) {
                float pointSpacing = (float)GRAPH_WIDTH / MAX_HISTORY_POINTS;
                
                // Calculate smoothed values
                smoothedHistory.clear();
                for (size_t i = 0; i < cpuUsageHistory.size(); i++) {
                    double smoothedValue = calculate_moving_average(cpuUsageHistory, i, SMOOTHING_WINDOW);
                    smoothedHistory.push_back(smoothedValue);
                }
                
                // Draw using smoothed values
                MoveToEx(hdc, PADDING, graphTop + GRAPH_HEIGHT - (smoothedHistory[0] * GRAPH_HEIGHT / 100), NULL);
                
                for (size_t i = 1; i < smoothedHistory.size(); i++) {
                    int x = PADDING + (i * pointSpacing);
                    int y = graphTop + GRAPH_HEIGHT - (smoothedHistory[i] * GRAPH_HEIGHT / 100);
                    LineTo(hdc, x, y);
                }
            }

            // Draw percentage labels
            SetTextAlign(hdc, TA_RIGHT);
            for (int i = 0; i <= 100; i += 20) {
                std::string label = std::to_string(i) + "%";
                TextOut(hdc, PADDING - 5, graphTop + GRAPH_HEIGHT - (i * GRAPH_HEIGHT / 100) - 8, 
                        label.c_str(), label.length());
            }

            // Draw time labels
            SetTextAlign(hdc, TA_CENTER);
            for (int i = 0; i <= MAX_HISTORY_POINTS; i += 10) {
                std::string label = std::to_string(i) + "s";
                TextOut(hdc, PADDING + (i * GRAPH_WIDTH / MAX_HISTORY_POINTS), 
                        graphTop + GRAPH_HEIGHT + 5, label.c_str(), label.length());
            }

            // Cleanup
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldFont);
            DeleteObject(graphPen);
            DeleteObject(gridPen);
            
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SETCURSOR: {
            // Get the hit test area
            WORD hitTest = LOWORD(lParam);
            
            // Change cursor based on hit test area
            switch (hitTest) {
                case HTLEFT:
                case HTRIGHT:
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    return TRUE;
                case HTTOP:
                case HTBOTTOM:
                    SetCursor(LoadCursor(NULL, IDC_SIZENS));
                    return TRUE;
                case HTTOPLEFT:
                case HTBOTTOMRIGHT:
                    SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
                    return TRUE;
                case HTTOPRIGHT:
                case HTBOTTOMLEFT:
                    SetCursor(LoadCursor(NULL, IDC_SIZENESW));
                    return TRUE;
                case HTCLIENT:
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                default:
                    return DefWindowProc(hwnd, uMsg, wParam, lParam);
            }
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void create_main_window(HINSTANCE hInstance, int nCmdShow) {
    const char CLASS_NAME[] = "Sample Window Class";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);  // Add default arrow cursor
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);  // Add window background
    wc.style = CS_HREDRAW | CS_VREDRAW;  // Redraw on horizontal or vertical resize

    RegisterClass(&wc);

    g_hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        "CPU Usage Monitor",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 
        600, 300,  // Adjusted window size
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (g_hwnd == NULL) {
        return;
    }

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);  // Force an initial paint

    // Start the RAM usage update thread
    std::thread ramUsageThread(ram_usage_thread);
    ramUsageThread.detach();

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
#endif