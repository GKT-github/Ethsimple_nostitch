// STEP 1: Individual Camera Bird's-Eye View Transformation
// Shows each camera warped to bird's-eye perspective separately
// NO stitching/blending yet - just perspective correction

#include "SVAppSimple.hpp"
#include <opencv2/cudawarping.hpp>
#include <opencv2/stitching/detail/warpers.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

class SVAppWarped {
public:
    SVAppWarped();
    ~SVAppWarped();
    
    bool init();
    void run();
    void stop();
    
private:
    std::shared_ptr<MultiCameraSource> camera_source;
    std::shared_ptr<SVRenderSimple> renderer;
    std::array<CamFrame, 4> frames;
    bool is_running;
    
    // Warping data (per camera)
    std::vector<cv::cuda::GpuMat> warp_x_maps;  // X coordinate maps
    std::vector<cv::cuda::GpuMat> warp_y_maps;  // Y coordinate maps
    std::vector<cv::Mat> K_matrices;            // Intrinsic matrices
    std::vector<cv::Mat> R_matrices;            // Rotation matrices
    float focal_length;
    float scale_factor;
    
    bool loadCalibration(const std::string& folder);
    bool setupWarpMaps();
};

SVAppWarped::SVAppWarped() 
    : is_running(false), scale_factor(0.65f) {
}

SVAppWarped::~SVAppWarped() {
    stop();
}

bool SVAppWarped::loadCalibration(const std::string& folder) {
    K_matrices.resize(NUM_CAMERAS);
    R_matrices.resize(NUM_CAMERAS);
    
    std::cout << "Loading calibration files..." << std::endl;
    
    for (int i = 0; i < NUM_CAMERAS; i++) {
        std::string filename = folder + "/Camparam" + std::to_string(i) + ".yaml";
        
        cv::FileStorage fs(filename, cv::FileStorage::READ);
        if (!fs.isOpened()) {
            std::cerr << "ERROR: Failed to open: " << filename << std::endl;
            return false;
        }
        
        fs["FocalLength"] >> focal_length;
        fs["Intrisic"] >> K_matrices[i];
        fs["Rotation"] >> R_matrices[i];
        
        fs.release();
        
        std::cout << "  ✓ Camera " << i << ": " << filename << std::endl;
    }
    
    std::cout << "  Focal length: " << focal_length << " pixels" << std::endl;
    
    return true;
}

bool SVAppWarped::setupWarpMaps() {
    warp_x_maps.resize(NUM_CAMERAS);
    warp_y_maps.resize(NUM_CAMERAS);
    
    // Create spherical warper
    auto warper = cv::makePtr<cv::detail::SphericalWarper>(
        static_cast<float>(scale_factor * focal_length)
    );
    
    std::cout << "Creating spherical warp maps..." << std::endl;
    
    cv::Size input_size(CAMERA_WIDTH, CAMERA_HEIGHT);
    cv::Size scaled_input(input_size.width * scale_factor, 
                          input_size.height * scale_factor);
    
    for (int i = 0; i < NUM_CAMERAS; i++) {
        // Scale intrinsic matrix
        cv::Mat K_scaled = K_matrices[i].clone();
        K_scaled.at<float>(0, 0) *= scale_factor;  // fx
        K_scaled.at<float>(1, 1) *= scale_factor;  // fy
        K_scaled.at<float>(0, 2) *= scale_factor;  // cx
        K_scaled.at<float>(1, 2) *= scale_factor;  // cy
        
        // Build warp maps
        cv::Mat xmap, ymap;
        warper->buildMaps(scaled_input, K_scaled, R_matrices[i], xmap, ymap);
        
        // Upload to GPU
        warp_x_maps[i].upload(xmap);
        warp_y_maps[i].upload(ymap);
        
        std::cout << "  ✓ Camera " << i << ": warp maps created" << std::endl;
    }
    
    return true;
}

bool SVAppWarped::init() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "STEP 1: Individual Bird's-Eye Views" << std::endl;
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
    // STEP 3: Load Calibration and Setup Warp Maps
    // ========================================
    std::cout << "\n[3/4] Setting up bird's-eye transformation..." << std::endl;
    
    // Load calibration
    if (!loadCalibration("../camparameters")) {
        std::cerr << "ERROR: Failed to load calibration" << std::endl;
        std::cerr << "Make sure Camparam0-3.yaml files are in ../camparameters/" << std::endl;
        return false;
    }
    
    // Setup warp maps
    if (!setupWarpMaps()) {
        std::cerr << "ERROR: Failed to setup warp maps" << std::endl;
        return false;
    }
    
    std::cout << "  ✓ Bird's-eye transformation ready" << std::endl;
    
    // ========================================
    // STEP 4: Initialize Renderer
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
    std::cout << "Mode: Individual Bird's-Eye Views (No Stitching)" << std::endl;
    std::cout << "\nLayout:" << std::endl;
    std::cout << "       [Front - Warped]" << std::endl;
    std::cout << "  [Left] [3D Car] [Right]" << std::endl;
    std::cout << "       [Rear - Warped]" << std::endl;
    std::cout << "\nEach camera shows its bird's-eye perspective\n" << std::endl;
    
    is_running = true;
    return true;
}

void SVAppWarped::run() {
    if (!is_running) {
        std::cerr << "ERROR: System not initialized" << std::endl;
        return;
    }
    
    int frame_count = 0;
    auto last_fps_time = std::chrono::steady_clock::now();
    
    std::cout << "Starting main loop..." << std::endl;
    
    // Storage for warped frames
    std::vector<cv::cuda::GpuMat> warped_frames(NUM_CAMERAS);
    
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
        
        // ================================================
        // WARP EACH CAMERA TO BIRD'S-EYE VIEW
        // ================================================
        for (int i = 0; i < NUM_CAMERAS; i++) {
            // 1. Resize to processing scale
            cv::cuda::GpuMat scaled;
            cv::cuda::resize(frames[i].gpuFrame, scaled, cv::Size(),
                            scale_factor, scale_factor, cv::INTER_LINEAR);
            
            // 2. Apply spherical warp (bird's-eye transformation)
            cv::cuda::remap(scaled, warped_frames[i],
                           warp_x_maps[i], warp_y_maps[i],
                           cv::INTER_LINEAR, cv::BORDER_CONSTANT);
        }
        
        // ================================================
        // PREPARE FOR DISPLAY (5-panel layout)
        // ================================================
        std::array<cv::cuda::GpuMat, 4> display_frames;
        
        // Camera 0: Front (top position)
        display_frames[0] = warped_frames[0];
        
        // Camera 1: Left (left position)
        display_frames[1] = warped_frames[1];
        
        // Camera 2: Rear (bottom position)
        display_frames[2] = warped_frames[2];
        
        // Camera 3: Right (right position)
        display_frames[3] = warped_frames[3];
        
        // ================================================
        // RENDER
        // ================================================
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
                std::cout << "FPS: " << fps << " (warped views)" << std::endl;
            }
            
            last_fps_time = now;
        }
        
        std::this_thread::sleep_for(1ms);
    }
    
    std::cout << "\nMain loop exited" << std::endl;
}

void SVAppWarped::stop() {
    is_running = false;
    
    if (camera_source) {
        std::cout << "Stopping camera streams..." << std::endl;
        camera_source->stopStream();
    }
    
    std::cout << "System stopped" << std::endl;
}
