#ifndef SV_APP_SIMPLE_HPP
#define SV_APP_SIMPLE_HPP

#include "SVEthernetCamera.hpp"
#include "SVRenderSimple.hpp"
#include <memory>
#include <array>
#include <string>
// #define WARPING
// #define WARPING_CUSTOM
// #define WARPING_SPERICAL
// #define WARPING_IPM
#define RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY
#define CUSTOM_HOMOGRAPHY_INTERACTIVE  // Enable for interactive calibration (requires GTK)
// #define CUSTOM_HOMOGRAPHY_NONINTERACTIVE   // Enable for non-interactive mode (uses defaults)

// Include necessary OpenCV headers for warping and custom homography
#if defined(WARPING) || defined(RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY)
    #include <opencv2/cudawarping.hpp>
    #include <opencv2/stitching/detail/warpers.hpp>
    #include <vector>
#endif
#define NUM_CAMERAS 4

/**
 * @brief Ultra-Simplified Surround View Application
 * 
 * NO STITCHING - Just displays 4 camera views around a 3D car:
 * - Camera capture from 4 H.264 Ethernet streams
 * - Simple 4-panel display around rendered car
 * - No warping, no blending, no gain compensation
 */
class SVAppSimple {
public:
    SVAppSimple();
    ~SVAppSimple();
    
    /**
     * @brief Initialize the system
     * @return true if initialization successful
     */
    bool init();
    
    /**
     * @brief Run main loop (blocking)
     */
    void run();
    
    /**
     * @brief Stop the system
     */
    void stop();
    
private:
    // Camera source
    std::shared_ptr<MultiCameraSource> camera_source;
    std::array<Frame, NUM_CAMERAS> frames;

    #ifdef WARPING
        std::vector<cv::Mat> K_matrices;
        std::vector<cv::Mat> R_matrices;
        float focal_length;
        bool loadCalibration(const std::string& folder);
        bool setupWarpMaps();
    #endif

    #if defined(WARPING) || defined(RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY)
        std::vector<cv::cuda::GpuMat> warp_x_maps;
        std::vector<cv::cuda::GpuMat> warp_y_maps;
        float scale_factor;
    #endif

    #ifdef RENDER_PRESERVE_AS_CUSTOMHOMOGRAPHY
        // Manual calibration points for each camera (4 points per camera)
        std::vector<std::vector<cv::Point2f>> manual_src_points;  // Source (perspective view)
        std::vector<std::vector<cv::Point2f>> manual_dst_points;  // Destination (bird's-eye)
        bool selectManualCalibrationPoints(const std::array<Frame, NUM_CAMERAS>& sample_frames);
        bool saveCalibrationPoints(const std::string& folder);
        bool loadCalibrationPoints(const std::string& folder);
        bool setupCustomHomographyMaps();
    #endif


    
    // Rendering (no stitching!)
    std::shared_ptr<SVRenderSimple> renderer;
    
    // State
    bool is_running;
};

#endif // SV_APP_SIMPLE_HPP
