# Custom Homography with Manual Point Selection

This document explains how to use the custom homography feature for bird's-eye view transformation with manual point calibration.

---

## 🎯 Overview

The custom homography system allows you to manually select 4 calibration points on each camera to define a precise perspective transformation to bird's-eye view. Instead of using mathematical camera models or pre-defined transformations, you directly specify the ground region visible in each camera.

### Key Benefits:
- **No calibration files required** - Works without camera intrinsic/distortion parameters
- **Visual calibration** - You define the transformation interactively
- **Per-camera customization** - Each camera can have a unique transformation
- **Persistent calibration** - Points are saved to YAML for future use
- **Precise control** - Define exactly which region of the camera sees the ground

---

## 🔧 How It Works

### Transformation Concept

For each camera, the system computes a **homography matrix** that maps a rectangular region in bird's-eye view to a trapezoid in the camera's perspective view:

```
Bird's-Eye View              Camera View (Perspective)
┌─────────────────┐          ┌──────────────────┐
│    Output       │          │   Input          │
│  Rectangle      │  <─────  │  Trapezoid       │
│    (0,0)        │  (H⁻¹)   │  (top-left)      │
└─────────────────┘          │  (wider bottom)  │
                             └──────────────────┘
```

The homography matrix H is computed using `cv::getPerspectiveTransform()` from 4 point correspondences:
- **Source points**: 4 corners in camera's perspective view (trapezoid defining ground)
- **Destination points**: 4 corners of output rectangle (bird's-eye view)

For each pixel in the output bird's-eye image, the inverse homography computes where that pixel comes from in the input camera image using:
```cpp
pt_source = H⁻¹ × pt_destination
```

---

## 🚀 Quick Start

### Enable Custom Homography

1. **Open** `include/SVAppSimple.hpp`

2. **Ensure this line exists and is NOT commented:**
   ```cpp
   #define RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY
   ```

3. **Optionally disable other warping modes** (comment these out):
   ```cpp
   // #define WARPING
   // #define WARPING_CUSTOM
   // #define WARPING_SPERICAL
   // #define WARPING_IPM
   ```

4. **Rebuild**:
   ```bash
   cd build
   cmake ..
   make -j4
   ```

### First Run - Manual Calibration

When you run the application for the first time:

```bash
./build/SurroundViewSimple
```

The system will launch interactive windows for each camera where you need to select 4 points:

1. **Camera 0 window opens** showing the front camera feed
2. **Click 4 points** in order:
   - **Point 1 (top-left)**: Top-left corner of ground area
   - **Point 2 (top-right)**: Top-right corner of ground area  
   - **Point 3 (bottom-right)**: Bottom-right corner of ground area
   - **Point 4 (bottom-left)**: Bottom-left corner of ground area

3. **Controls**:
   - **Left-click**: Add a point
   - **R key**: Reset current camera (clear all 4 points)
   - **SPACE**: Confirm 4 points and move to next camera

4. **Repeat for all 4 cameras** (front, left, rear, right)

5. **Calibration saved** → Points are automatically saved to:
   ```
   ../camparameters/custom_homography_points.yaml
   ```

### Subsequent Runs

On subsequent runs, the system automatically loads the saved calibration points and skips the manual selection step.

---

## 📋 Calibration Guidelines

### How to Select Good Points

Each camera can see the ground plane differently. Use these guidelines:

#### **Front Camera** (Looking forward)
- Point 1: Leftmost visible ground at horizon
- Point 2: Rightmost visible ground at horizon
- Point 3: Far-right corner of visible ground (near bumper)
- Point 4: Far-left corner of visible ground (near bumper)

```
           P1 ─────── P2
            │  GROUND  │
            │          │
           P4 ─────── P3
```

#### **Left Camera** (90° left)
- Points should outline the left side of car's surroundings
- Trapezoid should narrow toward horizon
- Include visible ground area only

#### **Rear Camera** (Looking backward)
- Similar to front, but reversed (backing up view)
- Points outline ground visible behind car

#### **Right Camera** (90° right)
- Mirror of left camera
- Points outline right side surroundings

### Tips for Accurate Calibration

✅ **DO:**
- Select points on the **actual ground/floor** visible in each camera
- Ensure points form a reasonable **trapezoid shape** (parallel-ish sides)
- Use the **full visible ground area** in the camera
- Be consistent with point ordering (always: TL → TR → BR → BL)

❌ **DON'T:**
- Click on sky, walls, or objects above ground
- Create extreme trapezoids (nearly parallel lines)
- Leave large ground areas un-calibrated
- Mix up point ordering

### Recalibrate

To redo the calibration:

1. **Delete the saved file**:
   ```bash
   rm ../camparameters/custom_homography_points.yaml
   ```

2. **Run again** - The application will prompt for new manual selection

---

## 🔍 Calibration File Format

Saved calibration is stored in YAML format:

```yaml
num_cameras: 4
scale_factor: 0.65
camera_0_src_points:
  - [245.3, 480.0]    # Top-left
  - [1034.7, 480.0]   # Top-right
  - [1280.0, 800.0]   # Bottom-right
  - [0.0, 800.0]      # Bottom-left
camera_0_dst_points:
  - [0.0, 0.0]
  - [832.0, 0.0]
  - [832.0, 520.0]
  - [0.0, 520.0]
camera_1_src_points:
  ...
```

- **src_points**: Manual points you clicked in camera's perspective view
- **dst_points**: Output rectangle corners (always same for bird's-eye)
- **scale_factor**: Processing scale (usually 0.65)

---

## 🧮 Technical Details

### Homography Computation

```cpp
// Source points (4 corners in perspective view)
std::vector<cv::Point2f> src_pts = {...};  // Your manual selections

// Destination points (4 corners in bird's-eye view)
std::vector<cv::Point2f> dst_pts = {
    {0, 0},
    {width, 0},
    {width, height},
    {0, height}
};

// Compute homography H that maps dst → src
cv::Mat H = cv::getPerspectiveTransform(dst_pts, src_pts);
```

### Warp Map Generation

For each output pixel (x, y) in bird's-eye view:

```cpp
// Convert to homogeneous coordinates
cv::Mat pt_dst = [x, y, 1]ᵀ

// Apply inverse homography to find input pixel
cv::Mat pt_src = H⁻¹ × pt_dst

// Normalize by w coordinate (perspective division)
src_x = pt_src[0] / pt_src[2]
src_y = pt_src[1] / pt_src[2]

// Store in warp map for GPU remap
xmap(y, x) = src_x
ymap(y, x) = src_y
```

### GPU Remapping

The warp maps are uploaded to GPU and used with `cv::cuda::remap()`:

```cpp
cv::cuda::remap(input_frame, output_frame,
                warp_x_map, warp_y_map,
                cv::INTER_LINEAR, cv::BORDER_CONSTANT);
```

This efficiently transforms each camera frame to bird's-eye view in real-time.

---

## 🐛 Troubleshooting

### "Invalid calibration points for camera X"

**Cause**: Calibration file is corrupted or missing points

**Fix**:
```bash
rm ../camparameters/custom_homography_points.yaml
./build/SurroundViewSimple  # Will re-do manual calibration
```

### Points aren't registering in the window

**Cause**: OpenCV window might not have focus

**Fix**:
1. Click on the OpenCV window to ensure it has focus
2. Click your points again
3. Console should print "Point X: (x, y)"

### Homography matrix seems wrong

**Cause**: Points might be in wrong order or off the ground plane

**Fix**:
- Reset with 'R' key
- Recalibrate with more careful point selection
- Ensure all 4 points are on actual ground/floor

### Warped image is distorted

**Cause**: Points define an invalid trapezoid

**Fix**:
- Ensure trapezoid sides aren't parallel (top narrower than bottom)
- Ensure winding order is correct (TL → TR → BR → BL)
- Recalibrate

### Application crashes during calibration

**Cause**: Frame might be invalid or camera connection lost

**Fix**:
1. Ensure cameras are properly connected
2. Run `/build/SurroundViewSimple` and wait for "Received valid frames"
3. Retry calibration

---

## 📚 Code Reference

### Main Functions

| Function | Purpose |
|----------|---------|
| `selectManualCalibrationPoints()` | Interactive GUI for point selection |
| `setupCustomHomographyMaps()` | Compute homography matrices and build warp maps |
| `saveCalibrationPoints()` | Save points to YAML file |
| `loadCalibrationPoints()` | Load points from YAML file |

### Member Variables

```cpp
std::vector<std::vector<cv::Point2f>> manual_src_points;  // [cam][4]
std::vector<std::vector<cv::Point2f>> manual_dst_points;  // [cam][4]
```

---

## 🔄 Workflow Summary

```
┌─────────────────────────────────────┐
│  Application Start                  │
└────────────┬────────────────────────┘
             │
             ↓
┌─────────────────────────────────────┐
│ Try to load calibration from YAML   │
└────────────┬────────────────────────┘
             │
      ┌──────┴──────┐
      │             │
      ↓             ↓
  SUCCESS      NOT FOUND
      │             │
      │             ↓
      │   ┌─────────────────────────────┐
      │   │ Manual Calibration GUI      │
      │   │ - Display each camera       │
      │   │ - User selects 4 points     │
      │   │ - Store in memory           │
      │   └────────┬────────────────────┘
      │            │
      │            ↓
      │   ┌─────────────────────────────┐
      │   │ Save to YAML                │
      │   └────────┬────────────────────┘
      │            │
      └────────┬───┘
               │
               ↓
    ┌──────────────────────────────────┐
    │ Setup Custom Homography Maps     │
    │ - Compute H matrices             │
    │ - Build warp maps                │
    │ - Upload to GPU                  │
    └────────┬─────────────────────────┘
             │
             ↓
    ┌──────────────────────────────────┐
    │ Initialize Renderer              │
    │ Start Main Loop                  │
    │ Apply warping in real-time       │
    └──────────────────────────────────┘
```

---

## 💾 Examples

### Example Calibration File

```yaml
# custom_homography_points.yaml
num_cameras: 4
scale_factor: 0.65

# Camera 0: Front view
camera_0_src_points:
  - [256.0, 360.0]    # Visible ground at horizon, top-left
  - [1024.0, 360.0]   # Visible ground at horizon, top-right
  - [1280.0, 800.0]   # Bottom-right corner at camera edge
  - [0.0, 800.0]      # Bottom-left corner at camera edge
camera_0_dst_points:
  - [0.0, 0.0]
  - [832.0, 0.0]
  - [832.0, 520.0]
  - [0.0, 520.0]

# Camera 1: Left view
camera_1_src_points:
  - [200.0, 400.0]    # Top-left ground
  - [850.0, 200.0]    # Top-right (closer to car)
  - [1280.0, 800.0]   # Bottom-right
  - [0.0, 800.0]      # Bottom-left
camera_1_dst_points:
  - [0.0, 0.0]
  - [832.0, 0.0]
  - [832.0, 520.0]
  - [0.0, 520.0]

# ... Camera 2 and 3 ...
```

---

## 🎓 Advanced Usage

### Modifying Points Programmatically

If you want to adjust calibration points in code:

```cpp
// Example: Manually adjust camera 0's first point
manual_src_points[0][0] = cv::Point2f(250.0f, 370.0f);

// Recompute warp maps
setupCustomHomographyMaps();
```

### Validating Calibration

Check if calibration loaded correctly:

```cpp
for (int i = 0; i < NUM_CAMERAS; i++) {
    std::cout << "Camera " << i << " src points:" << std::endl;
    for (int j = 0; j < 4; j++) {
        std::cout << "  (" << manual_src_points[i][j].x 
                  << ", " << manual_src_points[i][j].y << ")" << std::endl;
    }
}
```

---

## 📝 Notes

- Warp maps are computed once during initialization and reused every frame
- GPU remap is very fast (real-time performance)
- Points are in **original camera frame coordinates** (1280×800)
- Scaling is applied automatically during warp map generation
- Homography matrices printed to console for debugging

---

## 🔗 Related Files

| File | Purpose |
|------|---------|
| `include/SVAppSimple.hpp` | Header with definitions and function declarations |
| `src/SVAppSimple.cpp` | Implementation of custom homography functions |
| `../camparameters/custom_homography_points.yaml` | Saved calibration data |

---

## ✅ Testing Checklist

- [ ] Calibration file is created after first run
- [ ] Calibration loads without requiring manual selection on 2nd run
- [ ] Warp maps display correctly (no extreme distortions)
- [ ] All 4 cameras show proper bird's-eye transformation
- [ ] Console prints homography matrices for each camera
- [ ] GPU remap performance is real-time (no lag)

---

**Last Updated**: November 2025  
**Version**: 1.0  
**Status**: Stable
