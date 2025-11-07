#ifndef SV_APP_SIMPLE_HPP
#define SV_APP_SIMPLE_HPP

#include "SVEthernetCamera.hpp"
#include "SVRenderSimple.hpp"
#include <memory>
#include <array>
#include <string>

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
    
    // Rendering (no stitching!)
    std::shared_ptr<SVRenderSimple> renderer;
    
    // State
    bool is_running;
};

#endif // SV_APP_SIMPLE_HPP
