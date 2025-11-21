#include "SVStitcherAuto.hpp"
#include <opencv2/cudawarping.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudaarithm.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>

SVStitcherAuto::SVStitcherAuto() 
    : is_init(false)
    , num_cameras(NUM_CAMERAS)
    , scale_factor(PROCESS_SCALE)
    , frame_count(0)
    , use_gain_compensation(false) {  // Set to false to disable gain compensation
}

SVStitcherAuto::~SVStitcherAuto() {
}

bool SVStitcherAuto::init(const std::vector<cv::cuda::GpuMat>& sample_frames,const std::vector<cv::cuda::GpuMat>& warp_x_maps,const std::vector<cv::cuda::GpuMat>& warp_y_maps,float scale) {
    if (is_init) {
        std::cerr << "Stitcher already initialized" << std::endl;
        return false;
    }
    
    if (sample_frames.size() != num_cameras) {
        std::cerr << "Wrong number of frames: " << sample_frames.size() << std::endl;
        return false;
    }
    
    scale_factor = scale;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "SIMPLE ALPHA BLENDING STITCHER" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Mode: Fast alpha blending with linear interpolation" << std::endl;
    std::cout << "Gain compensation: " << (use_gain_compensation ? "ENABLED" : "DISABLED") << std::endl;
    std::cout << "========================================" << std::endl;
    
    // ============================================
    // STEP 1: Compute warped frame properties
    // ============================================
    std::cout << "\n[1/3] Computing warp geometry..." << std::endl;
    
    warp_corners.resize(num_cameras);
    warp_sizes.resize(num_cameras);
    
    for (int i = 0; i < num_cameras; i++) {
        // sample_frames are already scaled and warped from SVAppSimple
        // Note: sample_frames[i] are already at scale_factor (0.65) and warped
        warp_sizes[i] = sample_frames[i].size();
        
        std::cout << "  Camera " << i << ": size=" << warp_sizes[i] << std::endl;
    }
    
    // ============================================
    // STEP 2: Compute output ROI and corner positions
    // ============================================
    std::cout << "\n[2/3] Computing output canvas and positions..." << std::endl;
    
    // Output size: Rotated surround view with cameras at canvas corners
    // Canvas: 640×800 with each camera 640×400, rotated and positioned at corners
    int canvas_w = 640;
    int canvas_h = 800;
    output_roi = cv::Rect(0, 0, canvas_w, canvas_h);
    output_size = output_roi.size();
    
    std::cout << "  Output stitched view size: " << output_size << " (ROTATED CORNER LAYOUT)" << std::endl;
    
    // Rotated corner layout (640×800) - each camera 640×400
    // Canvas corners with camera 0,0 placement:
    //
    //  TL (0,0)              TR (640,0)
    //     ↓                      ↑
    //   Front Cam0           Right Cam3
    //  (0,0)→(640,400)    (640,80)→(240,720)
    //     |                  |
    //     |__________________|
    //     |                  |
    //  Left Cam1          Rear Cam2
    //  (0,720)→(400,80)   (640,800)→(0,400)
    //     ↓                  ↑
    //  BL (0,800)          BR (640,800)
    //
    // Blending occurs along 4 diagonal lines through corners:
    // - TL diagonal: top-left to center
    // - TR diagonal: top-right to center
    // - BL diagonal: bottom-left to center
    // - BR diagonal: bottom-right to center
    
    // Camera 0 (Front): Top half, (0,0) anchor
    warp_corners[0] = cv::Point(0, 0);
    
    // Camera 1 (Left): Bottom-left, rotated 90°, (0,720) anchor
    warp_corners[1] = cv::Point(0, 720);
    
    // Camera 2 (Rear): Bottom, rotated 180°, (640,800) anchor
    warp_corners[2] = cv::Point(640, 800);
    
    // Camera 3 (Right): Top-right, rotated 90°, (640,80) anchor
    warp_corners[3] = cv::Point(640, 80);
    
    for (int i = 0; i < num_cameras; i++) {
        std::cout << "  Camera " << i << ": position=" << warp_corners[i] << " (corner anchor)" << std::endl;
    }
    
    std::cout << "  Canvas: 640x800 with rotated cameras at corners" << std::endl;
    std::cout << "  Blend zones: 40px perpendicular to diagonal lines" << std::endl;
    
    // ============================================
    // STEP 3: Create simple alpha masks
    // ============================================
    std::cout << "\n[3/3] Creating alpha blend masks..." << std::endl;
    
    if (!createOverlapMasks(sample_frames)) {
        return false;
    }
    
    // ============================================
    // STEP 4: Initialize simple blender
    // ============================================
    blender = std::make_shared<SVBlender>();
    // Initialize blender with the computed output ROI
    blender->prepare(output_roi);
    
    std::cout << "  ✓ Simple alpha blender initialized" << std::endl;
    
    // ============================================
    // STEP 5: Optional gain compensation
    // ============================================
    if (use_gain_compensation) {
        gain_comp = std::make_shared<SVGainCompensator>(num_cameras);
        
        // Warp sample frames for gain initialization
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
        std::cout << "  ✓ Gain compensator initialized" << std::endl;
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "✓ STITCHER READY!" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    is_init = true;
    return true;
}

bool SVStitcherAuto::createOverlapMasks(const std::vector<cv::cuda::GpuMat>& sample_frames) {
    blend_masks.resize(num_cameras);
    
    std::cout << "Creating ROTATED CORNER blend masks with diagonal corner blending..." << std::endl;
    
    int fade_dist = 40;  // Perpendicular distance from diagonal lines for fade zone
    
    // All cameras are 640×400 (full width, half height)
    std::vector<cv::Size> target_sizes = {
        cv::Size(640, 400),  // Camera 0 (Front): (0,0) anchor, no rotation
        cv::Size(640, 400),  // Camera 1 (Left): (0,720) anchor, rotated 90°
        cv::Size(640, 400),  // Camera 2 (Rear): (640,800) anchor, rotated 180°
        cv::Size(640, 400)   // Camera 3 (Right): (640,80) anchor, rotated 90°
    };
    
    // Canvas corners for diagonal blending:
    // TL = (0, 0), TR = (640, 0), BL = (0, 800), BR = (640, 800), CENTER = (320, 400)
    // 
    // Diagonal lines through corners:
    // TL diagonal: from (0,0) to (320,400) → slope = 400/320 = 1.25
    // TR diagonal: from (640,0) to (320,400) → slope = 400/-320 = -1.25
    // BL diagonal: from (0,800) to (320,400) → slope = -400/320 = -1.25
    // BR diagonal: from (640,800) to (320,400) → slope = -400/-320 = 1.25
    //
    // These form two lines: y = 1.25x and y = -1.25x + 800
    
    const float diag_slope = 1.25f;
    const float diag_normalizer = std::sqrt(diag_slope * diag_slope + 1.0f);  // ~1.601
    
    for (int i = 0; i < num_cameras; i++) {
        cv::Size target = target_sizes[i];
        cv::Mat mask(target.height, target.width, CV_8U);
        
        int w = target.width;   // 640
        int h = target.height;  // 400
        
        // Canvas position of this camera's origin
        cv::Point cam_origin = warp_corners[i];
        
        // Create diagonal blend mask
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                // Convert camera-local coordinates to canvas coordinates
                float canvas_x = x + cam_origin.x;
                float canvas_y = y + cam_origin.y;
                
                // Calculate distance to both diagonal lines
                // Line 1: y = 1.25x → distance = |y - 1.25x| / sqrt(1.25^2 + 1)
                float dist_to_line1 = std::abs(canvas_y - diag_slope * canvas_x) / diag_normalizer;
                
                // Line 2: y = -1.25x + 800 → distance = |y + 1.25x - 800| / sqrt(1.25^2 + 1)
                float dist_to_line2 = std::abs(canvas_y + diag_slope * canvas_x - 800.0f) / diag_normalizer;
                
                // Take minimum distance to either diagonal
                float min_dist = std::min(dist_to_line1, dist_to_line2);
                
                // Calculate alpha based on distance to diagonals
                float alpha;
                if (min_dist < fade_dist) {
                    // Linear fade within blend zone
                    alpha = min_dist / fade_dist;
                } else {
                    // Full opacity outside blend zone
                    alpha = 1.0f;
                }
                
                // Smooth transition with ease-in-out curve: 3t^2 - 2t^3
                alpha = alpha * alpha * (3.0f - 2.0f * alpha);
                
                mask.at<uchar>(y, x) = (uchar)(255 * alpha);
            }
        }
        
        blend_masks[i].upload(mask);
        
        // Store the target size for later resizing
        warp_sizes[i] = target;
        
        std::cout << "  Camera " << i << ": Diagonal blend mask " << target 
                  << " at origin " << cam_origin
                  << " with " << fade_dist << "px fade zones" << std::endl;
        
        // Show representative alpha values
        std::cout << "    Corner alpha: TL=" << (int)mask.at<uchar>(0, 0) 
                  << " BR=" << (int)mask.at<uchar>(h-1, w-1) << std::endl;
        std::cout << "    Center alpha: " << (int)mask.at<uchar>(h/2, w/2) << std::endl;
    }
    
    return true;
}

