#include "SVAppSimple.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

SVAppSimple::SVAppSimple() : is_running(false) {
    #ifdef WARPING
        scale_factor = 0.65f;  // ADD THIS
    #endif
}

SVAppSimple::~SVAppSimple() {
    stop();
}

bool SVAppSimple::init() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Ultra-Simple 4-Camera Display System" << std::endl;
    std::cout << "NO STITCHING - Direct Camera Feed" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // ========================================
    // STEP 1: Initialize Camera Source
    // ========================================
    std::cout << "[1/3] Initializing camera source..." << std::endl;
    
    camera_source = std::make_shared<MultiCameraSource>();
    camera_source->setFrameSize(cv::Size(1280, 800));
    
    // Initialize without undistortion (faster!)
    if (camera_source->init("", cv::Size(1280, 800), 
                            cv::Size(1280, 800), false) < 0) {
        std::cerr << "ERROR: Failed to initialize cameras" << std::endl;
        return false;
    }
    
    std::cout << "  ✓ Cameras initialized" << std::endl;
    
    // Start camera streams
    if (!camera_source->startStream()) {
        std::cerr << "ERROR: Failed to start camera streams" << std::endl;
        return false;
    }
    
    std::cout << "  ✓ Camera streams started" << std::endl;
    
    // ========================================
    // STEP 2: Wait for Valid Frames
    // ========================================
    std::cout << "\n[2/3] Waiting for camera frames..." << std::endl;
    
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
                std::cout << "  ✓ Received valid frames from all " << NUM_CAMERAS 
                          << " cameras" << std::endl;
                
                // Print frame info
                for (int i = 0; i < NUM_CAMERAS; i++) {
                    std::cout << "    Camera " << i << ": " 
                              << frames[i].gpuFrame.size() << std::endl;
                }
            }
        }
        
        if (!got_frames) {
            std::this_thread::sleep_for(100ms);
            attempts++;
            if (attempts % 10 == 0) {
                std::cout << "  Still waiting for frames... (" << attempts << "/100)" << std::endl;
            }
        }
    }
    
    if (!got_frames) {
        std::cerr << "ERROR: Failed to get valid frames from cameras" << std::endl;
        return false;
    }
    // ========================================
    // STEP 2A: load calibration & warp maps
    // ========================================
    #ifdef WARPING
        std::cout << "\n[3/4] Setting up bird's-eye transformation..." << std::endl;

        if (!loadCalibration("../camparameters")) {
            std::cerr << "ERROR: Failed to load calibration" << std::endl;
            return false;
        }

        if (!setupWarpMaps()) {
            std::cerr << "ERROR: Failed to setup warp maps" << std::endl;
            return false;
        }

        std::cout << "  ✓ Bird's-eye transformation ready" << std::endl;
    #endif

    // ========================================
    // STEP 3: Initialize Renderer (NO STITCHER!)
    // ========================================
    std::cout << "\n[3/3] Initializing 4-camera display renderer..." << std::endl;
    
    renderer = std::make_shared<SVRenderSimple>(1920, 1080);
    
    if (!renderer->init(
        "../models/Dodge Challenger SRT Hellcat 2015.obj",
        "../shaders/carshadervert.glsl",
        "../shaders/carshaderfrag.glsl")) {
        std::cerr << "ERROR: Failed to initialize renderer" << std::endl;
        return false;
    }
    
    std::cout << "  ✓ Renderer ready" << std::endl;
    
    // ========================================
    // Initialization Complete
    // ========================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "✓ System Initialization Complete!" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "\nConfiguration:" << std::endl;
    std::cout << "  Cameras: " << NUM_CAMERAS << std::endl;
    std::cout << "  Input resolution: 1280x800" << std::endl;
    std::cout << "  Output resolution: 1920x1080" << std::endl;
    std::cout << "  Mode: Direct camera feed (NO STITCHING)" << std::endl;
    std::cout << "\nLayout:" << std::endl;
    std::cout << "       [Front]" << std::endl;
    std::cout << "  [Left] [Car] [Right]" << std::endl;
    std::cout << "       [Rear]" << std::endl;
    std::cout << "\nPress ESC or close window to exit\n" << std::endl;
    
    is_running = true;
    return true;
}

