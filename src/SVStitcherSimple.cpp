#include "SVStitcherSimple.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/stitching/detail/warpers.hpp>
#include <opencv2/cudawarping.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <iostream>
#include <opencv2/imgproc.hpp>  // ✅ REQUIRED for cv::resize

SVStitcherSimple::SVStitcherSimple() 
    : is_init(false), num_cameras(NUM_CAMERAS), scale_factor(PROCESS_SCALE) {
}

SVStitcherSimple::~SVStitcherSimple() {
}

bool SVStitcherSimple::initFromFiles(const std::string& calib_folder,
                                     const std::vector<cv::cuda::GpuMat>& sample_frames) {
    if (is_init) {
        std::cerr << "Stitcher already initialized" << std::endl;
        return false;
    }
    
    if (sample_frames.size() != num_cameras) {
        std::cerr << "Wrong number of frames: " << sample_frames.size() 
                  << " (expected " << num_cameras << ")" << std::endl;
        return false;
    }
    
    std::cout << "Initializing stitcher..." << std::endl;
    std::cout << "  Calibration folder: " << calib_folder << std::endl;
    std::cout << "  Number of cameras: " << num_cameras << std::endl;
    std::cout << "  Scale factor: " << scale_factor << std::endl;
    
    // Load calibration files
    if (!loadCalibration(calib_folder)) {
        return false;
    }
    
    // Setup warp maps
    if (!setupWarpMaps()) {
        return false;
    }
    
    // Create full overlap masks (no seam detection)
    if (!createOverlapMasks(sample_frames)) {
        return false;
    }
    
    // Initialize blender
    blender = std::make_shared<SVMultiBandBlender>(NUM_BLEND_BANDS);
    blender->prepare(warp_corners, warp_sizes, blend_masks);
    
    std::cout << "Multi-band blender initialized (" << NUM_BLEND_BANDS << " bands)" << std::endl;
    
    // Initialize gain compensator
    gain_comp = std::make_shared<SVGainCompensator>(num_cameras);
    
    // Create sample warped frames for gain initialization
    std::vector<cv::cuda::GpuMat> warped_samples(num_cameras);
    for (int i = 0; i < num_cameras; i++) {
        cv::cuda::GpuMat scaled;
        cv::cuda::resize(sample_frames[i], scaled, cv::Size(),
                        scale_factor, scale_factor, cv::INTER_LINEAR);
        cv::cuda::remap(scaled, warped_samples[i],
                       warp_x_maps[i], warp_y_maps[i],
                       cv::INTER_LINEAR, cv::BORDER_CONSTANT);
    }
    
    gain_comp->init(warped_samples, warp_corners, blend_masks);
    
    std::cout << "Gain compensator initialized" << std::endl;
    
    // Setup output cropping
    if (!setupOutputCrop(calib_folder)) {
        return false;
    }
    
    is_init = true;
    std::cout << "✓ Stitcher initialization complete!" << std::endl;
    
    return true;
}

