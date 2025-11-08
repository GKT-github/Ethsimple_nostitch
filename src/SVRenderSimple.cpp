#include "SVRenderSimple.hpp"
#include "OGLShader.hpp"
#include "Model.hpp"
#include "Shader.hpp"
#include <GL/gl.h>
#include <GL/glext.h>
#include <GLFW/glfw3.h>
#include <iostream>
// ✅ ADD THESE LINES:
#include <opencv2/cudawarping.hpp>   // For cv::cuda::remap
#include <opencv2/imgproc.hpp>        // For cv::INTER_LINEAR

// Simple quad vertices for texture display
static const float quadVertices[] = {
    // Positions   // TexCoords
    -1.0f,  1.0f,  0.0f, 1.0f,
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f
};

static const unsigned int quadIndices[] = {
    0, 1, 2,
    0, 2, 3
};

// Simple texture shader source
static const char* textureVertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 transform;

void main()
{
    gl_Position = transform * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

static const char* textureFragmentShader = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D texture1;

void main()
{
    FragColor = texture(texture1, TexCoord);
}
)";

SVRenderSimple::SVRenderSimple(int width, int height)
    : screen_width(width)
    , screen_height(height)
    , window(nullptr)
    , quad_VAO(0)
    , quad_VBO(0)
    , texture_shader(nullptr)
    , is_init(false) {
    
    camera_textures.fill(0);
    camera_pbos.fill(0);
}

SVRenderSimple::~SVRenderSimple() {
    if (texture_shader) delete texture_shader;
    
    for (auto tex : camera_textures) {
        if (tex) glDeleteTextures(1, &tex);
    }
    
    for (auto pbo : camera_pbos) {
        if (pbo) glDeleteBuffers(1, &pbo);
    }
    
    if (quad_VAO) glDeleteVertexArrays(1, &quad_VAO);
    if (quad_VBO) glDeleteBuffers(1, &quad_VBO);
    
    if (window) {
        glfwDestroyWindow(window);
        glfwTerminate();
    }
}