void SVAppSimple::run() {
    if (!is_running) {
        std::cerr << "ERROR: System not initialized" << std::endl;
        return;
    }
 

    int frame_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto last_fps_time = start_time;
    
    std::cout << "Starting main loop..." << std::endl;
    #ifdef WARPING
        std::vector<cv::cuda::GpuMat> warped_frames(NUM_CAMERAS);
    #endif
    while (is_running && !renderer->shouldClose()) {
        // Capture frames
        if (!camera_source->capture(frames)) {
            std::cerr << "WARNING: Frame capture failed" << std::endl;
            std::this_thread::sleep_for(1ms);
            continue;
        }
        
        // Validate all frames
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


        #ifdef WARPING
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

            // ========================================
            // Prepare for rendering
            // ========================================
            std::array<cv::cuda::GpuMat, 4> display_frames;
            
            // USE WARPED FRAMES instead of raw frames
            display_frames[0] = warped_frames[0];  // Front (warped)
            display_frames[1] = warped_frames[1];  // Left (warped)
            display_frames[2] = warped_frames[2];  // Rear (warped)
            display_frames[3] = warped_frames[3];  // Right (warped)

            // ========================================
            // Render (your existing code)
            // ========================================
            if (!renderer->render(display_frames)) {
                std::cerr << "ERROR: Rendering failed" << std::endl;
                break;
            }
        
        #else
                
            // Prepare frame array for renderer (just pass GPU frames directly!)
            std::array<cv::cuda::GpuMat, 4> gpu_frames;
            for (int i = 0; i < NUM_CAMERAS; i++) {
                gpu_frames[i] = frames[i].gpuFrame;
            }
            // Render directly - no stitching!
            if (!renderer->render(gpu_frames)) {
                std::cerr << "ERROR: Rendering failed" << std::endl;
                break;
            }
        #endif


        
        // FPS calculation and display
        frame_count++;
        if (frame_count % 30 == 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_fps_time).count();
            
            if (elapsed > 0) {
                float fps = (30.0f * 1000.0f) / elapsed;
                // std::cout << "FPS: " << fps << std::endl;
            }
            
            last_fps_time = now;
        }
        
        // Small sleep to prevent CPU spinning
        std::this_thread::sleep_for(1ms);
    }
    
    std::cout << "\nMain loop exited" << std::endl;
}

void SVAppSimple::stop() {
    is_running = false;
    
    if (camera_source) {
        std::cout << "Stopping camera streams..." << std::endl;
        camera_source->stopStream();
    }
    
    std::cout << "System stopped" << std::endl;
}