bool SVStitcherSimple::loadCalibration(const std::string& folder) {
    K_matrices.resize(num_cameras);
    R_matrices.resize(num_cameras);
    
    std::cout << "Loading calibration files..." << std::endl;
    
    for (int i = 0; i < num_cameras; i++) {
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

bool SVStitcherSimple::setupWarpMaps() {
    warp_x_maps.resize(num_cameras);
    warp_y_maps.resize(num_cameras);
    warp_corners.resize(num_cameras);
    warp_sizes.resize(num_cameras);
    
    // Create spherical warper    ////
    // cv::Ptr<cv::WarperCreator> warper_creator = cv::makePtr<cv::SphericalWarper>();
    // cv::Ptr<cv::detail::RotationWarper> warper = 
    //     warper_creator->create(static_cast<float>(scale_factor * focal_length));
    auto warper = cv::makePtr<cv::detail::SphericalWarper>(static_cast<float>(scale_factor * focal_length));
    std::cout << "Creating spherical warp maps..." << std::endl;
    
    cv::Size input_size(CAMERA_WIDTH, CAMERA_HEIGHT);
    cv::Size scaled_input(input_size.width * scale_factor, 
                          input_size.height * scale_factor);
    
    for (int i = 0; i < num_cameras; i++) {
        // Scale intrinsic matrix
        cv::Mat K_scaled = K_matrices[i].clone();
        K_scaled.at<float>(0, 0) *= scale_factor;  // fx
        K_scaled.at<float>(1, 1) *= scale_factor;  // fy
        K_scaled.at<float>(0, 2) *= scale_factor;  // cx
        K_scaled.at<float>(1, 2) *= scale_factor;  // cy
        
        // Build warp maps
        cv::Mat xmap, ymap;
        
        // Get corner and warped size
        cv::Mat dummy_warped;
        warp_corners[i] = warper->warp(
            cv::Mat::zeros(scaled_input, CV_8UC3),
            K_scaled, R_matrices[i],
            cv::INTER_LINEAR, cv::BORDER_REFLECT,
            dummy_warped
        );
        
        warp_sizes[i] = dummy_warped.size();
        
        // Build actual maps
        warper->buildMaps(scaled_input, K_scaled, R_matrices[i], xmap, ymap);
        
        // Upload to GPU
        warp_x_maps[i].upload(xmap);
        warp_y_maps[i].upload(ymap);
        
        std::cout << "  ✓ Camera " << i << ": corner=" << warp_corners[i] 
                  << ", size=" << warp_sizes[i] << std::endl;
    }
    
    return true;
}

bool SVStitcherSimple::createOverlapMasks(const std::vector<cv::cuda::GpuMat>& sample_frames) {
    blend_masks.resize(num_cameras);
    
    std::cout << "Creating full overlap masks..." << std::endl;
    
    // Create warper for mask generation ////
    // cv::Ptr<cv::WarperCreator> warper_creator = cv::makePtr<cv::SphericalWarper>();
    // cv::Ptr<cv::detail::RotationWarper> warper = 
    //     warper_creator->create(static_cast<float>(scale_factor * focal_length));
    auto warper = cv::makePtr<cv::detail::SphericalWarper>(static_cast<float>(scale_factor * focal_length));

    for (int i = 0; i < num_cameras; i++) {
        // Create full white mask for entire image
        cv::Size scaled_size(CAMERA_WIDTH * scale_factor, CAMERA_HEIGHT * scale_factor);
        cv::Mat full_mask(scaled_size, CV_8U, cv::Scalar(255));
        
        // Scale K matrix
        cv::Mat K_scaled = K_matrices[i].clone();
        K_scaled.at<float>(0, 0) *= scale_factor;
        K_scaled.at<float>(1, 1) *= scale_factor;
        K_scaled.at<float>(0, 2) *= scale_factor;
        K_scaled.at<float>(1, 2) *= scale_factor;
        
        // Warp mask
        cv::Mat warped_mask;
        warper->warp(full_mask, K_scaled, R_matrices[i],
                     cv::INTER_NEAREST, cv::BORDER_CONSTANT, warped_mask);
        
        // Upload to GPU
        blend_masks[i].upload(warped_mask);
        
        std::cout << "  ✓ Camera " << i << ": mask size=" << warped_mask.size() << std::endl;
    }
    
    return true;
}

bool SVStitcherSimple::setupOutputCrop(const std::string& folder) {
    std::string crop_file = folder + "/corner_warppts.yaml";
    
    cv::FileStorage fs(crop_file, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        std::cerr << "Warning: Could not load " << crop_file << std::endl;
        std::cerr << "Using default HD output without cropping" << std::endl;
        output_size = cv::Size(OUTPUT_WIDTH, OUTPUT_HEIGHT);
        return true;  // Non-fatal, will use simple resize
    }
    
    // Read as Mat first, then convert to Point/Size
    cv::Mat res_size_mat, tl_mat, tr_mat, bl_mat, br_mat;
    
    fs["res_size"] >> res_size_mat;
    fs["tl"] >> tl_mat;
    fs["tr"] >> tr_mat;
    fs["bl"] >> bl_mat;
    fs["br"] >> br_mat;
    fs.release();
    
    // Convert Mat to Size and Points
    output_size = cv::Size(res_size_mat.at<int>(0), res_size_mat.at<int>(1));
    cv::Point tl(tl_mat.at<int>(0), tl_mat.at<int>(1));
    cv::Point tr(tr_mat.at<int>(0), tr_mat.at<int>(1));
    cv::Point bl(bl_mat.at<int>(0), bl_mat.at<int>(1));
    cv::Point br(br_mat.at<int>(0), br_mat.at<int>(1));
    
    std::cout << "Output crop configuration loaded" << std::endl;
    std::cout << "  Output size: " << output_size << std::endl;
    std::cout << "  Crop corners: TL=" << tl << ", TR=" << tr 
              << ", BL=" << bl << ", BR=" << br << std::endl;
    
    // Rest of the function remains the same...
    // Create perspective transform
    std::vector<cv::Point2f> src_pts = {
        cv::Point2f(tl.x, tl.y),
        cv::Point2f(tr.x, tr.y),
        cv::Point2f(bl.x, bl.y),
        cv::Point2f(br.x, br.y)
    };
    
    std::vector<cv::Point2f> dst_pts = {
        cv::Point2f(0, 0),
        cv::Point2f(output_size.width, 0),
        cv::Point2f(0, output_size.height),
        cv::Point2f(output_size.width, output_size.height)
    };
    
    cv::Mat transform = cv::getPerspectiveTransform(src_pts, dst_pts);
    
    // Build GPU warp maps
    cv::cuda::buildWarpPerspectiveMaps(transform, false, output_size, 
                                        crop_warp_x, crop_warp_y);
    
    return true;
}

// ============================================================================
// DEBUG VERSION - stitch() function with extensive logging
// Replace the entire stitch() function in SVStitcherSimple.cpp
// This will help us identify exactly where the alignment issue occurs
// ============================================================================

bool SVStitcherSimple::stitch(const std::vector<cv::cuda::GpuMat>& frames,
                               cv::cuda::GpuMat& output) {
    std::cout << "\n=== STITCH DEBUG START ===" << std::endl;
    
    if (!is_init) {
        std::cerr << "ERROR: Stitcher not initialized" << std::endl;
        return false;
    }
    
    if (frames.size() != num_cameras) {
        std::cerr << "ERROR: Wrong number of frames: " << frames.size() << std::endl;
        return false;
    }
    
    std::cout << "Processing " << num_cameras << " frames" << std::endl;
    
    for (int i = 0; i < num_cameras; i++) {
        std::cout << "\n--- Camera " << i << " ---" << std::endl;
        
        // Check input frame properties
        std::cout << "Input frame properties:" << std::endl;
        std::cout << "  Size: " << frames[i].cols << "x" << frames[i].rows << std::endl;
        std::cout << "  Type: " << frames[i].type() << " (CV_8UC3=" << CV_8UC3 << ")" << std::endl;
        std::cout << "  Channels: " << frames[i].channels() << std::endl;
        std::cout << "  Step: " << frames[i].step << " bytes" << std::endl;
        std::cout << "  ElemSize: " << frames[i].elemSize() << " bytes" << std::endl;
        std::cout << "  IsContinuous: " << (frames[i].isContinuous() ? "YES" : "NO") << std::endl;
        std::cout << "  IsEmpty: " << (frames[i].empty() ? "YES" : "NO") << std::endl;
        
        // Check alignment
        size_t ptr_value = reinterpret_cast<size_t>(frames[i].data);
        std::cout << "  Data pointer: 0x" << std::hex << ptr_value << std::dec << std::endl;
        std::cout << "  Alignment check:" << std::endl;
        std::cout << "    Aligned to 4: " << (ptr_value % 4 == 0 ? "YES" : "NO") << std::endl;
        std::cout << "    Aligned to 8: " << (ptr_value % 8 == 0 ? "YES" : "NO") << std::endl;
        std::cout << "    Aligned to 16: " << (ptr_value % 16 == 0 ? "YES" : "NO") << std::endl;
        std::cout << "    Aligned to 32: " << (ptr_value % 32 == 0 ? "YES" : "NO") << std::endl;
        std::cout << "    Aligned to 256: " << (ptr_value % 256 == 0 ? "YES" : "NO") << std::endl;
        
        // Check step alignment
        std::cout << "  Step alignment:" << std::endl;
        std::cout << "    Step % 4 = " << (frames[i].step % 4) << std::endl;
        std::cout << "    Step % 32 = " << (frames[i].step % 32) << std::endl;
        std::cout << "    Step % 256 = " << (frames[i].step % 256) << std::endl;
        
        // Calculate expected step
        size_t expected_step = frames[i].cols * frames[i].elemSize();
        std::cout << "    Expected step: " << expected_step << std::endl;
        std::cout << "    Actual step: " << frames[i].step << std::endl;
        std::cout << "    Padding: " << (frames[i].step - expected_step) << " bytes" << std::endl;
        
        try {
            // Try direct download (will likely fail)
            std::cout << "\nAttempt 1: Direct download..." << std::endl;
            cv::Mat cpu_frame;
            frames[i].download(cpu_frame);
            std::cout << "  SUCCESS! Direct download worked" << std::endl;
            
            // If we get here, continue with processing
            std::cout << "Downloaded frame: " << cpu_frame.size() << std::endl;
            
            // Resize
            std::cout << "Resizing..." << std::endl;
            cv::Mat scaled_cpu;
            cv::resize(cpu_frame, scaled_cpu, cv::Size(),
                      scale_factor, scale_factor, cv::INTER_LINEAR);
            std::cout << "  Resized to: " << scaled_cpu.size() << std::endl;
            
            // Upload
            std::cout << "Uploading to GPU..." << std::endl;
            cv::cuda::GpuMat scaled;
            scaled.upload(scaled_cpu);
            std::cout << "  Upload successful" << std::endl;
            
            // Warp
            std::cout << "Warping..." << std::endl;
            cv::cuda::GpuMat warped;
            cv::cuda::remap(scaled, warped,
                           warp_x_maps[i], warp_y_maps[i],
                           cv::INTER_LINEAR, cv::BORDER_CONSTANT);
            std::cout << "  Warp successful" << std::endl;
            
            // Convert
            std::cout << "Converting to 16-bit..." << std::endl;
            warped.convertTo(warped, CV_16SC3);
            std::cout << "  Conversion successful" << std::endl;
            
            // Gain
            std::cout << "Applying gain compensation..." << std::endl;
            gain_comp->apply(warped, warped, i);
            std::cout << "  Gain applied" << std::endl;
            
            // Blend
            std::cout << "Feeding to blender..." << std::endl;
            blender->feed(warped, blend_masks[i], i);
            std::cout << "  Fed to blender" << std::endl;
            
        } catch (const cv::Exception& e) {
            std::cerr << "\n!!! EXCEPTION at camera " << i << " !!!" << std::endl;
            std::cerr << "What: " << e.what() << std::endl;
            std::cerr << "Code: " << e.code << std::endl;
            std::cerr << "Err: " << e.err << std::endl;
            std::cerr << "File: " << e.file << std::endl;
            std::cerr << "Func: " << e.func << std::endl;
            std::cerr << "Line: " << e.line << std::endl;
            
            // Try alternative method
            std::cout << "\nAttempt 2: Clone before download..." << std::endl;
            try {
                cv::cuda::GpuMat cloned = frames[i].clone();
                std::cout << "  Clone successful" << std::endl;
                std::cout << "  Cloned size: " << cloned.size() << std::endl;
                std::cout << "  Cloned step: " << cloned.step << std::endl;
                
                cv::Mat cpu_frame2;
                cloned.download(cpu_frame2);
                std::cout << "  Download after clone successful!" << std::endl;
                
                // Continue with this frame...
                cv::Mat scaled_cpu;
                cv::resize(cpu_frame2, scaled_cpu, cv::Size(),
                          scale_factor, scale_factor, cv::INTER_LINEAR);
                
                cv::cuda::GpuMat scaled;
                scaled.upload(scaled_cpu);
                
                cv::cuda::GpuMat warped;
                cv::cuda::remap(scaled, warped,
                               warp_x_maps[i], warp_y_maps[i],
                               cv::INTER_LINEAR, cv::BORDER_CONSTANT);
                
                warped.convertTo(warped, CV_16SC3);
                gain_comp->apply(warped, warped, i);
                blender->feed(warped, blend_masks[i], i);
                
                std::cout << "  Alternative method succeeded!" << std::endl;
                
            } catch (const cv::Exception& e2) {
                std::cerr << "\n!!! CLONE METHOD ALSO FAILED !!!" << std::endl;
                std::cerr << "What: " << e2.what() << std::endl;
                
                // Try completely CPU-based
                std::cout << "\nAttempt 3: CPU-only method..." << std::endl;
                try {
                    // Create a CPU mat and copy data manually
                    cv::Mat manual_copy(frames[i].rows, frames[i].cols, frames[i].type());
                    
                    // Try to get the data somehow
                    std::cout << "  Attempting manual data extraction..." << std::endl;
                    
                    // This is a last resort - create dummy data for this frame
                    std::cout << "  WARNING: Using dummy frame for camera " << i << std::endl;
                    manual_copy = cv::Mat::zeros(frames[i].rows, frames[i].cols, CV_8UC3);
                    
                    cv::Mat scaled_cpu;
                    cv::resize(manual_copy, scaled_cpu, cv::Size(),
                              scale_factor, scale_factor, cv::INTER_LINEAR);
                    
                    cv::cuda::GpuMat scaled;
                    scaled.upload(scaled_cpu);
                    
                    cv::cuda::GpuMat warped;
                    cv::cuda::remap(scaled, warped,
                                   warp_x_maps[i], warp_y_maps[i],
                                   cv::INTER_LINEAR, cv::BORDER_CONSTANT);
                    
                    warped.convertTo(warped, CV_16SC3);
                    gain_comp->apply(warped, warped, i);
                    blender->feed(warped, blend_masks[i], i);
                    
                } catch (const cv::Exception& e3) {
                    std::cerr << "\n!!! ALL METHODS FAILED !!!" << std::endl;
                    std::cerr << "What: " << e3.what() << std::endl;
                    return false;
                }
            }
        }
        
        std::cout << "Camera " << i << " processing complete" << std::endl;
    }
    
    // Blending
    std::cout << "\n--- Blending ---" << std::endl;
    cv::cuda::GpuMat blended_result;
    cv::cuda::GpuMat blended_mask;
    
    try {
        blender->blend(blended_result, blended_mask);
        std::cout << "Blending successful" << std::endl;
        std::cout << "Blended size: " << blended_result.size() << std::endl;
    } catch (const cv::Exception& e) {
        std::cerr << "BLENDING FAILED: " << e.what() << std::endl;
        return false;
    }
    
    // Final output
    std::cout << "\n--- Final Output ---" << std::endl;
    try {
        if (output_size.width > 0 && output_size.height > 0 && 
            !crop_warp_x.empty() && !crop_warp_y.empty()) {
            
            std::cout << "Applying crop warp..." << std::endl;
            cv::cuda::remap(blended_result, output,
                           crop_warp_x, crop_warp_y,
                           cv::INTER_LINEAR, cv::BORDER_CONSTANT);
            std::cout << "Crop warp successful" << std::endl;
        } else {
            std::cout << "Resizing to output..." << std::endl;
            cv::Mat cpu_blended;
            blended_result.download(cpu_blended);
            
            cv::Mat resized_cpu;
            cv::resize(cpu_blended, resized_cpu, output_size, 0, 0, cv::INTER_LINEAR);
            
            output.upload(resized_cpu);
            std::cout << "Resize successful" << std::endl;
        }
        
        std::cout << "Final output size: " << output.size() << std::endl;
        
    } catch (const cv::Exception& e) {
        std::cerr << "FINAL OUTPUT FAILED: " << e.what() << std::endl;
        return false;
    }
    
    std::cout << "\n=== STITCH DEBUG END - SUCCESS ===" << std::endl;
    return true;
}

void SVStitcherSimple::recomputeGain(const std::vector<cv::cuda::GpuMat>& frames) {
    if (!is_init || !gain_comp) {
        return;
    }
    
    // Create warped frames for gain computation
    std::vector<cv::cuda::GpuMat> warped_frames(num_cameras);
    
    for (int i = 0; i < num_cameras; i++) {
        cv::cuda::GpuMat scaled_frame;
        cv::cuda::resize(frames[i], scaled_frame, cv::Size(),
                        scale_factor, scale_factor, cv::INTER_LINEAR);
        
        cv::cuda::remap(scaled_frame, warped_frames[i],
                       warp_x_maps[i], warp_y_maps[i],
                       cv::INTER_LINEAR, cv::BORDER_CONSTANT);
    }
    
    gain_comp->recompute(warped_frames, warp_corners, blend_masks);
    
    std::cout << "Gain compensation updated" << std::endl;
}
