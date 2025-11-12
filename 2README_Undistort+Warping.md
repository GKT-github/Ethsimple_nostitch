FIX 2: Add Fisheye Undistortion (CRITICAL)
The curved lines indicate you NEED to undistort the fisheye first. Here's the complete 2-step solution:
Replace your setupWarpMaps() with this:
cppbool SVAppSimple::setupWarpMaps() {
    warp_x_maps.resize(NUM_CAMERAS);
    warp_y_maps.resize(NUM_CAMERAS);
    undistort_x_maps.resize(NUM_CAMERAS);  // NEW
    undistort_y_maps.resize(NUM_CAMERAS);  // NEW
    
    std::cout << "Creating fisheye undistortion + bird's-eye warp maps..." << std::endl;
    
    cv::Size input_size(CAMERA_WIDTH, CAMERA_HEIGHT);
    cv::Size scaled_input(input_size.width * scale_factor, 
                          input_size.height * scale_factor);
    
    // Fisheye distortion coefficients (adjust these!)
    // Start with moderate fisheye distortion
    cv::Mat distCoeffs = (cv::Mat_<double>(4,1) << -0.15, 0.03, 0.0, 0.0);
    
    for (int i = 0; i < NUM_CAMERAS; i++) {
        // ═══════════════════════════════════════════════════════════
        // STEP 1: Create fisheye undistortion maps
        // ═══════════════════════════════════════════════════════════
        cv::Mat K = K_matrices[i];
        cv::Mat K_new = K.clone();
        
        // Scale for processing
        cv::Mat K_scaled = K * scale_factor;
        K_scaled.at<double>(2,2) = 1.0;  // Don't scale homogeneous coordinate
        
        cv::Mat K_new_scaled = K_new * scale_factor;
        K_new_scaled.at<double>(2,2) = 1.0;
        
        cv::Mat undist_x, undist_y;
        cv::fisheye::initUndistortRectifyMap(
            K_scaled, distCoeffs, 
            cv::Mat::eye(3, 3, CV_64F),  // No rotation
            K_new_scaled, 
            scaled_input, 
            CV_32F, 
            undist_x, undist_y);
        
        undistort_x_maps[i].upload(undist_x);
        undistort_y_maps[i].upload(undist_y);
        
        std::cout << "  ✓ Camera " << i << ": fisheye undistortion maps created" << std::endl;
        
        // ═══════════════════════════════════════════════════════════
        // STEP 2: Create bird's-eye perspective transform
        // ═══════════════════════════════════════════════════════════
        float w = scaled_input.width;
        float h = scaled_input.height;
        
        // Source points (trapezoid on ground in UNDISTORTED image)
        std::vector<cv::Point2f> src_pts = {
            cv::Point2f(w * 0.20f, h * 0.40f),  // Top-left
            cv::Point2f(w * 0.80f, h * 0.40f),  // Top-right
            cv::Point2f(w * 1.00f, h * 1.00f),  // Bottom-right
            cv::Point2f(0.00f, h * 1.00f)       // Bottom-left
        };
        
        // Destination points (rectangle in bird's-eye view)
        std::vector<cv::Point2f> dst_pts = {
            cv::Point2f(0, 0),
            cv::Point2f(w, 0),
            cv::Point2f(w, h),
            cv::Point2f(0, h)
        };
        
        cv::Mat H = cv::getPerspectiveTransform(src_pts, dst_pts);
        
        // Build bird's-eye warp maps
        cv::Mat xmap(scaled_input, CV_32F);
        cv::Mat ymap(scaled_input, CV_32F);
        
        for (int y = 0; y < scaled_input.height; y++) {
            for (int x = 0; x < scaled_input.width; x++) {
                cv::Mat pt_dst = (cv::Mat_<double>(3,1) << x, y, 1.0);
                cv::Mat pt_src = H.inv() * pt_dst;
                
                double w_coord = pt_src.at<double>(2);
                if (fabs(w_coord) > 1e-6) {  // Check for valid homogeneous coordinate
                    float src_x = static_cast<float>(pt_src.at<double>(0) / w_coord);
                    float src_y = static_cast<float>(pt_src.at<double>(1) / w_coord);
                    
                    xmap.at<float>(y, x) = src_x;
                    ymap.at<float>(y, x) = src_y;
                } else {
                    xmap.at<float>(y, x) = -1;  // Invalid
                    ymap.at<float>(y, x) = -1;
                }
            }
        }
        
        warp_x_maps[i].upload(xmap);
        warp_y_maps[i].upload(ymap);
        
        std::cout << "  ✓ Camera " << i << ": bird's-eye transform created" << std::endl;
    }
    
    return true;
}
Add to SVAppSimple.hpp:
cpp#ifdef WARPING
    std::vector<cv::cuda::GpuMat> warp_x_maps;
    std::vector<cv::cuda::GpuMat> warp_y_maps;
    std::vector<cv::cuda::GpuMat> undistort_x_maps;  // ADD
    std::vector<cv::cuda::GpuMat> undistort_y_maps;  // ADD
    // ... rest
