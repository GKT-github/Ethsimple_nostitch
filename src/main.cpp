#include "SVAppSimple.hpp"
#include <iostream>
#include <csignal>
#include <opencv2/cudawarping.hpp>   
#include <opencv2/imgproc.hpp>        

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

        cv::cuda::GpuMat test_in(100, 100, CV_8UC3);
        cv::cuda::GpuMat test_out;
        cv::cuda::GpuMat test_map_x(100, 100, CV_32F);
        cv::cuda::GpuMat test_map_y(100, 100, CV_32F);
        try {
            cv::cuda::remap(test_in, test_out, test_map_x, test_map_y, cv::INTER_LINEAR);
            std::cout << "✓ cv::cuda::remap is working!" << std::endl;
        } catch (const cv::Exception& e) {
            std::cerr << "✗ cv::cuda::remap FAILED: " << e.what() << std::endl;
        }
        
        
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