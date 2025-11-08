#ifndef SV_RENDER_SIMPLE_HPP
#define SV_RENDER_SIMPLE_HPP

#include <opencv2/core/cuda.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <memory>
#include <string>
#include <array>

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
    
private:
    void setupQuad();
    void setupCarModel(const std::string& model_path,
                       const std::string& vert_shader,
                       const std::string& frag_shader);
    void createTextureShader();
    void uploadTexture(const cv::cuda::GpuMat& frame, unsigned int texture_id);
    void drawCameraView(unsigned int texture_id, int x, int y, int w, int h);
    
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
    
    
    bool is_init;
};

#endif // SV_RENDER_SIMPLE_HPP
