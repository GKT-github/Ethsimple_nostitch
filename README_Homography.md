Great question! Let me explain the warping calculation and alternatives.

## 📍 **Where Warp Maps Are Calculated**

### Location: `SVAppSimple.cpp` in `setupWarpMaps()` function

```cpp
bool SVAppSimple::setupWarpMaps() {
    warp_x_maps.resize(NUM_CAMERAS);
    warp_y_maps.resize(NUM_CAMERAS);
    
    // ═══════════════════════════════════════════════════════════
    // THIS IS THE KEY LINE - Creates the spherical warper
    // ═══════════════════════════════════════════════════════════
    auto warper = cv::makePtr<cv::detail::SphericalWarper>(
        static_cast<float>(scale_factor * focal_length)
    );
    
    // ... loop through cameras ...
    for (int i = 0; i < NUM_CAMERAS; i++) {
        cv::Mat K_scaled = K_matrices[i].clone();
        K_scaled.at<float>(0, 0) *= scale_factor;  // fx
        K_scaled.at<float>(1, 1) *= scale_factor;  // fy
        K_scaled.at<float>(0, 2) *= scale_factor;  // cx
        K_scaled.at<float>(1, 2) *= scale_factor;  // cy
        
        // ═══════════════════════════════════════════════════════════
        // THIS BUILDS THE ACTUAL WARP MAPS
        // ═══════════════════════════════════════════════════════════
        cv::Mat xmap, ymap;
        warper->buildMaps(scaled_input, K_scaled, R_matrices[i], xmap, ymap);
        
        warp_x_maps[i].upload(xmap);
        warp_y_maps[i].upload(ymap);
    }
}
```

---

## 🌐 **What Spherical Warping Does**

### The Math:

**Input:** Flat camera image with pixel (u, v)

**Step 1: Unproject to 3D**
```
X = (u - cx) / fx
Y = (v - cy) / fy
Z = 1
Ray = [X, Y, Z]
```

**Step 2: Apply Camera Rotation**
```
Ray_world = R × Ray  ← Uses your rotation matrix
```

**Step 3: Project onto Sphere**
```
θ = atan2(Ray_world.x, Ray_world.z)  ← Azimuth
φ = atan2(Ray_world.y, norm(x,z))    ← Elevation
```

**Step 4: Map to Output**
```
x_out = focal_length × θ + center_x
y_out = focal_length × φ + center_y
```

**Problem:** This assumes you're stitching 360° panorama photos, **NOT** bird's-eye view for ground plane!

---

## 🚨 **Why You're Seeing Issues**

### Spherical Warping is NOT Ideal for Bird's-Eye View!

**What spherical warping does:**
- ✅ Good for: Panoramas, 360° photos, VR content
- ❌ Bad for: Ground plane projection, parking cameras

**What you actually need:**
- **Perspective transformation** (homography)
- **Ground plane projection** (IPM - Inverse Perspective Mapping)

---

## 🎯 **BETTER ALTERNATIVES FOR BIRD'S-EYE VIEW**

### **Option 1: Planar Homography (Best for Your Use Case)**

Instead of projecting onto a sphere, project onto a flat ground plane.

**Replace spherical warper with:**

```cpp
bool SVAppSimple::setupWarpMaps() {
    warp_x_maps.resize(NUM_CAMERAS);
    warp_y_maps.resize(NUM_CAMERAS);
    
    // ═══════════════════════════════════════════════════════════
    // USE PLANAR WARPER INSTEAD OF SPHERICAL
    // ═══════════════════════════════════════════════════════════
    auto warper = cv::makePtr<cv::detail::PlaneWarper>(
        static_cast<float>(scale_factor * focal_length)
    );
    
    // Rest stays the same...
    cv::Size input_size(CAMERA_WIDTH, CAMERA_HEIGHT);
    cv::Size scaled_input(input_size.width * scale_factor, 
                          input_size.height * scale_factor);
    
    for (int i = 0; i < NUM_CAMERAS; i++) {
        cv::Mat K_scaled = K_matrices[i].clone();
        K_scaled.at<float>(0, 0) *= scale_factor;
        K_scaled.at<float>(1, 1) *= scale_factor;
        K_scaled.at<float>(0, 2) *= scale_factor;
        K_scaled.at<float>(1, 2) *= scale_factor;
        
        cv::Mat xmap, ymap;
        warper->buildMaps(scaled_input, K_scaled, R_matrices[i], xmap, ymap);
        
        warp_x_maps[i].upload(xmap);
        warp_y_maps[i].upload(ymap);
    }
    
    return true;
}
```

