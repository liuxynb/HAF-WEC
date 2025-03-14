#ifndef SSD_METRICS_HPP
#define SSD_METRICS_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>

class SSDMetrics {
public:
    struct DiskInfo {
        std::string device_path;
        std::string model;
        std::string serial;
        uint64_t size_bytes;
        std::string interface_type; // SATA, NVMe, etc.
    };

    struct SMARTData {
        int id;
        std::string attribute_name;
        int current_value;
        int worst_value;
        int threshold;
        std::string raw_value;
    };

    struct PerformanceMetrics {
        double read_iops;
        double write_iops;
        double read_throughput_mb;
        double write_throughput_mb;
        double read_latency_ms;
        double write_latency_ms;
        double queue_depth;
        std::chrono::system_clock::time_point timestamp;
    };

    SSDMetrics();
    ~SSDMetrics();

    // Discover all SSDs in the system
    std::vector<DiskInfo> discoverSSDs();

    // Direct drive detection (more reliable)
    std::vector<DiskInfo> detectDrives();
    
    // Fallback method to discover block devices when specific tools are missing
    std::vector<DiskInfo> discoverBlockDevices();

    // Get SMART data for a specific device
    std::vector<SMARTData> getSMARTData(const std::string& device_path);

    // Get NVMe-specific metrics (if applicable)
    std::map<std::string, std::string> getNVMeMetrics(const std::string& device_path);

    // Get real-time performance metrics
    PerformanceMetrics getPerformanceMetrics(const std::string& device_path);

    // Monitor wear leveling and estimated lifetime
    double getEstimatedLifeRemaining(const std::string& device_path);

    // Save metrics to file for trend analysis
    void saveMetricsHistory(const std::string& output_file);

    // Set up periodic monitoring
    void startPeriodicMonitoring(const std::string& device_path, 
                                int interval_seconds,
                                const std::string& output_file);
    
    // Stop periodic monitoring
    void stopPeriodicMonitoring();

private:
    bool monitoring_active_;
    std::map<std::string, std::vector<PerformanceMetrics>> metrics_history_;
    
    // Helper methods to execute system commands
    std::string executeCommand(const std::string& cmd);
    
    // Parse different command outputs
    std::vector<SMARTData> parseSMARTOutput(const std::string& output);
    std::map<std::string, std::string> parseNVMeOutput(const std::string& output);
};

#endif // SSD_METRICS_HPP