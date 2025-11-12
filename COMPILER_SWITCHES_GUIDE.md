# Compiler Switches for Custom Homography - Quick Guide

## 🎛️ Available Switches

Located in: `include/SVAppSimple.hpp` (lines 13-15)

```cpp
#define RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY       // Enable custom homography
#define CUSTOM_HOMOGRAPHY_INTERACTIVE              // Interactive mode (requires GTK)
#define CUSTOM_HOMOGRAPHY_NONINTERACTIVE           // Non-interactive mode (no GTK needed)
```

---

## 📋 Switch Combinations

### **Option 1: Non-Interactive Mode (RECOMMENDED - Current)**
```cpp
#define RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY
// #define CUSTOM_HOMOGRAPHY_INTERACTIVE
#define CUSTOM_HOMOGRAPHY_NONINTERACTIVE
```

**Best for:**
- ✅ Systems without GTK support
- ✅ Headless/remote systems
- ✅ Quick testing with default calibration
- ✅ Custom calibration via YAML file editing

**Behavior:**
- Uses hardcoded default calibration points
- No GUI window needed
- Saves calibration to `custom_homography_points.yaml`
- Can be refined by editing the YAML file

---

### **Option 2: Interactive Mode (After Installing GTK)**
```cpp
#define RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY
#define CUSTOM_HOMOGRAPHY_INTERACTIVE
// #define CUSTOM_HOMOGRAPHY_NONINTERACTIVE
```

**Best for:**
- ✅ Systems with GTK support
- ✅ Real-time visual calibration
- ✅ Precise point selection
- ✅ User-friendly calibration

**Behavior:**
- Shows OpenCV windows for each camera
- User clicks 4 points per camera
- Real-time console feedback
- Saves calibration to `custom_homography_points.yaml`

**Prerequisites:**
```bash
# Install GTK support
sudo apt-get install libgtk2.0-dev pkg-config

# Rebuild OpenCV with GTK
cd ~/opencv/build
cmake .. -DWITH_GTK=ON
make -j4 && sudo make install
```

---

### **Option 3: Both Modes Enabled (Not Recommended)**
```cpp
#define RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY
#define CUSTOM_HOMOGRAPHY_INTERACTIVE
#define CUSTOM_HOMOGRAPHY_NONINTERACTIVE
```

**Issue:**
❌ **Compiler will fail!** Both `#ifdef` blocks define the same function

**Error:**
```
error: redefinition of 'bool SVAppSimple::selectManualCalibrationPoints(...)'
```

**Resolution:** Only enable ONE of the two mode switches

---

## 🔄 How to Switch Modes

### From Non-Interactive to Interactive

**Step 1:** Install GTK
```bash
sudo apt-get install libgtk2.0-dev pkg-config
```

**Step 2:** Rebuild OpenCV with GTK support
```bash
cd ~/opencv
mkdir -p build_gtk
cd build_gtk
cmake .. -DWITH_GTK=ON
make -j4
sudo make install
sudo ldconfig
```

**Step 3:** Update header
Edit `include/SVAppSimple.hpp`:
```cpp
#define RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY
#define CUSTOM_HOMOGRAPHY_INTERACTIVE              // ENABLE THIS
// #define CUSTOM_HOMOGRAPHY_NONINTERACTIVE         // DISABLE THIS
```

**Step 4:** Rebuild project
```bash
cd /home/nano/Documents/SVS-nostitch/Ethsimple_nostitch/build
cmake ..
make clean
make -j4
```

**Step 5:** Delete old calibration (optional)
```bash
rm ../camparameters/custom_homography_points.yaml
```

**Step 6:** Run with interactive calibration
```bash
./SurroundViewSimple
# Now you can click 4 points per camera!
```

---

### From Interactive to Non-Interactive

**Step 1:** Update header
Edit `include/SVAppSimple.hpp`:
```cpp
#define RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY
// #define CUSTOM_HOMOGRAPHY_INTERACTIVE            // DISABLE THIS
#define CUSTOM_HOMOGRAPHY_NONINTERACTIVE           // ENABLE THIS
```

**Step 2:** Rebuild project
```bash
cd /home/nano/Documents/SVS-nostitch/Ethsimple_nostitch/build
cmake ..
make -j4
```

**Step 3:** Run
```bash
./SurroundViewSimple
# Uses default calibration points (no GUI)
```

---

## 🎯 Current Configuration

