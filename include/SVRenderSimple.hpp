#ifndef SV_RENDER_SIMPLE_HPP
#define SV_RENDER_SIMPLE_HPP

#include <opencv2/core/cuda.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <memory>
#include <string>
#include <array>
#include "SVConfig.hpp"


// Forward declarations to avoid full includes
class OGLShader;
class Model;
class Shader;

/**
 * @brief Simple fixed-view camera
 */
struct Camera {
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    float zoom;
    
    Camera() : position(0.0f, 5.0f, 10.0f),
               front(0.0f, -0.3f, -1.0f),
               up(0.0f, 1.0f, 0.0f),
               zoom(45.0f) {}
    
    glm::mat4 getView() const {
        return glm::lookAt(position, position + front, up);
    }
};

/**
 * @brief Simplified 4-Camera Display Renderer
 * 
 * Layout:
 *     [Front Camera]
 * [Left] [Car] [Right]
 *     [Rear Camera]
 */
class SVRenderSimple {
public:
    SVRenderSimple(int width, int height);
    ~SVRenderSimple();
    
    /**
     * @brief Initialize renderer
     * @param car_model_path Path to 3D car model (.obj)
     * @param car_vert_shader Car vertex shader path
     * @param car_frag_shader Car fragment shader path
     * @return true if initialization successful
     */
    bool init(const std::string& car_model_path,
              const std::string& car_vert_shader,
              const std::string& car_frag_shader);
    
    /**
     * @brief Render frame with 4 camera views around car
     * @param camera_frames Array of 4 camera frames [Front, Left, Rear, Right]
     * @return true if successful
     */
    bool render(const std::array<cv::cuda::GpuMat, 4>& camera_frames);
    
    /**
     * @brief Check if window should close
     */
    bool shouldClose() const;
    
    #ifdef EN_RENDER_STITCH
        /**
         * @brief Render split-screen view (50% normal + 50% stitched)
         * @param camera_frames Array of 4 camera frames (warped)
         * @param stitched_frame Stitched output frame
         * @return true if successful
         */
        bool renderSplitScreen(const std::array<cv::cuda::GpuMat, 4>& camera_frames,
                            const cv::cuda::GpuMat& stitched_frame);
        
        /**
         * @brief Render split-viewport layout (left half: 3D car + 4 viewports, right half: stitched/black)
         * @param camera_frames Array of 4 camera frames (warped)
         * @param show_right Whether to show stitched output on right half
         * @param stitched_frame Optional stitched frame (nullptr = black screen)
         * @return true if successful
         */
        bool renderSplitViewportLayout(const std::array<cv::cuda::GpuMat, 4>& camera_frames,
                                       bool show_right = false,
                                       const cv::cuda::GpuMat* stitched_frame = nullptr);
        
        /**
         * @brief Get GLFW window pointer (for keyboard input)
         */
        GLFWwindow* getWindow() const { return window; }
    #endif

private:
    void setupQuad();
    void setupCarModel(const std::string& model_path,
                       const std::string& vert_shader,
                       const std::string& frag_shader);
    void createTextureShader();
    void uploadTexture(const cv::cuda::GpuMat& frame, unsigned int texture_id);
    #ifdef RENDER_NOPRESERVE_AS
    void drawCameraView(unsigned int texture_id, int x, int y, int w, int h);
    #endif
    #ifdef RENDER_PRESERVE_AS
    void drawCameraViewWithAspect(GLuint texture, 
                                   int region_x, int region_y, 
                                   int region_w, int region_h,
                                   float texture_aspect);
    #endif
    
    // Window
    GLFWwindow* window;
    int screen_width;
    int screen_height;
    
    // Camera
    Camera camera;
    
    // Car model
    std::unique_ptr<Model> car_model;
    std::unique_ptr<OGLShader> car_shader;
    glm::mat4 car_transform;
    
    // Quad for displaying camera textures
    unsigned int quad_VAO;
    unsigned int quad_VBO;
    OGLShader* texture_shader;
    
    // Camera textures (Front, Left, Rear, Right)
    std::array<unsigned int, 4> camera_textures;
    std::array<unsigned int, 4> camera_pbos;
    
    // Camera frame dimensions (may be scaled)
    int camera_frame_width;
    int camera_frame_height;
    
    bool is_init;
};

#endif // SV_RENDER_SIMPLE_HPP
