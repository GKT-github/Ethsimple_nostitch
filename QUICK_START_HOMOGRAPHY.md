# Quick Reference: Custom Homography

## 🎯 What It Does
Allows you to manually select 4 points per camera to define a custom bird's-eye view transformation. No calibration files needed - just click and confirm!

---

## ⚡ Quick Start (3 Steps)

### 1️⃣ Enable in Header
Edit `include/SVAppSimple.hpp`:
```cpp
#define RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY
```

### 2️⃣ Rebuild
```bash
cd build && cmake .. && make -j4
```

### 3️⃣ Run
```bash
./build/SurroundViewSimple
```

---

## 🖱️ First Run Workflow

For **each of 4 cameras** (Front → Left → Rear → Right):

1. **Window opens** showing camera feed
2. **Click 4 points** (trapezoid shape):
   - Point 1: Top-left ground
   - Point 2: Top-right ground
   - Point 3: Bottom-right ground
   - Point 4: Bottom-left ground
3. **Press SPACE** → Move to next camera
4. **Or Press R** → Reset if wrong

Console shows feedback:
```
Point 1: (256, 360)
Point 2: (1024, 360)
Point 3: (1280, 800)
Point 4: (0, 800)
✓ Camera 0 calibration complete
```

---

## 💾 Saved Files

After first run, file is created:
```
../camparameters/custom_homography_points.yaml
```

**Future runs**: Automatically loads → no manual selection needed!

---

## 🔄 Recalibrate (Delete & Restart)

```bash
# Delete saved calibration
rm ../camparameters/custom_homography_points.yaml

# Run again - will prompt for new calibration
./build/SurroundViewSimple
```

---

## 🧮 How It Works (Simple)

```
Your 4 clicks (Camera View)        Output (Bird's-Eye View)
    
    P1 ─────── P2                  ┌──────────┐
    │          │         ┐         │          │
    │          │    ──→  │ Maps    │          │
    │          │         │ using   │          │
    P4 ─────── P3        │ H matrix│          │
                         ┘         └──────────┘

H = getPerspectiveTransform([P1,P2,P3,P4], [corners])
```

For each bird's-eye pixel: `pt_camera = H⁻¹ × pt_output`

---

## 📋 Troubleshooting

| Problem | Solution |
|---------|----------|
| Window won't respond | Click on window to focus |
| Points not registering | Ensure window has focus, click again |
| Only 3 points show | You need exactly 4 - keep clicking |
| Wrong points selected | Press 'R' to reset, click 4 again |
| File loading error | Delete YAML file, recalibrate |
| Distorted output | Points might be in wrong order or off-ground |

---

## 🎨 Point Selection Tips

### ✅ Good Calibration
- Points outline **actual ground** visible in camera
- Forms clear **trapezoid** (narrow at horizon, wide at bottom)
- Order is consistent: **TL → TR → BR → BL**
- All 4 points well-distributed

### ❌ Bad Calibration
- Points on **sky or walls** (not ground)
- Nearly **parallel sides** (too-narrow trapezoid)
- **Random order** (inconsistent)
- Some points **off-screen**

---

## 🔧 Key Locations

| What | Where |
|-----|-------|
| Enable switch | `include/SVAppSimple.hpp` line ~9 |
| Implementation | `src/SVAppSimple.cpp` lines ~475-750 |
| Saved data | `../camparameters/custom_homography_points.yaml` |
| Full docs | `README_CUSTOM_HOMOGRAPHY.md` |

---

## 🎓 Under the Hood

```cpp
// Your clicks become:
manual_src_points[cam] = {P1, P2, P3, P4}

// Homography computed:
H = cv::getPerspectiveTransform(dst_rectangle, manual_src_points[cam])

// Warp maps generated:
for each pixel (x,y) in output:
    pt_src = H⁻¹ × (x, y, 1)
    xmap[y][x] = pt_src.x
    ymap[y][x] = pt_src.y

// GPU remapping (every frame):
cv::cuda::remap(camera_frame, output, xmap, ymap, LINEAR)
```

---

## ⏱️ Performance

- **Calibration**: ~1 minute (interactive, one-time)
- **Map generation**: ~100ms (first-time only)
- **Per-frame remapping**: <1ms (real-time ✓)

---

## 🔑 Function Quick Reference

```cpp
// Main function in init()
setupCustomHomographyMaps()          // Builds warp maps

// Helper functions
selectManualCalibrationPoints()      // GUI for clicking points
saveCalibrationPoints()              // Save to YAML
loadCalibrationPoints()              // Load from YAML
```

---

## 📝 YAML File Format

```yaml
num_cameras: 4
scale_factor: 0.65

camera_0_src_points:    # Your 4 clicks
  - [256, 360]
  - [1024, 360]
  - [1280, 800]
  - [0, 800]

camera_0_dst_points:    # Output rectangle (fixed)
  - [0, 0]
  - [832, 0]
  - [832, 520]
  - [0, 520]

# ... repeat for cameras 1-3 ...
```

---

## 🚀 Advanced: Programmatic Adjustment

```cpp
// Load calibration
loadCalibrationPoints("../camparameters");

// Manually tweak a point
manual_src_points[0][0] = cv::Point2f(260, 365);

// Regenerate maps
setupCustomHomographyMaps();

// Continue rendering...
```

---

## ❓ FAQ

**Q: Do I need calibration files?**  
A: No! Custom homography works without them.

**Q: How many times do I calibrate?**  
A: Once! Then it's saved and reused.

**Q: Can I use different points for each camera?**  
A: Yes! That's the whole point - customize per camera.

**Q: What if I mess up a point?**  
A: Press 'R' to reset that camera and try again.

**Q: How accurate is the transformation?**  
A: Depends on point accuracy. More precise clicks = better results.

**Q: Can I edit points in the YAML file manually?**  
A: Yes, but you need to rerun to regenerate maps.

---

## 🎬 Example Workflow

```bash
# 1. Make sure switch is enabled
nano include/SVAppSimple.hpp
# Check: #define RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY

# 2. Build
cd build && make clean && cmake .. && make -j4

# 3. Run (first time)
./SurroundViewSimple
# → Manual calibration GUI appears
# → Click 4 points per camera
# → Calibration saved to YAML

# 4. Run (second time)  
./SurroundViewSimple
# → Loads saved calibration instantly
# → No manual selection needed
# → Bird's-eye view working!

# 5. Recalibrate if needed
rm ../camparameters/custom_homography_points.yaml
./SurroundViewSimple
# → Manual calibration again
```

---

**Pro Tip**: Take a screenshot of your calibration to remember which points you selected in case you need to recreate it!

---

**Quick Reference Card**  
**v1.0** | **Nov 2025**
