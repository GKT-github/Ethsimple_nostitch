#include "SVAppSimple.hpp"
#include <iostream>
#include <csignal>

volatile bool g_running = true;

void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received." << std::endl;
    g_running = false;
}

int main(int argc, char** argv) {
    std::cout << "========================================" << std::endl;
    std::cout << "Ultra-Simple 4-Camera Display System" << std::endl;
    std::cout << "Direct Feed - No Stitching" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Setup signal handler
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Create application
    SVAppSimple app;
    
    // Initialize (no calibration folder needed!)
    std::cout << "\n--- Initialization Phase ---" << std::endl;
    if (!app.init()) {
        std::cerr << "\nERROR: Failed to initialize application" << std::endl;
        return -1;
    }
    
    std::cout << "\n--- Running... (Press ESC to stop) ---" << std::endl;
    
    // Run main loop
    app.run();
    
    // Cleanup
    std::cout << "\n--- Shutting down ---" << std::endl;
    app.stop();
    
    std::cout << "Goodbye!" << std::endl;
    return 0;
}