**Change ONE line:**
```cpp
// OLD:
auto warper = cv::makePtr<cv::detail::SphericalWarper>(...);

// NEW:
auto warper = cv::makePtr<cv::detail::PlaneWarper>(...);
```

---

### **Option 2: Cylindrical Warper**

Middle ground between spherical and planar:

```cpp
auto warper = cv::makePtr<cv::detail::CylindricalWarper>(
    static_cast<float>(scale_factor * focal_length)
);
```

---

### **Option 3: Custom Homography (Most Control)**

Compute homography matrix directly for ground plane projection.

**Add new function:**

```cpp
bool SVAppSimple::setupHomographyMaps() {
    warp_x_maps.resize(NUM_CAMERAS);
    warp_y_maps.resize(NUM_CAMERAS);
    
    cv::Size output_size(1280, 800);  // Desired output size
    
    for (int i = 0; i < NUM_CAMERAS; i++) {
        // Define source points (4 corners in original image)
        std::vector<cv::Point2f> src_pts = {
            cv::Point2f(0, CAMERA_HEIGHT),           // Bottom-left
            cv::Point2f(CAMERA_WIDTH, CAMERA_HEIGHT), // Bottom-right
            cv::Point2f(CAMERA_WIDTH * 0.2, CAMERA_HEIGHT * 0.6), // Top-left
            cv::Point2f(CAMERA_WIDTH * 0.8, CAMERA_HEIGHT * 0.6)  // Top-right
        };
        
        // Define destination points (bird's-eye rectangle)
        std::vector<cv::Point2f> dst_pts = {
            cv::Point2f(0, output_size.height),
            cv::Point2f(output_size.width, output_size.height),
            cv::Point2f(0, 0),
            cv::Point2f(output_size.width, 0)
        };
        
        // Compute homography
        cv::Mat H = cv::getPerspectiveTransform(src_pts, dst_pts);
        
        // Build warp maps from homography
        cv::Mat xmap(output_size, CV_32F);
        cv::Mat ymap(output_size, CV_32F);
        
        for (int y = 0; y < output_size.height; y++) {
            for (int x = 0; x < output_size.width; x++) {
                cv::Mat pt_dst = (cv::Mat_<double>(3,1) << x, y, 1);
                cv::Mat pt_src = H.inv() * pt_dst;
                
                float src_x = pt_src.at<double>(0) / pt_src.at<double>(2);
                float src_y = pt_src.at<double>(1) / pt_src.at<double>(2);
                
                xmap.at<float>(y, x) = src_x;
                ymap.at<float>(y, x) = src_y;
            }
        }
        
        warp_x_maps[i].upload(xmap);
        warp_y_maps[i].upload(ymap);
    }
    
    return true;
}
```

---

## 📊 **Comparison of Warping Methods**

| Method | Best For | Pros | Cons | Your Use Case |
|--------|----------|------|------|---------------|
| **Spherical** | 360° panoramas | Smooth blending | Curved lines | ❌ Not ideal |
| **Planar** | Ground plane | Straight lines | Limited FOV | ✅ **BEST** |
| **Cylindrical** | Wide scenes | Good compromise | Some curvature | ⚠️ OK |
| **Custom Homography** | Parking cameras | Full control | Manual tuning | ✅ **BEST** |

---

## 🔧 **RECOMMENDED FIX FOR YOUR ISSUE**