#endif
Modify run() to apply BOTH transformations:
cpp#ifdef WARPING
    for (int i = 0; i < NUM_CAMERAS; i++) {
        // 1. Resize
        cv::cuda::GpuMat scaled;
        cv::cuda::resize(frames[i].gpuFrame, scaled, cv::Size(),
                        scale_factor, scale_factor, cv::INTER_LINEAR);
        
        // 2. Undistort fisheye
        cv::cuda::GpuMat undistorted;
        cv::cuda::remap(scaled, undistorted,
                       undistort_x_maps[i], undistort_y_maps[i],
                       cv::INTER_LINEAR, cv::BORDER_CONSTANT);
        
        // 3. Apply bird's-eye transformation
        cv::cuda::remap(undistorted, warped_frames[i],
                       warp_x_maps[i], warp_y_maps[i],
                       cv::INTER_LINEAR, cv::BORDER_CONSTANT);
    }
#endif

FIX 3: Tune Distortion Coefficients
The fisheye distortion coefficients control how much barrel/pincushion correction is applied.
Start with these values and adjust:
cpp// Mild fisheye (try this first)
cv::Mat distCoeffs = (cv::Mat_<double>(4,1) << -0.10, 0.02, 0.0, 0.0);

// Moderate fisheye (if mild isn't enough)
cv::Mat distCoeffs = (cv::Mat_<double>(4,1) << -0.15, 0.03, 0.0, 0.0);

// Strong fisheye (if you have very curved lines)
cv::Mat distCoeffs = (cv::Mat_<double>(4,1) << -0.20, 0.05, 0.0, 0.0);
```

**How to tune:**
- **k1 (first value):** Controls barrel distortion
  - Negative = barrel (lines curve outward) → typical for fisheye
  - More negative = more correction
- **k2 (second value):** Higher-order correction
  - Usually small positive value

---

## 📊 **Expected Improvement**

### **Before (Current):**
```
[Front - Some curves visible]
  Large black    [Car]    Large black
  regions                 regions
[Rear - Some curves]
```

### **After Fixes:**
```
[Front - Straight lines ✓]
  Minimal black  [Car]  Minimal black
  regions               regions
[Rear - Straight lines ✓]
Key improvements:

✅ Straight lines (fisheye corrected)
✅ Less black regions (better source points)
✅ More aggressive top-down view
✅ Ground plane more visible


🎯 Recommended Tuning Process
Step 1: Test with undistortion only
Temporarily disable bird's-eye to see just fisheye correction:
cpp// In run(), comment out the bird's-eye remap:
cv::cuda::remap(scaled, undistorted, ...);
warped_frames[i] = undistorted;  // Just show undistorted
// cv::cuda::remap(undistorted, warped_frames[i], ...);  // COMMENTED
See if lines become straighter. If not, adjust distortion coefficients.
Step 2: Enable bird's-eye transform
Once undistortion looks good, uncomment the bird's-eye remap.
Step 3: Tune source points per camera
Each camera may need different source points:
cpp// Example: Different points for each camera
if (i == 0) {  // Front
    src_pts = {...};
} else if (i == 1) {  // Left
    src_pts = {...};
} // etc.

💡 Quick Parameter Reference
Parameter               What It Does                       Adjust To
k1 = -0.15              Fisheye correction strengthMore    negative = more correction
src_pts top_y = 0.40    How much ground to show            Lower = more ground (0.30-0.50)
src_pts bottom edges    Width of view                      0.0-1.0 = full width
scale_factor = 0.65     Processing speed/quality           0.5=fast, 0.8=quality

✅ Summary