cv::Rect SVStitcherAuto::computeStitchROI(const std::vector<cv::Point>& corners,
                                          const std::vector<cv::Size>& sizes) {
    // Fixed output size: 640×800 diagonal X-pattern surround view
    // This is scaled to fit in the right 50% of the split-screen display
    
    return cv::Rect(0, 0, 640, 800);
}

bool SVStitcherAuto::stitch(const std::vector<cv::cuda::GpuMat>& raw_frames,
                            const std::vector<cv::cuda::GpuMat>& warped_frames,
                            cv::cuda::GpuMat& output) {
    if (!is_init) {
        std::cerr << "Stitcher not initialized" << std::endl;
        return false;
    }
    
    if (warped_frames.size() != num_cameras) {
        std::cerr << "Wrong number of frames" << std::endl;
        return false;
    }
    
    // Validate all frames
    for (int i = 0; i < num_cameras; i++) {
        if (warped_frames[i].empty()) {
            std::cerr << "Empty frame for camera " << i << std::endl;
            return false;
        }
    }
    
    // ================================================
    // SIMPLE ALPHA BLENDING PIPELINE
    // ================================================
    
    std::vector<cv::cuda::GpuMat> frames_to_blend(num_cameras);
    
    for (int i = 0; i < num_cameras; i++) {
        // Validate frame size matches expected blend mask size
        if (warped_frames[i].size() != blend_masks[i].size()) {
            std::cerr << "WARNING: Frame " << i << " size " << warped_frames[i].size() 
                      << " doesn't match mask size " << blend_masks[i].size() 
                      << ". Resizing frame..." << std::endl;
            
            // Resize to match mask size
            cv::cuda::GpuMat resized;
            cv::cuda::resize(warped_frames[i], resized, blend_masks[i].size(), 
                            0, 0, cv::INTER_LINEAR);
            
            // Convert to 16-bit for blending
            cv::cuda::GpuMat frame_16s;
            resized.convertTo(frame_16s, CV_16SC3);
            
            if (use_gain_compensation && gain_comp) {
                gain_comp->apply(frame_16s, frames_to_blend[i], i);
            } else {
                frames_to_blend[i] = frame_16s;
            }
        } else {
            // Convert to 16-bit for blending (prevents overflow)
            cv::cuda::GpuMat frame_16s;
            warped_frames[i].convertTo(frame_16s, CV_16SC3);
            
            // Optional: Apply gain compensation
            if (use_gain_compensation && gain_comp) {
                gain_comp->apply(frame_16s, frames_to_blend[i], i);
            } else {
                frames_to_blend[i] = frame_16s;
            }
        }
        
        // Feed to simple blender with alpha mask
        try {
            blender->feed(frames_to_blend[i], blend_masks[i], warp_corners[i]);
        } catch (const cv::Exception& e) {
            std::cerr << "ERROR in blender->feed(): " << e.what() << std::endl;
            return false;
        }
    }
    
    std::cout << "Blending..." << std::endl;
    
    // Get blended result
    cv::cuda::GpuMat blended_result;
    cv::cuda::GpuMat blended_mask;
    cv::cuda::Stream stream;
    
    try {
        blender->blend(blended_result, blended_mask, stream);
    } catch (const cv::Exception& e) {
        std::cerr << "ERROR in blender->blend(): " << e.what() << std::endl;
        return false;
    }
    
    // Convert back to 8-bit
    if (blended_result.type() == CV_16SC3) {
        cv::cuda::GpuMat result_8bit;
        blended_result.convertTo(result_8bit, CV_8UC3);
        output = result_8bit;
    } else {
        output = blended_result;
    }
    
    std::cout << "✓ Stitched output ready: " << output.size() << std::endl;
    
    // Optional: Periodic gain update
    if (use_gain_compensation) {
        frame_count++;
        if (frame_count % GAIN_UPDATE_INTERVAL == 0) {
            recomputeGain(warped_frames);
        }
    }
    
    return true;
}

void SVStitcherAuto::recomputeGain(const std::vector<cv::cuda::GpuMat>& warped_frames) {
    if (!is_init || !gain_comp || !use_gain_compensation) {
        return;
    }
    
    gain_comp->recompute(warped_frames, warp_corners, blend_masks);
    std::cout << "Gain compensation updated (frame " << frame_count << ")" << std::endl;
}