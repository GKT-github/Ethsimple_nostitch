#include "SVRenderSimple.hpp"
#include "OGLShader.hpp"
#include "Model.hpp"
#include "Shader.hpp"
#include <GL/gl.h>
#include <GL/glext.h>
#include <GLFW/glfw3.h>
#include <iostream>

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
                                   return;
    // Load car model
    car_model = std::make_unique<Model>(model_path);
    
    // Load car shader
    car_shader = std::make_unique<OGLShader>();
    if (!car_shader->loadFromFile(vert_shader, frag_shader)) {
        std::cerr << "Warning: Failed to load car shaders, will skip car rendering" << std::endl;
        car_model.reset();
        return;
    }
    
    // Setup car transform (centered, scaled appropriately)
    car_transform = glm::mat4(1.0f);
    car_transform = glm::translate(car_transform, glm::vec3(0.0f, 0.0f, 0.0f));
    car_transform = glm::rotate(car_transform, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    car_transform = glm::rotate(car_transform, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    car_transform = glm::scale(car_transform, glm::vec3(0.003f));
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
    
    // Download from GPU to PBO
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, camera_pbos[pbo_idx]);
    void* ptr = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 
                                  frame.cols * frame.rows * 3,
                                  GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    
    if (ptr) {
        cv::Mat cpu_frame(frame.rows, frame.cols, CV_8UC3, ptr);
        frame.download(cpu_frame);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }
    
    // Upload to texture
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frame.cols, frame.rows,
                 0, GL_BGR, GL_UNSIGNED_BYTE, 0);
    
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void SVRenderSimple::drawCameraView(unsigned int texture_id, int x, int y, int w, int h) {
    // Set viewport for this camera
    glViewport(x, y, w, h);
    
    // Calculate NDC transform
    float screen_x = (float)x / screen_width;
    float screen_y = (float)y / screen_height;
    float screen_w = (float)w / screen_width;
    float screen_h = (float)h / screen_height;
    
    // Create transform matrix (NDC space)
    glm::mat4 transform = glm::mat4(1.0f);
    transform = glm::translate(transform, glm::vec3(-1.0f + screen_w, -1.0f + screen_h, 0.0f));
    transform = glm::scale(transform, glm::vec3(screen_w, screen_h, 1.0f));
    
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

bool SVRenderSimple::render(const std::array<cv::cuda::GpuMat, 4>& camera_frames) {
    if (!is_init) return false;
    
    // Upload all camera textures
    for (int i = 0; i < 4; i++) {
        if (!camera_frames[i].empty()) {
            uploadTexture(camera_frames[i], camera_textures[i]);
        }
    }
    
    // Clear screen
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Calculate layout dimensions
    int cam_width = screen_width / 4;
    int cam_height = screen_height / 3;
    int center_width = screen_width / 2;
    int center_height = screen_height / 3;
    
    // Disable depth test for 2D camera views
    glDisable(GL_DEPTH_TEST);
    
    // Draw camera views:
    // Layout:
    //     [Front - top center]
    // [Left] [Car - center] [Right]
    //     [Rear - bottom center]
    
    // Front camera (top center)
    drawCameraView(camera_textures[0], 
                   screen_width / 4, screen_height * 2 / 3, 
                   center_width, cam_height);
    
    // Left camera (middle left)
    drawCameraView(camera_textures[1], 
                   0, screen_height / 3, 
                   cam_width, center_height);
    
    // Rear camera (bottom center)
    drawCameraView(camera_textures[2], 
                   screen_width / 4, 0, 
                   center_width, cam_height);
    
    // Right camera (middle right)
    drawCameraView(camera_textures[3], 
                   screen_width * 3 / 4, screen_height / 3, 
                   cam_width, center_height);
    
    // Draw 3D car in center
    glEnable(GL_DEPTH_TEST);
    glViewport(screen_width / 4, screen_height / 3, center_width, center_height);
    
    if (car_model && car_shader) {
        glm::mat4 view = camera.getView();
        glm::mat4 projection = glm::perspective(glm::radians(camera.zoom), 
                                               (float)center_width / center_height, 
                                               0.1f, 100.0f);
        
        car_shader->use();
        car_shader->setMat4("model", car_transform);
        car_shader->setMat4("view", view);
        car_shader->setMat4("projection", projection);
        car_shader->setVec3("lightPos", glm::vec3(5.0f, 10.0f, 5.0f));
        car_shader->setVec3("viewPos", camera.position);
        car_shader->setVec3("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));
        car_shader->setVec3("objectColor", glm::vec3(0.2f, 0.2f, 0.8f));
        
        Shader& shader_ref = *reinterpret_cast<Shader*>(car_shader.get());
        car_model->Draw(shader_ref);
    }
    
    // Restore full viewport
    glViewport(0, 0, screen_width, screen_height);
    
    // Swap buffers
    glfwSwapBuffers(window);
    glfwPollEvents();
    
    return true;
}

bool SVRenderSimple::shouldClose() const {
    return window && glfwWindowShouldClose(window);
}
