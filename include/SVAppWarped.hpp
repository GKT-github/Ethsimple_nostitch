// SVAppWarped.hpp - STEP 1: Individual Bird's-Eye View Transformation
// Header file for the warped (perspective-corrected) app

#ifndef SV_APP_WARPED_HPP
#define SV_APP_WARPED_HPP

#include "SVConfig.hpp"
#include "SVEthernetCamera.hpp"
#include "SVRenderSimple.hpp"
#include <opencv2/core.hpp>
#include <opencv2/core/cuda.hpp>
#include <memory>
#include <vector>
#include <array>
#include <string>

/**
 * @brief Surround View Application with Individual Bird's-Eye Transformation
 * 
 * STEP 1 Implementation:
 * - Captures 4 camera feeds
 * - Applies spherical warp (bird's-eye transformation) to EACH camera individually
 * - Displays all 4 warped views + 3D car in 5-panel layout
 * - NO stitching/blending yet - just perspective correction
 * 
 * Layout:
 *        [Front Warped]
 *   [L]    [3D Car]    [R]
 *        [Rear Warped]
 */
class SVAppWarped {
public:
    SVAppWarped();
    ~SVAppWarped();
    
    /**
     * @brief Initialize cameras, calibration, and renderer
     * @return true if successful
     */
    bool init();
    
    /**
     * @brief Main processing loop
     */
    void run();
    
    /**
     * @brief Stop cameras and cleanup
     */
    void stop();
    
private:
    // Camera source
    std::shared_ptr<MultiCameraSource> camera_source;
    
    // Renderer
    std::shared_ptr<SVRenderSimple> renderer;
    
    // Frame storage
    std::array<CamFrame, 4> frames;
    
    // Running state
    bool is_running;
    
    // Warping data (per camera)
    std::vector<cv::cuda::GpuMat> warp_x_maps;  // X coordinate maps
    std::vector<cv::cuda::GpuMat> warp_y_maps;  // Y coordinate maps
    std::vector<cv::Mat> K_matrices;            // Intrinsic matrices
    std::vector<cv::Mat> R_matrices;            // Rotation matrices
    float focal_length;
    float scale_factor;
    
    /**
     * @brief Load calibration from YAML files
     * @param folder Path to calibration folder
     * @return true if successful
     */
    bool loadCalibration(const std::string& folder);
    
    /**
     * @brief Setup spherical warp lookup tables for each camera
     * @return true if successful
     */
    bool setupWarpMaps();
};

#endif // SV_APP_WARPED_HPP
