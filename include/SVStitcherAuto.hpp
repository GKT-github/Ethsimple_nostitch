#ifndef SV_STITCHER_AUTO_HPP
#define SV_STITCHER_AUTO_HPP

#include "SVConfig.hpp"
#include "SVBlender.hpp"
#include "SVGainCompensator.hpp"
#include <opencv2/core.hpp>
#include <opencv2/core/cuda.hpp>
#include <vector>
#include <string>
#include <memory>

/**
 * @brief Automotive Surround View Stitcher
 * 
 * Stitches 4 camera views into a seamless surround view using:
 * - Diagonal X-pattern layout (640×800 canvas divided by diagonal lines)
 * - Alpha blending with 40px fade zones along diagonals
 * - Gain compensation (exposure matching)
 * - Custom homography warping (already done in your app)
 * 
 * Layout - Diagonal X-Pattern (640×800):
 * 
 *        TL (Front)      TR (Right)
 *        /          \    /          \
 *       /   Camera   \  /   Camera   \
 *      /      0      \/      3      \
 *     /______ /\______X______/\ ______\
 *    /       /  \     |     /  \       \
 *   /       /    \    |    /    \       \
 *  /  Camera1     \   |   /   Camera2    \
 * /   (Left)       \ | /    (Rear)       \
 *\/________________\|/___________________/
 * 
 * Each camera's region is a TRAPEZIUM (not rectangle):
 * - Camera 0 (Front):  Top-left trapezium, blends with Right along diagonal /
 * - Camera 1 (Left):   Bottom-left trapezium, blends along both diagonals
 * - Camera 2 (Rear):   Bottom-right trapezium, blends along both diagonals
 * - Camera 3 (Right):  Top-right trapezium, blends with Front along diagonal \
 * 
 * Blending: Only along the two diagonal lines (40px fade zones)
 * Outside blend zones: Pure camera data displayed
 */
class SVStitcherAuto {
public:
    SVStitcherAuto();
    ~SVStitcherAuto();
    
    /**
     * @brief Initialize stitcher with camera configuration
     * @param sample_frames Sample frames from all 4 cameras
     * @param warp_x_maps X warp maps (from your homography)
     * @param warp_y_maps Y warp maps (from your homography)
     * @param scale_factor Processing scale (0.65)
     * @return true if successful
     */
    bool init(const std::vector<cv::cuda::GpuMat>& sample_frames,
              const std::vector<cv::cuda::GpuMat>& warp_x_maps,
              const std::vector<cv::cuda::GpuMat>& warp_y_maps,
              float scale_factor);
    
    /**
     * @brief Stitch 4 camera frames into seamless output
     * @param raw_frames Raw camera frames (before warping)
     * @param warped_frames Warped frames (after homography)
     * @param output Stitched output frame (GPU)
     * @return true if successful
     */
    bool stitch(const std::vector<cv::cuda::GpuMat>& raw_frames,
                const std::vector<cv::cuda::GpuMat>& warped_frames,
                cv::cuda::GpuMat& output);
    
    /**
     * @brief Recompute gain compensation (call periodically)
     * @param warped_frames Vector of warped camera frames
     */
    void recomputeGain(const std::vector<cv::cuda::GpuMat>& warped_frames);
    
    /**
     * @brief Check if stitcher is initialized
     */
    bool isInitialized() const { return is_init; }
    
    /**
     * @brief Get stitched output size
     */
    cv::Size getOutputSize() const { return output_size; }
    
private:
    /**
     * @brief Create overlap masks for 4-camera layout
     * @param sample_frames Sample frames to determine size
     * @return true if successful
     */
    bool createOverlapMasks(const std::vector<cv::cuda::GpuMat>& sample_frames);
    
    /**
     * @brief Compute ROI (region of interest) for stitched output
     * @param corners Corner positions of warped images
     * @param sizes Sizes of warped images
     */
    cv::Rect computeStitchROI(const std::vector<cv::Point>& corners,
                               const std::vector<cv::Size>& sizes);
    
    // Simple blending
    std::shared_ptr<SVBlender> blender;
    
    // Gain compensation (optional - can disable for pure alpha blend)
    std::shared_ptr<SVGainCompensator> gain_comp;
    bool use_gain_compensation;
    
    // Masks for overlap regions (diagonal fade zones)
    std::vector<cv::cuda::GpuMat> blend_masks;
    
    // Warp information
    std::vector<cv::Point> warp_corners;
    std::vector<cv::Size> warp_sizes;
    
    // Output configuration
    cv::Size output_size;
    
    // output_roi: Overall canvas bounds (640×800) - NOT per-camera region
    // Each camera's region is TRAPEZIUM-shaped (not rectangular):
    // - Defined by its diagonal blend mask, not by a simple Rect
    // - Blending occurs only along diagonal centerlines within 40px fade zones
    // - Outside blend zones: pure camera data (no blending)
    cv::Rect output_roi;
    
    // Trapezium regions (for reference - blend_masks define actual boundaries):
    // Camera 0 (Front):  Top-left trapezium (0,0)-(320,0)-(320,400)-(0,400)
    //   Blends with Right along diagonal y=1.25x
    //   Blends with Left along diagonal y=-1.25x+800
    //
    // Camera 1 (Left):   Bottom-left trapezium (0,400)-(0,800)-(320,800)-(320,400)
    //   Blends with Front along diagonal y=1.25x
    //   Blends with Rear along diagonal y=-1.25x+800
    //
    // Camera 2 (Rear):   Bottom-right trapezium (320,400)-(320,800)-(640,800)-(640,400)
    //   Blends with Left along diagonal y=1.25x
    //   Blends with Right along diagonal y=-1.25x+800
    //
    // Camera 3 (Right):  Top-right trapezium (320,0)-(640,0)-(640,400)-(320,400)
    //   Blends with Front along diagonal y=-1.25x+800
    //   Blends with Rear along diagonal y=1.25x
    
    // State
    bool is_init;
    int num_cameras;
    float scale_factor;
    int frame_count;
    
    // Gain update interval
    static constexpr int GAIN_UPDATE_INTERVAL = 30; // Every 30 frames
};

#endif // SV_STITCHER_AUTO_HPP