### **Try Planar Warper First (Easiest Fix)**

**Edit:** `src/SVAppSimple.cpp` line ~329

**Change from:**
```cpp
auto warper = cv::makePtr<cv::detail::SphericalWarper>(
    static_cast<float>(scale_factor * focal_length)
);
```

**To:**
```cpp
auto warper = cv::makePtr<cv::detail::PlaneWarper>(
    static_cast<float>(scale_factor * focal_length)
);
```

**Rebuild:**
```bash
cd build
make
./SurroundViewSimple
```

---

## 🎯 **Available OpenCV Warpers**

OpenCV provides these warpers in `opencv2/stitching/detail/warpers.hpp`:

```cpp
// Available warper types:
cv::detail::SphericalWarper    // Current - curves everything
cv::detail::PlaneWarper         // Flat projection - good for ground
cv::detail::CylindricalWarper   // Horizontal curves only
cv::detail::FisheyeWarper       // For fisheye cameras
cv::detail::StereographicWarper // Special projection
cv::detail::CompressedRectilinearWarper // Less distortion
cv::detail::PaniniWarper        // Artistic projection
cv::detail::MercatorWarper      // Map-like projection
```

---

## 💡 **Why Spherical Creates Curves**

**Spherical warping wraps image onto a sphere:**

```
Flat Image → [Sphere Surface] → Unwrap to Rectangle
              ╱╲
            ╱    ╲
          ╱ Curved ╲
         ╱  Surface  ╲
        ╱              ╲

Result: Everything curves!
```

**Planar warping projects onto flat plane:**

```
Flat Image → [Flat Ground Plane] → Rectangle
                  ═══════
                 Ground

Result: Straight lines stay straight!
```

---

## 🔬 **Test Each Warper**

Create a test function to compare:

```cpp
void SVAppSimple::testWarpers() {
    std::vector<std::string> warper_names = {
        "Spherical", "Planar", "Cylindrical"
    };
    
    for (const auto& name : warper_names) {
        cv::Ptr<cv::detail::RotationWarper> warper;
        
        if (name == "Spherical") {
            warper = cv::makePtr<cv::detail::SphericalWarper>(focal_length);
        } else if (name == "Planar") {
            warper = cv::makePtr<cv::detail::PlaneWarper>(focal_length);
        } else if (name == "Cylindrical") {
            warper = cv::makePtr<cv::detail::CylindricalWarper>(focal_length);
        }
        
        std::cout << "Testing " << name << " warper..." << std::endl;
        // Build maps and test...
    }
}
```

---

## 🎯 **Quick Action Items**

### **IMMEDIATE FIX:**

1. **Change to PlaneWarper:**
   ```cpp
   auto warper = cv::makePtr<cv::detail::PlaneWarper>(
       static_cast<float>(scale_factor * focal_length)
   );
   ```

2. **Reduce pitch angle:**
   ```python
   pitch_down = 10  # In generate_calibration.py
   ```

3. **Rebuild and test**

---

## 📊 **Expected Results**

### After changing to PlaneWarper:

**Before (Spherical):**
```
╱──────╲  ← Curved desk edges
│ Desk  │
╲──────╱
```

**After (Planar):**
```
┌──────┐  ← Straight desk edges
│ Desk │
└──────┘
```

---

## 🔗 **OpenCV Documentation**

See all available warpers:
```cpp
// In your code:
#include <opencv2/stitching/detail/warpers.hpp>

// Available types documented at:
// https://docs.opencv.org/4.x/d7/d74/classcv_1_1detail_1_1RotationWarper.html
```

---

## ✅ **Summary**

**Your question:** "Is spherical warp the only way?"

**Answer:** **NO!** Spherical is actually **wrong** for bird's-eye view.

**What to use:** **PlaneWarper** for flat ground projection

**Quick fix:** Change ONE line in `setupWarpMaps()`:
```cpp
cv::detail::PlaneWarper  // Instead of SphericalWarper
```

Try this and share the results! Should look much better. 🎯
