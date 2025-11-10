// Modified SVAppSimple for Bird's-Eye View Stitching
// Replace your current SVAppSimple.cpp with this version

#include "SVAppSimple.hpp"
#include "SVStitcherSimple.hpp"  // ← ADD THIS
#include <iostream>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

// Add stitcher as member variable (update SVAppSimple.hpp too)
class SVAppStitched {
public:
    SVAppStitched();
    ~SVAppStitched();
    
    bool init();
    void run();
    void stop();
    
private:
    std::shared_ptr<MultiCameraSource> camera_source;
    std::shared_ptr<SVStitcherSimple> stitcher;  // ← Stitcher instead of direct renderer
    std::shared_ptr<SVRenderSimple> renderer;
    std::array<CamFrame, 4> frames;
    bool is_running;
};

SVAppStitched::SVAppStitched() : is_running(false) {
}

SVAppStitched::~SVAppStitched() {
    stop();
}

bool SVAppStitched::init() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Bird's-Eye View Stitching System" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // ========================================
    // STEP 1: Initialize Camera Source
    // ========================================
    std::cout << "[1/4] Initializing camera source..." << std::endl;
    
    camera_source = std::make_shared<MultiCameraSource>();
    camera_source->setFrameSize(cv::Size(1280, 800));
    
    if (camera_source->init("", cv::Size(1280, 800), 
                            cv::Size(1280, 800), false) < 0) {
        std::cerr << "ERROR: Failed to initialize cameras" << std::endl;
        return false;
    }
    
    if (!camera_source->startStream()) {
        std::cerr << "ERROR: Failed to start camera streams" << std::endl;
        return false;
    }
    
    std::cout << "  ✓ Cameras initialized" << std::endl;
    
    // ========================================
    // STEP 2: Wait for Valid Frames
    // ========================================
    std::cout << "\n[2/4] Waiting for camera frames..." << std::endl;
    
    int attempts = 0;
    bool got_frames = false;
    
    while (attempts < 100 && !got_frames) {
        if (camera_source->capture(frames)) {
            bool all_valid = true;
            for (int i = 0; i < NUM_CAMERAS; i++) {
                if (frames[i].gpuFrame.empty()) {
                    all_valid = false;
                    break;
                }
            }
            
            if (all_valid) {
                got_frames = true;
                std::cout << "  ✓ Received valid frames" << std::endl;
            }
        }
        
        if (!got_frames) {
            std::this_thread::sleep_for(100ms);
            attempts++;
        }
    }
    
    if (!got_frames) {
        std::cerr << "ERROR: Failed to get valid frames" << std::endl;
        return false;
    }
    
    // ========================================
    // STEP 3: Initialize Stitcher
    // ========================================
    std::cout << "\n[3/4] Initializing stitcher..." << std::endl;
    
    stitcher = std::make_shared<SVStitcherSimple>();
    
    // Prepare sample frames
    std::vector<cv::cuda::GpuMat> sample_frames;
    for (int i = 0; i < NUM_CAMERAS; i++) {
        sample_frames.push_back(frames[i].gpuFrame);
    }
    
    // Initialize with calibration folder
    if (!stitcher->initFromFiles("../camparameters", sample_frames)) {
        std::cerr << "ERROR: Failed to initialize stitcher" << std::endl;
        std::cerr << "Make sure Camparam0-3.yaml files are in ../camparameters/" << std::endl;
        return false;
    }
    
    std::cout << "  ✓ Stitcher initialized" << std::endl;
    
    // ========================================
    // STEP 4: Initialize Renderer (for displaying stitched output)
    // ========================================
    std::cout << "\n[4/4] Initializing display renderer..." << std::endl;
    
    renderer = std::make_shared<SVRenderSimple>(1920, 1080);
    
    if (!renderer->init(
        "../models/Dodge Challenger SRT Hellcat 2015.obj",
        "../shaders/carshadervert.glsl",
        "../shaders/carshaderfrag.glsl")) {
        std::cerr << "ERROR: Failed to initialize renderer" << std::endl;
        return false;
    }
    
    std::cout << "  ✓ Renderer ready" << std::endl;
    
    std::cout << "\n✓ System Initialization Complete!" << std::endl;
    std::cout << "Mode: Bird's-Eye View Stitching\n" << std::endl;
    
    is_running = true;
    return true;
}

void SVAppStitched::run() {
    if (!is_running) {
        std::cerr << "ERROR: System not initialized" << std::endl;
        return;
    }
    
    int frame_count = 0;
    auto last_fps_time = std::chrono::steady_clock::now();
    
    std::cout << "Starting main loop..." << std::endl;
    
    while (is_running && !renderer->shouldClose()) {
        // Capture frames
        if (!camera_source->capture(frames)) {
            std::this_thread::sleep_for(1ms);
            continue;
        }
        
        // Validate frames
        bool all_valid = true;
        for (int i = 0; i < NUM_CAMERAS; i++) {
            if (frames[i].gpuFrame.empty()) {
                all_valid = false;
                break;
            }
        }
        
        if (!all_valid) {
            std::this_thread::sleep_for(1ms);
            continue;
        }
        
        // Prepare frame array for stitcher
        std::vector<cv::cuda::GpuMat> gpu_frames;
        for (int i = 0; i < NUM_CAMERAS; i++) {
            gpu_frames.push_back(frames[i].gpuFrame);
        }
        
        // Stitch frames into bird's-eye view
        cv::cuda::GpuMat stitched_output;
        if (!stitcher->stitch(gpu_frames, stitched_output)) {
            std::cerr << "ERROR: Stitching failed" << std::endl;
            continue;
        }
        
        // Display the stitched bird's-eye view (fullscreen)
        std::array<cv::cuda::GpuMat, 4> display_frames;
        display_frames[0] = stitched_output;  // Show stitched view in first position
        // Other positions can be empty or show individual cameras
        
        if (!renderer->render(display_frames)) {
            std::cerr << "ERROR: Rendering failed" << std::endl;
            break;
        }
        
        // FPS calculation
        frame_count++;
        if (frame_count % 30 == 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_fps_time).count();
            
            if (elapsed > 0) {
                float fps = (30.0f * 1000.0f) / elapsed;
                std::cout << "FPS: " << fps << std::endl;
            }
            
            last_fps_time = now;
        }
        
        std::this_thread::sleep_for(1ms);
    }
    
    std::cout << "\nMain loop exited" << std::endl;
}

void SVAppStitched::stop() {
    is_running = false;
    
    if (camera_source) {
        std::cout << "Stopping camera streams..." << std::endl;
        camera_source->stopStream();
    }
    
    std::cout << "System stopped" << std::endl;
}