bool SVRenderSimple::init(const std::string& car_model_path,
                          const std::string& car_vert_shader,
                          const std::string& car_frag_shader) {
    
    std::cout << "Initializing simplified 4-camera renderer..." << std::endl;
    // std::cout << "=== RENDERER INITIALIZATION ===" << std::endl;
    // std::cout << "Screen dimensions: " << screen_width << " x " << screen_height << std::endl;
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    // Create window
    window = glfwCreateWindow(screen_width, screen_height, 
                             "Surround View - 4 Camera Display", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }
    
    glfwMakeContextCurrent(window);
    
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, screen_width, screen_height);
    
    std::cout << "  ✓ OpenGL context created" << std::endl;
    
    // Setup quad for camera textures
    setupQuad();
    std::cout << "  ✓ Quad geometry created" << std::endl;
    
    // Create texture shader
    createTextureShader();
    std::cout << "  ✓ Texture shader compiled" << std::endl;
    
    // Setup car model
    setupCarModel(car_model_path, car_vert_shader, car_frag_shader);
    std::cout << "  ✓ Car model loaded" << std::endl;
    
    // Create textures and PBOs for 4 cameras
    for (int i = 0; i < 4; i++) {
        // Texture
        glGenTextures(1, &camera_textures[i]);
        glBindTexture(GL_TEXTURE_2D, camera_textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        // PBO
        glGenBuffers(1, &camera_pbos[i]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, camera_pbos[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, 
                     1280 * 800 * 3,  // Camera resolution
                     nullptr, GL_STREAM_DRAW);
    }
    
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    
    std::cout << "  ✓ Camera textures created (4 cameras)" << std::endl;
    
    is_init = true;
    std::cout << "✓ Renderer initialization complete!" << std::endl;
    
    return true;
}

void SVRenderSimple::setupQuad() {
    glGenVertexArrays(1, &quad_VAO);
    glGenBuffers(1, &quad_VBO);
    
    glBindVertexArray(quad_VAO);
    
    glBindBuffer(GL_ARRAY_BUFFER, quad_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    
    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // TexCoord attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 
                         (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);
}

void SVRenderSimple::setupCarModel(const std::string& model_path,
                                   const std::string& vert_shader,
                                   const std::string& frag_shader) {
    std::cout << "Loading 3D car model..." << std::endl;
    std::cout << "  Model: " << model_path << std::endl;
    std::cout << "  Vertex shader: " << vert_shader << std::endl;
    std::cout << "  Fragment shader: " << frag_shader << std::endl;
    
    // Load car model
    try {
        car_model = std::make_unique<Model>(model_path);
        std::cout << "  ✓ Car model loaded successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "  ✗ Failed to load car model: " << e.what() << std::endl;
        return;
    }
    
    // Load car shader
    car_shader = std::make_unique<OGLShader>();
    if (!car_shader->loadFromFile(vert_shader, frag_shader)) {
        std::cerr << "  ✗ Failed to load car shaders, will skip car rendering" << std::endl;
        car_model.reset();
        return;
    }
    std::cout << "  ✓ Car shaders loaded successfully" << std::endl;
    
    // Setup car transform (centered, scaled appropriately)
    car_transform = glm::mat4(1.0f);
    car_transform = glm::translate(car_transform, glm::vec3(0.0f, 2.1f, 0.0f));  // Lower the car
    car_transform = glm::rotate(car_transform, glm::radians(-20.0f), glm::vec3(1.0f, 0.0f, 0.0f));  // Pitch
    car_transform = glm::rotate(car_transform, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    car_transform = glm::scale(car_transform, glm::vec3(0.014f));  // Smaller scale
    std::cout << "  ✓ Car transform configured" << std::endl;
}

void SVRenderSimple::createTextureShader() {
    texture_shader = new OGLShader();
    
    // Compile shaders from strings
    unsigned int vertex, fragment;
    
    // Vertex shader
    vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &textureVertexShader, NULL);
    glCompileShader(vertex);
    
    // Check for errors
    int success;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(vertex, 512, NULL, infoLog);
        std::cerr << "Vertex shader compilation failed:\n" << infoLog << std::endl;
    }
    
    // Fragment shader
    fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &textureFragmentShader, NULL);
    glCompileShader(fragment);
    
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(fragment, 512, NULL, infoLog);
        std::cerr << "Fragment shader compilation failed:\n" << infoLog << std::endl;
    }
    
    // Link program
    texture_shader->ID = glCreateProgram();
    glAttachShader(texture_shader->ID, vertex);
    glAttachShader(texture_shader->ID, fragment);
    glLinkProgram(texture_shader->ID);
    
    glGetProgramiv(texture_shader->ID, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(texture_shader->ID, 512, NULL, infoLog);
        std::cerr << "Shader program linking failed:\n" << infoLog << std::endl;
    }
    
    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

