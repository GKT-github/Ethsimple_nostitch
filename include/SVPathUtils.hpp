#ifndef SV_PATH_UTILS_HPP
#define SV_PATH_UTILS_HPP

#include <string>
#include <unistd.h>
#include <linux/limits.h>
#include <libgen.h>
#include <sys/stat.h>
#include <iostream>

/**
 * @brief Utility functions for resolving resource paths
 */
class SVPathUtils {
public:
    /**
     * @brief Get the directory containing the executable
     * @return Absolute path to executable directory
     */
    static std::string getExecutableDir() {
        char result[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
        if (count != -1) {
            result[count] = '\0';
            char* dir = dirname(result);
            return std::string(dir);
        }
        return ".";
    }
    
    /**
     * @brief Get the project root directory
     * Assumes executable is in build/ subdirectory
     * @return Absolute path to project root
     */
    static std::string getProjectRoot() {
        std::string exec_dir = getExecutableDir();
        
        // Check if we're in a build directory
        if (exec_dir.find("/build") != std::string::npos) {
            // Go up one level from build/
            return exec_dir.substr(0, exec_dir.rfind("/build"));
        }
        
        // Otherwise, assume executable is in project root
        return exec_dir;
    }
    
    /**
     * @brief Check if a file exists
     * @param path File path to check
     * @return true if file exists
     */
    static bool fileExists(const std::string& path) {
        struct stat buffer;
        return (stat(path.c_str(), &buffer) == 0);
    }
    
    /**
     * @brief Resolve a resource path
     * Tries multiple locations to find the resource
     * @param relative_path Relative path from project root
     * @return Absolute path if found, empty string otherwise
     */
    static std::string resolveResourcePath(const std::string& relative_path) {
        std::string project_root = getProjectRoot();
        
        // Try project root + relative path
        std::string path1 = project_root + "/" + relative_path;
        if (fileExists(path1)) {
            return path1;
        }
        
        // Try current directory + relative path
        std::string path2 = relative_path;
        if (fileExists(path2)) {
            return path2;
        }
        
        // Try executable directory + relative path
        std::string exec_dir = getExecutableDir();
        std::string path3 = exec_dir + "/" + relative_path;
        if (fileExists(path3)) {
            return path3;
        }
        
        return "";
    }
    
    /**
     * @brief Resolve calibration folder path
     * @param user_path User-provided path (may be relative or absolute)
     * @return Resolved absolute path
     */
    static std::string resolveCalibrationPath(const std::string& user_path) {
        // If absolute path, use as-is
        if (user_path[0] == '/') {
            return user_path;
        }
        
        // Try as relative to current directory
        if (fileExists(user_path)) {
            return user_path;
        }
        
        // Try relative to project root
        std::string project_root = getProjectRoot();
        std::string full_path = project_root + "/" + user_path;
        if (fileExists(full_path)) {
            return full_path;
        }
        
        // Return original path and let it fail with good error message
        return user_path;
    }
    
    /**
     * @brief Print debug information about paths
     */
    static void printPathInfo() {
        std::cout << "\n=== Path Resolution Debug Info ===" << std::endl;
        std::cout << "Executable directory: " << getExecutableDir() << std::endl;
        std::cout << "Project root: " << getProjectRoot() << std::endl;
        std::cout << "==================================\n" << std::endl;
    }
};

#endif // SV_PATH_UTILS_HPP