Your current setup (RECOMMENDED):
```cpp
#define RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY       ✅ Enabled
#define CUSTOM_HOMOGRAPHY_INTERACTIVE              ❌ Disabled (comment)
#define CUSTOM_HOMOGRAPHY_NONINTERACTIVE           ✅ Enabled
```

**Status:** ✅ Ready to run (no GTK required)

**Output when run:**
```
[3/4] Setting up custom homography with manual points...
NON-INTERACTIVE CALIBRATION: Using Default Points
Note: OpenCV compiled without GTK support or interactive mode disabled
Using default calibration points instead

Using default calibration points:
  Camera 0:
    Point 0: (256, 360)
    Point 1: (1024, 360)
    Point 2: (1280, 800)
    Point 3: (0, 800)
  [... repeat for cameras 1-3 ...]

✓ Default calibration points loaded!
To refine calibration:
  1. Edit '../camparameters/custom_homography_points.yaml'
  2. Or install GTK and use interactive calibration
```

---

## 📝 Default Calibration Points

Located in: `src/SVAppSimple.cpp` (lines ~650-665)

### **Camera 0: Front**
```
(256, 360)  ─────────  (1024, 360)
    │        GROUND        │
    │                      │
(0, 800)   ─────────  (1280, 800)
```

### **Camera 1: Left (90° left)**
```
(200, 400)  ─────────  (850, 300)
    │        GROUND        │
    │                      │
(0, 800)   ─────────  (1280, 800)
```

### **Camera 2: Rear (opposite of front)**
```
(256, 360)  ─────────  (1024, 360)
    │        GROUND        │
    │                      │
(0, 800)   ─────────  (1280, 800)
```

### **Camera 3: Right (90° right)**
```
(430, 300)  ─────────  (1080, 400)
    │        GROUND        │
    │                      │
(0, 800)   ─────────  (1280, 800)
```

**To customize:** Edit the values in `src/SVAppSimple.cpp` and rebuild.

---

## 🧪 Testing

### Test Non-Interactive Mode
```bash
cd /home/nano/Documents/SVS-nostitch/Ethsimple_nostitch/build
./SurroundViewSimple
# Should use default calibration immediately
```

### Test Interactive Mode (After GTK Installation)
```bash
# Change header to CUSTOM_HOMOGRAPHY_INTERACTIVE
# Rebuild
./SurroundViewSimple
# Should show OpenCV windows for point selection
```

---

## ⚙️ Configuration Files

### Header File
- **Location:** `include/SVAppSimple.hpp` (lines 13-15)
- **Changes:** Comment/uncomment the mode switches

### Implementation File
- **Location:** `src/SVAppSimple.cpp` (lines 513-695)
- **Sections:**
  - Lines 515-600: `CUSTOM_HOMOGRAPHY_INTERACTIVE` implementation
  - Lines 602-695: `CUSTOM_HOMOGRAPHY_NONINTERACTIVE` implementation

### Calibration Data
- **Location:** `../camparameters/custom_homography_points.yaml`
- **Created:** After first run
- **Can be:** Manually edited to refine calibration

---

## 🚀 Performance Comparison

| Aspect | Interactive | Non-Interactive |
|--------|-------------|-----------------|
| **GUI** | Yes (requires GTK) | No |
| **User Input** | Yes (4 clicks per camera) | No |
| **Startup Time** | ~1-2 minutes (manual calibration) | <1 second |
| **Point Accuracy** | High (visual feedback) | Medium (hardcoded defaults) |
| **Refinement** | Via YAML file | Via YAML file |
| **Ease of Use** | Easiest | Simpler |

---

## 📖 Commands Quick Reference

```bash
# View current configuration
cat include/SVAppSimple.hpp | grep CUSTOM_HOMOGRAPHY

# Build current configuration
cd build && cmake .. && make -j4

# Run current configuration
./build/SurroundViewSimple

# Check OpenCV GTK support
python3 -c "import cv2; print(cv2.getBuildInformation())" | grep -i gtk

# Edit calibration points
nano ../camparameters/custom_homography_points.yaml

# Delete saved calibration
rm ../camparameters/custom_homography_points.yaml
```

---

## ✅ Checklist

- [x] Non-interactive mode working
- [ ] Install GTK support (when ready)
- [ ] Switch to interactive mode (after GTK installation)
- [ ] Calibrate with interactive GUI
- [ ] Verify bird's-eye view output
- [ ] Save calibration YAML

---

**Current Status**: ✅ Non-Interactive Mode Ready  
**Next Step**: Install GTK for interactive calibration (optional)

---

**Last Updated**: November 12, 2025
