#include "SVAppSimple.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

SVAppSimple::SVAppSimple() : is_running(false) {
    #if defined(WARPING) || defined(RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY)
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
    // CUSTOM HOMOGRAPHY WITH MANUAL CALIBRATION
    // ========================================
    #ifdef RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY
        std::cout << "\n[3/4] Setting up custom homography with manual points..." << std::endl;
        
        // Try to load existing calibration points
        if (!loadCalibrationPoints("../camparameters")) {
            // If no saved points, perform manual calibration
            std::cout << "  No saved calibration found. Starting manual calibration..." << std::endl;
            
            if (!selectManualCalibrationPoints(frames)) {
                std::cerr << "ERROR: Failed to select calibration points" << std::endl;
                return false;
            }
            
            // Save the calibration points for future use
            if (!saveCalibrationPoints("../camparameters")) {
                std::cerr << "WARNING: Failed to save calibration points" << std::endl;
            }
        }
        
        // Build warp maps from the calibration points
        if (!setupCustomHomographyMaps()) {
            std::cerr << "ERROR: Failed to setup custom homography maps" << std::endl;
            return false;
        }
        
        std::cout << "  ✓ Custom homography ready" << std::endl;
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
#endif // end WARPING


#ifdef RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY
// ============================================================================
// CUSTOM HOMOGRAPHY WITH MANUAL POINT SELECTION
// ============================================================================

bool SVAppSimple::setupCustomHomographyMaps() {
    warp_x_maps.resize(NUM_CAMERAS);
    warp_y_maps.resize(NUM_CAMERAS);
    
    std::cout << "Creating custom homography warp maps from manual points..." << std::endl;
    
    cv::Size input_size(CAMERA_WIDTH, CAMERA_HEIGHT);
    cv::Size scaled_input(input_size.width * scale_factor, 
                        input_size.height * scale_factor);
    
    // Output size for bird's-eye view
    cv::Size output_size = scaled_input;  // Keep same size as scaled input
    
    for (int i = 0; i < NUM_CAMERAS; i++) {
        // Get source and destination points for this camera
        if (manual_src_points.empty() || manual_src_points[i].size() != 4 ||
            manual_dst_points.empty() || manual_dst_points[i].size() != 4) {
            std::cerr << "ERROR: Invalid calibration points for camera " << i << std::endl;
            return false;
        }
        
        std::vector<cv::Point2f> src_pts = manual_src_points[i];
        std::vector<cv::Point2f> dst_pts = manual_dst_points[i];
        
        // Scale source points for processing scale
        for (auto& pt : src_pts) {
            pt.x *= scale_factor;
            pt.y *= scale_factor;
        }
        
        // Compute homography matrix H: maps destination -> source
        // H transforms a point in the bird's-eye view back to the perspective view
        cv::Mat H = cv::getPerspectiveTransform(dst_pts, src_pts);
        
        std::cout << "  Camera " << i << " homography matrix:" << std::endl;
        std::cout << H << std::endl;
        
        // Build warp maps using the homography
        cv::Mat xmap(output_size, CV_32F);
        cv::Mat ymap(output_size, CV_32F);
        
        for (int y = 0; y < output_size.height; y++) {
            for (int x = 0; x < output_size.width; x++) {
                // For each output pixel in bird's-eye view, find where it comes from in input
                cv::Mat pt_dst = (cv::Mat_<double>(3,1) << x, y, 1.0);
                cv::Mat pt_src = H * pt_dst;  // Apply homography
                
                double w_coord = pt_src.at<double>(2);
                if (w_coord > 1e-6) {
                    float src_x = static_cast<float>(pt_src.at<double>(0) / w_coord);
                    float src_y = static_cast<float>(pt_src.at<double>(1) / w_coord);
                    
                    xmap.at<float>(y, x) = src_x;
                    ymap.at<float>(y, x) = src_y;
                } else {
                    // Invalid point (w = 0), mark as out of bounds
                    xmap.at<float>(y, x) = -1.0f;
                    ymap.at<float>(y, x) = -1.0f;
                }
            }
        }
        
        // Upload to GPU
        warp_x_maps[i].upload(xmap);
        warp_y_maps[i].upload(ymap);
        
        std::cout << "  ✓ Camera " << i << ": custom homography warp maps created" << std::endl;
    }
    
    return true;
}

    // ============================================================================
    // INTERACTIVE CALIBRATION - Requires GTK support in OpenCV
    // ============================================================================
    #ifdef CUSTOM_HOMOGRAPHY_INTERACTIVE
    bool SVAppSimple::selectManualCalibrationPoints(const std::array<Frame, NUM_CAMERAS>& sample_frames) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "INTERACTIVE CALIBRATION: Select 4 Points per Camera" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Instructions:" << std::endl;
        std::cout << "  - Click on 4 points in each camera image (trapezoid shape)" << std::endl;
        std::cout << "  - Order: Top-Left → Top-Right → Bottom-Right → Bottom-Left" << std::endl;
        std::cout << "  - Points should outline the ground visible in the camera" << std::endl;
        std::cout << "  - Press 'SPACE' to confirm 4 points and move to next camera" << std::endl;
        std::cout << "  - Press 'R' to reset current camera" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
        manual_src_points.resize(NUM_CAMERAS);
        manual_dst_points.resize(NUM_CAMERAS);
        
        // Create destination points (output bird's-eye view rectangle)
        cv::Size scaled_input(CAMERA_WIDTH * scale_factor, CAMERA_HEIGHT * scale_factor);
        manual_dst_points[0] = {
            cv::Point2f(0.0f, 0.0f),
            cv::Point2f(static_cast<float>(scaled_input.width), 0.0f),
            cv::Point2f(static_cast<float>(scaled_input.width), static_cast<float>(scaled_input.height)),
            cv::Point2f(0.0f, static_cast<float>(scaled_input.height))
        };
        // All cameras use same destination rectangle
        for (int i = 1; i < NUM_CAMERAS; i++) {
            manual_dst_points[i] = manual_dst_points[0];
        }
        
        for (int cam = 0; cam < NUM_CAMERAS; cam++) {
            std::cout << "Camera " << cam << ": Select 4 points..." << std::endl;
            
            // Download frame to CPU for display
            cv::Mat cpu_frame;
            sample_frames[cam].gpuFrame.download(cpu_frame);
            
            // Create window and display image
            std::string window_name = "Camera " + std::to_string(cam) + " - Click 4 Points";
            cv::namedWindow(window_name, cv::WINDOW_AUTOSIZE);
            cv::imshow(window_name, cpu_frame);
            
            // Temporary storage for points during selection
            std::vector<cv::Point2f> temp_points;
            
            // Set mouse callback - capture temp_points by reference
            cv::setMouseCallback(window_name, [](int event, int x, int y, int flags, void* userdata) {
                auto* pThis = static_cast<std::vector<cv::Point2f>*>(userdata);
                if (event == cv::EVENT_LBUTTONDOWN) {
                    pThis->push_back(cv::Point2f(static_cast<float>(x), static_cast<float>(y)));
                    std::cout << "  Point " << pThis->size() << ": (" << x << ", " << y << ")" << std::endl;
                }
            }, &temp_points);
            
            // Wait for user to select 4 points
            while (temp_points.size() < 4) {
                int key = cv::waitKey(0);
                
                if (key == 'r' || key == 'R') {
                    // Reset
                    temp_points.clear();
                    std::cout << "Points reset. Select 4 points again..." << std::endl;
                } else if (key == ' ' && temp_points.size() == 4) {
                    // Confirm
                    break;
                }
            }
            
            if (temp_points.size() != 4) {
                std::cerr << "ERROR: Did not get exactly 4 points for camera " << cam << std::endl;
                cv::destroyWindow(window_name);
                return false;
            }
            
            // Store the selected points
            manual_src_points[cam] = temp_points;
            
            std::cout << "  ✓ Camera " << cam << " calibration complete:" << std::endl;
            for (int j = 0; j < 4; j++) {
                std::cout << "    Point " << j << ": (" << manual_src_points[cam][j].x 
                          << ", " << manual_src_points[cam][j].y << ")" << std::endl;
            }
            
            cv::destroyWindow(window_name);
        }
        
        std::cout << "\n✓ Interactive calibration complete!" << std::endl;
        return true;
    }
    #endif

    // ============================================================================
    // NON-INTERACTIVE CALIBRATION - Uses Default Points (No GTK Required)
    // ============================================================================
    #ifdef CUSTOM_HOMOGRAPHY_NONINTERACTIVE
    bool SVAppSimple::selectManualCalibrationPoints(const std::array<Frame, NUM_CAMERAS>& sample_frames) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "NON-INTERACTIVE CALIBRATION: Using Default Points" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Note: OpenCV compiled without GTK support or interactive mode disabled" << std::endl;
        std::cout << "Using default calibration points instead" << std::endl;
        std::cout << "To enable interactive calibration:" << std::endl;
        std::cout << "  1. Install: sudo apt-get install libgtk2.0-dev pkg-config" << std::endl;
        std::cout << "  2. Rebuild OpenCV with GTK support" << std::endl;
        std::cout << "  3. Change header: #define CUSTOM_HOMOGRAPHY_INTERACTIVE" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
        manual_src_points.resize(NUM_CAMERAS);
        manual_dst_points.resize(NUM_CAMERAS);
        
        // Create destination points (output bird's-eye view rectangle)
        cv::Size scaled_input(CAMERA_WIDTH * scale_factor, CAMERA_HEIGHT * scale_factor);
        manual_dst_points[0] = {
            cv::Point2f(0.0f, 0.0f),
            cv::Point2f(static_cast<float>(scaled_input.width), 0.0f),
            cv::Point2f(static_cast<float>(scaled_input.width), static_cast<float>(scaled_input.height)),
            cv::Point2f(0.0f, static_cast<float>(scaled_input.height))
        };
        // All cameras use same destination rectangle
        for (int i = 1; i < NUM_CAMERAS; i++) {
            manual_dst_points[i] = manual_dst_points[0];
        }
        
        // Define default calibration points for each camera
        // These are typical trapezoids that map ground plane to bird's-eye view
        // ADJUST THESE VALUES BASED ON YOUR CAMERA SETUP
        
        // Camera 0: Front
        manual_src_points[0] = {
            cv::Point2f(256.0f, 360.0f),   // Top-left
            cv::Point2f(1024.0f, 360.0f),  // Top-right
            cv::Point2f(1280.0f, 800.0f),  // Bottom-right
            cv::Point2f(0.0f, 800.0f)      // Bottom-left
        };
        
        // Camera 1: Left (90° left view)
        manual_src_points[1] = {
            cv::Point2f(200.0f, 400.0f),   // Top-left
            cv::Point2f(850.0f, 300.0f),   // Top-right
            cv::Point2f(1280.0f, 800.0f),  // Bottom-right
            cv::Point2f(0.0f, 800.0f)      // Bottom-left
        };
        
        // Camera 2: Rear (opposite of front)
        manual_src_points[2] = {
            cv::Point2f(256.0f, 360.0f),   // Top-left
            cv::Point2f(1024.0f, 360.0f),  // Top-right
            cv::Point2f(1280.0f, 800.0f),  // Bottom-right
            cv::Point2f(0.0f, 800.0f)      // Bottom-left
        };
        
        // Camera 3: Right (90° right view)
        manual_src_points[3] = {
            cv::Point2f(430.0f, 300.0f),   // Top-left
            cv::Point2f(1080.0f, 400.0f),  // Top-right
            cv::Point2f(1280.0f, 800.0f),  // Bottom-right
            cv::Point2f(0.0f, 800.0f)      // Bottom-left
        };
        
        std::cout << "Using default calibration points:" << std::endl;
        for (int cam = 0; cam < NUM_CAMERAS; cam++) {
            std::cout << "  Camera " << cam << ":" << std::endl;
            for (int j = 0; j < 4; j++) {
                std::cout << "    Point " << j << ": (" << manual_src_points[cam][j].x 
                          << ", " << manual_src_points[cam][j].y << ")" << std::endl;
            }
        }
        
        std::cout << "\n✓ Default calibration points loaded!" << std::endl;
        std::cout << "To refine calibration:" << std::endl;
        std::cout << "  1. Edit '../camparameters/custom_homography_points.yaml'" << std::endl;
        std::cout << "  2. Or install GTK and use interactive calibration" << std::endl;
        return true;
    }
    #endif

    bool SVAppSimple::saveCalibrationPoints(const std::string& folder) {
    std::cout << "Saving calibration points to YAML..." << std::endl;
    
    std::string filename = folder + "/custom_homography_points.yaml";
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    
    if (!fs.isOpened()) {
        std::cerr << "ERROR: Failed to open file for writing: " << filename << std::endl;
        return false;
    }
    
    fs << "num_cameras" << NUM_CAMERAS;
    fs << "scale_factor" << scale_factor;
    
    for (int i = 0; i < NUM_CAMERAS; i++) {
        std::string src_key = "camera_" + std::to_string(i) + "_src_points";
        std::string dst_key = "camera_" + std::to_string(i) + "_dst_points";
        
        fs << src_key << manual_src_points[i];
        fs << dst_key << manual_dst_points[i];
    }
    
    fs.release();
    std::cout << "  ✓ Saved to: " << filename << std::endl;
    return true;
}

bool SVAppSimple::loadCalibrationPoints(const std::string& folder) {
    std::string filename = folder + "/custom_homography_points.yaml";
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    
    if (!fs.isOpened()) {
        std::cout << "Note: Calibration file not found. Will need manual calibration." << std::endl;
        return false;
    }
    
    int saved_cameras = 0;
    fs["num_cameras"] >> saved_cameras;
    
    if (saved_cameras != NUM_CAMERAS) {
        std::cerr << "ERROR: Saved calibration has " << saved_cameras << " cameras, expected " 
                    << NUM_CAMERAS << std::endl;
        return false;
    }
    
    manual_src_points.resize(NUM_CAMERAS);
    manual_dst_points.resize(NUM_CAMERAS);
    
    for (int i = 0; i < NUM_CAMERAS; i++) {
        std::string src_key = "camera_" + std::to_string(i) + "_src_points";
        std::string dst_key = "camera_" + std::to_string(i) + "_dst_points";
        
        fs[src_key] >> manual_src_points[i];
        fs[dst_key] >> manual_dst_points[i];
    }
    
    fs.release();
    std::cout << "  ✓ Loaded calibration points from: " << filename << std::endl;
    return true;
}
#endif