#ifdef WARPING
    // ============================================================================
    // NEW FUNCTION 1: Load Calibration from YAML Files
    // ============================================================================
    bool SVAppSimple::loadCalibration(const std::string& folder) {
        K_matrices.resize(NUM_CAMERAS);
        R_matrices.resize(NUM_CAMERAS);
        
        std::cout << "Loading calibration files..." << std::endl;
        
        for (int i = 0; i < NUM_CAMERAS; i++) {
            std::string filename = folder + "/Camparam" + std::to_string(i) + ".yaml";
            
            cv::FileStorage fs(filename, cv::FileStorage::READ);
            if (!fs.isOpened()) {
                std::cerr << "ERROR: Failed to open: " << filename << std::endl;
                std::cerr << "Run: cd camparameters && python3 generate_calibration.py" << std::endl;
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

    // ============================================================================
    // NEW FUNCTION 2: Setup Warp Maps for Bird's-Eye Transformation
    // ============================================================================
    #ifdef WARPING_SPERICAL

        bool SVAppSimple::setupWarpMaps() {
            warp_x_maps.resize(NUM_CAMERAS);
            warp_y_maps.resize(NUM_CAMERAS);
            
            // Create spherical warper with scaled focal length
            auto warper = cv::makePtr<cv::detail::SphericalWarper>(static_cast<float>(scale_factor * focal_length));
            std::cout << "Creating spherical warp maps..." << std::endl;

            // auto warper = cv::makePtr<cv::detail::CylindricalWarper>(static_cast<float>(scale_factor * focal_length));
            // std::cout << "Creating PlaneWarper warp maps..." << std::endl;

            
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
                
                // Build warp maps (pixel coordinate transformation)
                cv::Mat xmap, ymap;
                warper->buildMaps(scaled_input, K_scaled, R_matrices[i], xmap, ymap);
                
                // Upload to GPU for fast processing
                warp_x_maps[i].upload(xmap);
                warp_y_maps[i].upload(ymap);
                
                std::cout << "  ✓ Camera " << i << ": warp maps created" << std::endl;
            }
            
            return true;
        }
    #endif

    #ifdef WARPING_CUSTOM
        bool SVAppSimple::setupWarpMaps() {
        warp_x_maps.resize(NUM_CAMERAS);
        warp_y_maps.resize(NUM_CAMERAS);
        
        std::cout << "Creating bird's-eye warp maps..." << std::endl;
        
        cv::Size input_size(CAMERA_WIDTH, CAMERA_HEIGHT);
        cv::Size scaled_input(input_size.width * scale_factor, 
                            input_size.height * scale_factor);
        
        // Output size for bird's-eye view
        cv::Size output_size = scaled_input;  // Keep same size as scaled input
        
        for (int i = 0; i < NUM_CAMERAS; i++) {
            // ═══════════════════════════════════════════════════════════
            // Define trapezoid region in input (perspective view)
            // This is the ground area we want to see from above
            // ═══════════════════════════════════════════════════════════
            
            std::vector<cv::Point2f> src_pts;
            std::vector<cv::Point2f> dst_pts;
            
            // Adjust these based on camera orientation
            float w = input_size.width;
            float h = input_size.height;
            
            // Source points: trapezoid in perspective view (ground area)
            // These define what part of the camera sees the ground
            src_pts.push_back(cv::Point2f(w * 0.2f, h * 0.6f));  // Top-left
            src_pts.push_back(cv::Point2f(w * 0.8f, h * 0.6f));  // Top-right
            src_pts.push_back(cv::Point2f(w * 1.0f, h * 1.0f));  // Bottom-right
            src_pts.push_back(cv::Point2f(0.0f, h * 1.0f));      // Bottom-left
            
            // Destination points: rectangle in bird's-eye view
            dst_pts.push_back(cv::Point2f(0.0f, 0.0f));
            dst_pts.push_back(cv::Point2f(output_size.width, 0.0f));
            dst_pts.push_back(cv::Point2f(output_size.width, output_size.height));
            dst_pts.push_back(cv::Point2f(0.0f, output_size.height));
            
            // Scale source points for processing scale
            for (auto& pt : src_pts) {
                pt.x *= scale_factor;
                pt.y *= scale_factor;
            }
            
            // Compute homography matrix
            cv::Mat H = cv::getPerspectiveTransform(src_pts, dst_pts);
            
            // Build warp maps
            cv::Mat xmap(output_size, CV_32F);
            cv::Mat ymap(output_size, CV_32F);
            
            for (int y = 0; y < output_size.height; y++) {
                for (int x = 0; x < output_size.width; x++) {
                    // For each output pixel, find where it comes from in input
                    cv::Mat pt_dst = (cv::Mat_<double>(3,1) << x, y, 1.0);
                    cv::Mat pt_src = H.inv() * pt_dst;
                    
                    double w_coord = pt_src.at<double>(2);
                    float src_x = pt_src.at<double>(0) / w_coord;
                    float src_y = pt_src.at<double>(1) / w_coord;
                    
                    xmap.at<float>(y, x) = src_x;
                    ymap.at<float>(y, x) = src_y;
                }
            }
            
            // Upload to GPU
            warp_x_maps[i].upload(xmap);
            warp_y_maps[i].upload(ymap);
            
            std::cout << "  ✓ Camera " << i << ": bird's-eye warp maps created" << std::endl;
        }
        
        return true;
        }
    #endif

    #ifdef WARPING_IPM
        bool SVAppSimple::setupWarpMaps() {
        warp_x_maps.resize(NUM_CAMERAS);
        warp_y_maps.resize(NUM_CAMERAS);
        
        std::cout << "Creating IPM (bird's-eye) warp maps..." << std::endl;
        
        cv::Size scaled_input(CAMERA_WIDTH * scale_factor, 
                            CAMERA_HEIGHT * scale_factor);
        cv::Size output_size = scaled_input;
        
        for (int i = 0; i < NUM_CAMERAS; i++) {
            // Simple inverse perspective mapping
            // Map output rectangle to input trapezoid
            
            float w = scaled_input.width;
            float h = scaled_input.height;
            
            // These control how much "ground" you see
            float vanishing_point_y = h * 0.5f;  // Horizon line
            float bottom_width_ratio = 1.0f;     // Bottom stays full width
            float top_width_ratio = 0.6f;        // Top narrows (perspective)
            
            cv::Mat xmap(output_size, CV_32F);
            cv::Mat ymap(output_size, CV_32F);
            
            for (int y = 0; y < output_size.height; y++) {
                // Interpolate width based on y position
                float t = static_cast<float>(y) / output_size.height;
                float width_ratio = bottom_width_ratio + t * (top_width_ratio - bottom_width_ratio);
                float half_width = w * width_ratio * 0.5f;
                float center_x = w * 0.5f;
                
                // Map y to perspective y
                float src_y = vanishing_point_y + (h - vanishing_point_y) * t;
                
                for (int x = 0; x < output_size.width; x++) {
                    // Map x to perspective x
                    float t_x = static_cast<float>(x) / output_size.width;
                    float src_x = center_x + (t_x - 0.5f) * 2.0f * half_width;
                    
                    xmap.at<float>(y, x) = src_x;
                    ymap.at<float>(y, x) = src_y;
                }
            }
            
            warp_x_maps[i].upload(xmap);
            warp_y_maps[i].upload(ymap);
            
            std::cout << "  ✓ Camera " << i << ": IPM warp maps created" << std::endl;
        }
        
        return true;
        }
    #endif
#endif