void SVRenderSimple::uploadTexture(const cv::cuda::GpuMat& frame, unsigned int texture_id) {
    if (frame.empty()) return;
    
    // Get PBO index (0-3)
    unsigned int pbo_idx = 0;
    for (int i = 0; i < 4; i++) {
        if (camera_textures[i] == texture_id) {
            pbo_idx = i;
            break;
        }
    }
    // ============================================================
    // FLIP PROCESSING
    // ============================================================
    cv::cuda::GpuMat processed_frame;
    
    if (pbo_idx == 0) {
        // Front camera: Flip horizontally
        // Create flip maps (only once, cache them as static)
        static cv::cuda::GpuMat map_x, map_y;
        static cv::Size last_size(0, 0);

        if (map_x.empty()) {
            cv::Mat cpu_map_x(frame.rows, frame.cols, CV_32F);
            cv::Mat cpu_map_y(frame.rows, frame.cols, CV_32F);
            
            for (int y = 0; y < frame.rows; y++) {
            for (int x = 0; x < frame.cols; x++) {
                cpu_map_x.at<float>(y, x) = x;                    // Keep X same
                cpu_map_y.at<float>(y, x) = frame.rows - 1 - y;  // Flip Y
                }
            }
            
            map_x.upload(cpu_map_x);
            map_y.upload(cpu_map_y);
            last_size = frame.size();
        }
        cv::cuda::remap(frame, processed_frame, map_x, map_y, cv::INTER_LINEAR);
        
    } else if (pbo_idx == 1) {
        // Left camera: Rotate 90° ccounter -clockwise
        static cv::cuda::GpuMat map_x, map_y;
        static cv::Size last_size(0, 0);
        if (map_x.empty()) {
            // Output size will be swapped (width <-> height)
            cv::Mat cpu_map_x(frame.cols, frame.rows, CV_32F);  // Note: swapped!
            cv::Mat cpu_map_y(frame.cols, frame.rows, CV_32F);
            
            for (int y = 0; y < frame.cols; y++) {
                for (int x = 0; x < frame.rows; x++) {
                    cpu_map_x.at<float>(y, x) =  y;  // New X = flipped old Y
                    cpu_map_y.at<float>(y, x) =  x;   // New Y = old X
                }
            }

            
            map_x.upload(cpu_map_x);
            map_y.upload(cpu_map_y);
            last_size = frame.size();
        }
        cv::cuda::remap(frame, processed_frame, map_x, map_y, cv::INTER_LINEAR);
        
    } 
    
    else if (pbo_idx == 3) {
        // Right camera: Rotate 90° clockwise + vertical flip
        static cv::cuda::GpuMat map_x, map_y;
        static cv::Size last_size(0, 0);
        
        if (map_x.empty() || last_size != frame.size()) {
            // Output size will be swapped (width <-> height)
            cv::Mat cpu_map_x(frame.cols, frame.rows, CV_32F);  // Note: swapped!
            cv::Mat cpu_map_y(frame.cols, frame.rows, CV_32F);
            
            for (int y = 0; y < frame.cols; y++) {
                for (int x = 0; x < frame.rows; x++) {
                    // 90° CW + vertical flip
                    cpu_map_x.at<float>(y, x) = frame.cols - 1 - y;  // Flipped old Y
                    cpu_map_y.at<float>(y, x) = frame.rows - 1 - x;  // Flipped old X
                }
            }
            
            map_x.upload(cpu_map_x);
            map_y.upload(cpu_map_y);
            last_size = frame.size();
        }
        cv::cuda::remap(frame, processed_frame, map_x, map_y, cv::INTER_LINEAR);
    }
    else if (pbo_idx == 2) {
    // Rear camera: Flip horizontally
    static cv::cuda::GpuMat map_x, map_y;
    static cv::Size last_size(0, 0);
    
    if (map_x.empty() || last_size != frame.size()) {
        cv::Mat cpu_map_x(frame.rows, frame.cols, CV_32F);
        cv::Mat cpu_map_y(frame.rows, frame.cols, CV_32F);
        
        for (int y = 0; y < frame.rows; y++) {
            for (int x = 0; x < frame.cols; x++) {
                cpu_map_x.at<float>(y, x) = frame.cols - 1 - x;  // Flip X
                cpu_map_y.at<float>(y, x) = y;                    // Keep Y
            }
        }
        
        map_x.upload(cpu_map_x);
        map_y.upload(cpu_map_y);
        last_size = frame.size();
    }
    cv::cuda::remap(frame, processed_frame, map_x, map_y, cv::INTER_LINEAR);
    }
    else {
        // Rear camera (index 2): no transformation
        processed_frame = frame;
    }
    // ============================================================
    
    // Download from GPU to PBO
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, camera_pbos[pbo_idx]);
    void* ptr = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 
                                  processed_frame.cols * processed_frame.rows * 3,  // ✅ FIXED
                                  GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    
    if (ptr) {
        cv::Mat cpu_frame(processed_frame.rows, processed_frame.cols, CV_8UC3, ptr);  // ✅ FIXED
        processed_frame.download(cpu_frame);  // ✅ FIXED
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }
    
    // Upload to texture
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, processed_frame.cols, processed_frame.rows,  // ✅ FIXED
                 0, GL_BGR, GL_UNSIGNED_BYTE, 0);
    
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

// void SVRenderSimple::drawCameraView(unsigned int texture_id, int x, int y, int w, int h) {
//     // Set viewport for this camera
//     glViewport(x, y, w, h);
    
//     // Calculate NDC transform
//     float screen_x = (float)x / screen_width;
//     float screen_y = (float)y / screen_height;
//     float screen_w = (float)w / screen_width;
//     float screen_h = (float)h / screen_height;
    
//     // Create transform matrix (NDC space)
//     glm::mat4 transform = glm::mat4(1.0f);
//     transform = glm::translate(transform, glm::vec3(-1.0f + screen_w, -1.0f + screen_h, 0.0f));
//     transform = glm::scale(transform, glm::vec3(screen_w, screen_h, 1.0f));
    
//     // Use texture shader
//     texture_shader->use();
//     texture_shader->setMat4("transform", transform);
    
//     // Bind texture
//     glActiveTexture(GL_TEXTURE0);
//     glBindTexture(GL_TEXTURE_2D, texture_id);
//     texture_shader->setInt("texture1", 0);
    
//     // Draw quad
//     glBindVertexArray(quad_VAO);
//     glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
//     glBindVertexArray(0);
// }
void SVRenderSimple::drawCameraView(unsigned int texture_id, int x, int y, int w, int h) {
    // Set viewport for this camera
    glViewport(x, y, w, h);
    
    // ============================================================
    // STRETCH TO FILL - No aspect ratio preservation
    // ============================================================
    glm::mat4 transform = glm::mat4(1.0f);  // Identity matrix
    // Quad vertices already cover -1 to +1 NDC space
    // This stretches texture to completely fill viewport
    
    // Use texture shader
    texture_shader->use();
    texture_shader->setMat4("transform", transform);
    
    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    texture_shader->setInt("texture1", 0);
    
    // Draw quad
    glBindVertexArray(quad_VAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

// bool SVRenderSimple::render(const std::array<cv::cuda::GpuMat, 4>& camera_frames) {
//     if (!is_init) return false;
    
//     // Upload all camera textures
//     for (int i = 0; i < 4; i++) {
//         if (!camera_frames[i].empty()) {
//             uploadTexture(camera_frames[i], camera_textures[i]);
//         }
//     }
    
//     // Clear screen
//     glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
//     glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
//     // ============================================================
//     // LARGER LAYOUT - Make cameras fill more screen space
//     // ============================================================
    
//     // Side cameras take 30% width, center area takes 40% width
//     int side_width = screen_width * 0.30;      // 30% for left/right
//     int center_width = screen_width * 0.40;    // 40% for center
    
//     // Vertical: each row gets equal space
//     int row_height = screen_height / 3;        // Each row = 1/3 height
    
//     // Disable depth test for 2D camera views
//     glDisable(GL_DEPTH_TEST);
    
//     // Draw camera views:
//     // Layout (with bigger cameras):
//     //            [Front - BIGGER]
//     // [Left-BIG]    [empty]     [Right-BIG]
//     //            [Rear - BIGGER]
    
//     // Front camera (top center) - BIGGER
//     drawCameraView(camera_textures[0], 
//                    side_width, screen_height * 2 / 3, 
//                    center_width, row_height);
    
//     // Left camera (middle left) - BIGGER
//     drawCameraView(camera_textures[1], 
//                    0, screen_height / 3, 
//                    side_width, row_height);
    
//     // Rear camera (bottom center) - BIGGER
//     drawCameraView(camera_textures[2], 
//                    side_width, 0, 
//                    center_width, row_height);
    
//     // Right camera (middle right) - BIGGER
//     drawCameraView(camera_textures[3], 
//                    side_width + center_width, screen_height / 3, 
//                    side_width, row_height);
    
//     // No car model rendering (disabled)
    
//     // Swap buffers
//     glfwSwapBuffers(window);
//     glfwPollEvents();
    
//     return true;
// }

///////////////////////Test render function to render one full camera output
// bool SVRenderSimple::render(const std::array<cv::cuda::GpuMat, 4>& camera_frames) {
//     if (!is_init) return false;
    
//     // Upload all camera textures
//     for (int i = 0; i < 4; i++) {
//         if (!camera_frames[i].empty()) {
//             uploadTexture(camera_frames[i], camera_textures[i]);
//         }
//     }
    
//     // Clear screen
//     glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
//     glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
//     glDisable(GL_DEPTH_TEST);
    
//     // TEST: Show ONLY front camera FULLSCREEN
//     std::cout << "Drawing fullscreen camera at 0,0 with size " 
//               << screen_width << "x" << screen_height << std::endl;
    
//     drawCameraView(camera_textures[2], 
//                    0, 0,                      // Start at 0,0
//                    screen_width, screen_height);  // Fill entire window
    
//     glfwSwapBuffers(window);
//     glfwPollEvents();
    
//     return true;
// }

// // //////////////////////////test render function to render 2x2 grid 
// bool SVRenderSimple::render(const std::array<cv::cuda::GpuMat, 4>& camera_frames) {
//     if (!is_init) return false;
    
//     // Upload all camera textures
//     for (int i = 0; i < 4; i++) {
//         if (!camera_frames[i].empty()) {
//             uploadTexture(camera_frames[i], camera_textures[i]);
//         }
//     }
    
//     // Clear screen
//     glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
//     glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
//     glDisable(GL_DEPTH_TEST);
    
//     // ============================================================
//     // 2x2 GRID LAYOUT - Each camera gets 1/4 of screen
//     // Screen: 1920x1080 → Each camera: 960x540
//     // ============================================================
//     int half_width = screen_width / 2;   // 960
//     int half_height = screen_height / 2; // 540
    
//     // Layout:
//     // [Front 0,540]  [Right 960,540]
//     // [Left  0,0  ]  [Rear  960,0  ]
    
//     // Front camera (top-left)
//     drawCameraView(camera_textures[0], 
//                    0, half_height,        // x=0, y=540
//                    half_width, half_height); // w=960, h=540
    
//     // Right camera (top-right)
//     drawCameraView(camera_textures[3], 
//                    half_width, half_height,  // x=960, y=540
//                    half_width, half_height); // w=960, h=540
    
//     // Left camera (bottom-left)
//     drawCameraView(camera_textures[1], 
//                    0, 0,                  // x=0, y=0
//                    half_width, half_height); // w=960, h=540
    
//     // Rear camera (bottom-right)
//     drawCameraView(camera_textures[2], 
//                    half_width, 0,         // x=960, y=0
//                    half_width, half_height); // w=960, h=540
    
//     glfwSwapBuffers(window);
//     glfwPollEvents();
    
//     return true;
// }

// ////////////Screen: 1920×1080

// ┌────────────┬──────────────────┬────────────┐
// │            │                  │            │
// │            │  FRONT (0)       │            │
// │            │  768×360         │            │  ← Top row (360px)
// │            │  STRETCHED       │            │
// ├────────────┼──────────────────┼────────────┤
// │ LEFT (1)   │                  │ RIGHT (3)  │
// │ 576×360    │  CAR AREA        │ 576×360    │  ← Middle row (360px)
// │ STRETCHED  │  768×360         │ STRETCHED  │
// ├────────────┼──────────────────┼────────────┤
// │            │  REAR (2)        │            │
// │            │  768×360         │            │  ← Bottom row (360px)
// │            │  STRETCHED       │            │
// └────────────┴──────────────────┴────────────┘
//    576px         768px            576px
//////////////////////////
// COMPLETE REPLACEMENT for the render() function in SVRenderSimple.cpp
// This version draws the car FIRST, then cameras around it

bool SVRenderSimple::render(const std::array<cv::cuda::GpuMat, 4>& camera_frames) {
    if (!is_init) return false;
    
    // Upload all camera textures
    for (int i = 0; i < 4; i++) {
        if (!camera_frames[i].empty()) {
            uploadTexture(camera_frames[i], camera_textures[i]);
        }
    }
    
    // Clear entire screen
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Layout calculations
    int side_width = screen_width * 0.30;    // 576
    int center_width = screen_width * 0.40;  // 768
    int row_height = screen_height / 3;      // 360
    
    // Debug output once
    static bool debug_once = false;
    if (!debug_once) {
        std::cout << "\n=== RENDER LAYOUT DEBUG ===" << std::endl;
        std::cout << "Screen: " << screen_width << "x" << screen_height << std::endl;
        std::cout << "Side width: " << side_width << ", Center width: " << center_width 
                  << ", Row height: " << row_height << std::endl;
        std::cout << "Front cam: (" << side_width << ", " << (screen_height * 2 / 3) 
                  << ") size " << center_width << "x" << row_height << std::endl;
        std::cout << "Left cam: (0, " << row_height << ") size " 
                  << side_width << "x" << row_height << std::endl;
        std::cout << "Rear cam: (" << side_width << ", 0) size " 
                  << center_width << "x" << row_height << std::endl;
        std::cout << "Right cam: (" << (side_width + center_width) << ", " << row_height 
                  << ") size " << side_width << "x" << row_height << std::endl;
        std::cout << "CAR viewport: (" << side_width << ", " << row_height 
                  << ") size " << center_width << "x" << row_height << std::endl;
        std::cout << "Car model ptr: " << (car_model ? "VALID" : "NULL") << std::endl;
        std::cout << "Car shader ptr: " << (car_shader ? "VALID" : "NULL") << std::endl;
        std::cout << "========================\n" << std::endl;
        debug_once = true;
    }
    
    // ============================================================
    // STEP 1: Draw 3D CAR FIRST (in center)
    // ============================================================
    if (car_model && car_shader) {
        std::cout << "Drawing car..." << std::endl;  // This will print every frame
        
        // Set viewport for center
        glViewport(side_width, row_height, center_width, row_height);
        
        // Clear only this viewport
        glEnable(GL_SCISSOR_TEST);
        glScissor(side_width, row_height, center_width, row_height);
        glClearColor(0.2f, 0.2f, 0.3f, 1.0f);  // Blue-ish background to see it clearly
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        
        // Enable 3D rendering
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        
        // Setup camera and projection
        glm::mat4 view = camera.getView();
        glm::mat4 projection = glm::perspective(
            glm::radians(camera.zoom), 
            (float)center_width / row_height, 
            0.1f, 100.0f
        );
        
        // Render car
        car_shader->use();
        car_shader->setMat4("model", car_transform);
        car_shader->setMat4("view", view);
        car_shader->setMat4("projection", projection);
        car_shader->setVec3("lightPos", glm::vec3(5.0f, 10.0f, 5.0f));
        car_shader->setVec3("viewPos", camera.position);
        car_shader->setVec3("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));
        car_shader->setVec3("objectColor", glm::vec3(0.8f, 0.2f, 0.2f));  // Red car
        
        Shader& shader_ref = *reinterpret_cast<Shader*>(car_shader.get());
        car_model->Draw(shader_ref);
        
        std::cout << "Car drawn!" << std::endl;
    } else {
        std::cout << "Car NOT drawn - model=" << (car_model ? "OK" : "NULL") 
                  << " shader=" << (car_shader ? "OK" : "NULL") << std::endl;
    }
    
    // ============================================================
    // STEP 2: Draw cameras AROUND the car
    // ============================================================
    glDisable(GL_DEPTH_TEST);  // Cameras are 2D overlays
    
    // Front camera (top center)
    drawCameraView(camera_textures[0], 
                   side_width, screen_height * 2 / 3,
                   center_width, row_height);
    
    // Left camera (middle left)
    drawCameraView(camera_textures[1], 
                   0, row_height,
                   side_width, row_height);
    
    // Rear camera (bottom center)
    drawCameraView(camera_textures[2], 
                   side_width, 0,
                   center_width, row_height);
    
    // Right camera (middle right)
    drawCameraView(camera_textures[3], 
                   side_width + center_width, row_height,
                   side_width, row_height);
    
    // Restore full viewport
    glViewport(0, 0, screen_width, screen_height);
    
    glfwSwapBuffers(window);
    glfwPollEvents();
    
    return true;
}



bool SVRenderSimple::shouldClose() const {
    return window && glfwWindowShouldClose(window);
}
