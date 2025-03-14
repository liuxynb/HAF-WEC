#include "ssd_metrics.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <cstdio>
#include <memory>
#include <array>
#include <stdexcept>
#include <filesystem>
#include <regex>
#include <iomanip>

SSDMetrics::SSDMetrics() : monitoring_active_(false) {}

SSDMetrics::~SSDMetrics() {
    stopPeriodicMonitoring();
}

std::string SSDMetrics::executeCommand(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    
    try {
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        
        if (!pipe) {
            return ""; // Return empty string instead of throwing exception
        }
        
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
    } catch (const std::exception& e) {
        std::cerr << "Command execution failed: " << cmd << " - " << e.what() << std::endl;
        return "";
    }
    
    return result;
}

std::vector<SSDMetrics::DiskInfo> SSDMetrics::discoverSSDs() {
    std::vector<DiskInfo> disks;
    
    // For SATA/SAS SSDs
    try {
        std::string cmd = "lsblk -d -o NAME,MODEL,SIZE,SERIAL -n | grep -v -e '^loop' -e '^sr'";
        std::string result = executeCommand(cmd);
        
        std::istringstream iss(result);
        std::string line;
        
        while (std::getline(iss, line)) {
            std::istringstream line_iss(line);
            DiskInfo info;
            
            std::string name, size;
            line_iss >> name;
            info.device_path = "/dev/" + name;
            
            std::getline(line_iss, info.model, ' ');
            line_iss >> size;
            
            // Safely convert size to bytes with better error handling
            try {
                // Remove any non-digit characters before conversion
                std::string size_digits = "";
                for(char c : size) if(std::isdigit(c)) size_digits += c;
                info.size_bytes = size_digits.empty() ? 0 : std::stoull(size_digits);
            } catch (const std::exception& e) {
                info.size_bytes = 0; // Default to 0 if conversion fails
            }
            
            // Determine if it's an SSD using rotational flag
            std::string rot_cmd = "cat /sys/block/" + name + "/queue/rotational";
            std::string rot_result = executeCommand(rot_cmd);
            
            // Only add if it's an SSD (rotational = 0) or if we can't determine
            if (rot_result.find("0") != std::string::npos || rot_result.empty()) {
                // Try to determine interface type
                std::string smart_cmd = "smartctl -i " + info.device_path + " 2>/dev/null";
                std::string smart_result = executeCommand(smart_cmd);
                
                if (smart_result.find("NVMe") != std::string::npos) {
                    info.interface_type = "NVMe";
                } else if (smart_result.find("SATA") != std::string::npos) {
                    info.interface_type = "SATA";
                } else {
                    info.interface_type = "Unknown";
                }
                
                disks.push_back(info);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error discovering SATA/SAS SSDs: " << e.what() << std::endl;
    }
    
    // For NVMe SSDs - simplified approach
    try {
        // 使用更简单的命令格式，减少解析错误
        std::string cmd = "nvme list 2>/dev/null";
        std::string result = executeCommand(cmd);

        // 逐行解析结果
        std::istringstream iss(result);
        std::string line;
        
        // 跳过标题行
        std::getline(iss, line);
        
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            
            // 提取设备路径 (通常第一个单词是设备名)
            std::istringstream line_iss(line);
            std::string dev_name;
            line_iss >> dev_name;
            
            if (!dev_name.empty()) {
                DiskInfo info;
                info.device_path = dev_name;
                info.model = "NVMe Device";
                info.size_bytes = 0;  // 不尝试解析大小
                info.interface_type = "NVMe";
                disks.push_back(info);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error discovering NVMe SSDs: " << e.what() << std::endl;
    }
    
    // If no drives found, try the direct detection method
    if (disks.empty()) {
        disks = detectDrives();
    }
    
    return disks;
}

std::vector<SSDMetrics::DiskInfo> SSDMetrics::detectDrives() {
    std::vector<DiskInfo> disks;
    
    // 直接检查常见的设备文件
    std::vector<std::string> potential_paths = {
        "/dev/nvme0n1",
        "/dev/nvme1n1",
        "/dev/sda",
        "/dev/sdb",
        "/dev/sdc",
        "/dev/vda"
    };
    
    for (const auto& path : potential_paths) {
        // 检查文件是否存在
        if (std::ifstream(path).good()) {
            DiskInfo info;
            info.device_path = path;
            info.model = path.find("nvme") != std::string::npos ? "NVMe Device" : "SATA/SAS Device";
            info.interface_type = path.find("nvme") != std::string::npos ? "NVMe" : "SATA/SAS";
            info.size_bytes = 0;
            disks.push_back(info);
        }
    }
    
    // 如果仍然没有找到设备，尝试使用lsblk
    if (disks.empty()) {
        std::string cmd = "lsblk -d -n -o NAME | grep -v -e '^loop' -e '^sr'";
        std::string result = executeCommand(cmd);
        
        std::istringstream iss(result);
        std::string line;
        
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            
            DiskInfo info;
            info.device_path = "/dev/" + line;
            info.model = line.find("nvme") != std::string::npos ? "NVMe Device" : "Block Device";
            info.interface_type = line.find("nvme") != std::string::npos ? "NVMe" : "Unknown";
            info.size_bytes = 0;
            disks.push_back(info);
        }
    }
    
    return disks;
}

std::vector<SSDMetrics::DiskInfo> SSDMetrics::discoverBlockDevices() {
    std::vector<DiskInfo> disks;
    
    try {
        // 使用 lsblk 基本命令检测块设备
        std::string cmd = "lsblk -d -n -o NAME,SIZE,MODEL";
        std::string result = executeCommand(cmd);
        
        std::istringstream iss(result);
        std::string line;
        
        while (std::getline(iss, line)) {
            if (line.empty() || line.find("loop") != std::string::npos) continue;
            
            std::istringstream line_iss(line);
            DiskInfo info;
            
            std::string name, size;
            line_iss >> name >> size;
            info.device_path = "/dev/" + name;
            info.size_bytes = 0; // 不进行转换，避免错误
            
            // 获取剩余部分作为模型名称
            std::getline(line_iss, info.model);
            info.interface_type = "Unknown";
            disks.push_back(info);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error discovering block devices: " << e.what() << std::endl;
    }
    
    return disks;
}

std::vector<SSDMetrics::SMARTData> SSDMetrics::getSMARTData(const std::string& device_path) {
    std::string cmd = "smartctl -A " + device_path + " 2>/dev/null";
    std::string result = executeCommand(cmd);
    
    return parseSMARTOutput(result);
}

std::vector<SSDMetrics::SMARTData> SSDMetrics::parseSMARTOutput(const std::string& output) {
    std::vector<SMARTData> smart_data;
    std::istringstream iss(output);
    std::string line;
    
    // Skip header lines
    while (std::getline(iss, line)) {
        if (line.find("ID#") != std::string::npos) {
            break;
        }
    }
    
    // Parse data lines
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        
        std::istringstream line_iss(line);
        SMARTData data;
        
        // Try to parse ID
        if (!(line_iss >> data.id)) {
            continue; // Skip lines that don't start with an ID number
        }
        
        // Skip to attribute name (may contain spaces)
        std::string word;
        if (line_iss >> word) { // First word of attribute name
            data.attribute_name = word;
        } else {
            continue;
        }
        
        // Read rest of attribute name until we hit a number
        while (line_iss >> word) {
            if (std::isdigit(word[0])) {
                try {
                    data.current_value = std::stoi(word);
                    break;
                } catch (...) {
                    // If conversion fails, treat as part of attribute name
                    data.attribute_name += " " + word;
                }
            } else {
                data.attribute_name += " " + word;
            }
        }
        
        // Try to read remaining values
        try {
            if (!(line_iss >> data.worst_value)) data.worst_value = 0;
            if (!(line_iss >> data.threshold)) data.threshold = 0;
            
            // Read raw value (may contain spaces)
            std::string raw;
            std::getline(line_iss, raw);
            data.raw_value = raw;
        } catch (...) {
            // If any parsing fails, use defaults
            data.worst_value = 0;
            data.threshold = 0;
            data.raw_value = "N/A";
        }
        
        smart_data.push_back(data);
    }
    
    return smart_data;
}

std::map<std::string, std::string> SSDMetrics::getNVMeMetrics(const std::string& device_path) {
    std::map<std::string, std::string> metrics;
    
    if (device_path.find("nvme") == std::string::npos) {
        return metrics; // Not an NVMe device
    }
    
    try {
        std::string cmd = "nvme smart-log " + device_path + " 2>/dev/null";
        std::string result = executeCommand(cmd);
        
        return parseNVMeOutput(result);
    } catch (const std::exception& e) {
        std::cerr << "Error getting NVMe metrics: " << e.what() << std::endl;
        return metrics;
    }
}

std::map<std::string, std::string> SSDMetrics::parseNVMeOutput(const std::string& output) {
    std::map<std::string, std::string> metrics;
    std::istringstream iss(output);
    std::string line;
    
    while (std::getline(iss, line)) {
        size_t colon_pos = line.find(":");
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            metrics[key] = value;
        }
    }
    
    return metrics;
}

SSDMetrics::PerformanceMetrics SSDMetrics::getPerformanceMetrics(const std::string& device_path) {
    PerformanceMetrics metrics;
    metrics.timestamp = std::chrono::system_clock::now();
    
    // Get device name without /dev/ prefix
    std::string device_name = device_path;
    if (device_path.find("/dev/") != std::string::npos) {
        device_name = device_path.substr(5);
    }
    
    try {
        // Run iostat for 1 second to get current metrics
        std::string cmd = "iostat -xm " + device_name + " 1 2 2>/dev/null | tail -n 2";
        std::string result = executeCommand(cmd);
        
        // Parse iostat output
        std::istringstream iss(result);
        std::string line;
        
        // Skip to the last line
        while (std::getline(iss, line)) {
            if (!line.empty() && line.find(device_name) != std::string::npos) {
                std::istringstream line_iss(line);
                std::string field;
                
                // Skip device name
                line_iss >> field;
                
                // Read fields in iostat output order:
                // r/s w/s rMB/s wMB/s rrqm/s wrqm/s %rrqm %wrqm r_await w_await aqu-sz rareq-sz wareq-sz svctm %util
                try {
                    line_iss >> metrics.read_iops >> metrics.write_iops
                             >> metrics.read_throughput_mb >> metrics.write_throughput_mb;
                    
                    // Skip some fields
                    for (int i = 0; i < 6; i++) {
                        line_iss >> field;
                    }
                    
                    line_iss >> metrics.read_latency_ms >> metrics.write_latency_ms
                             >> metrics.queue_depth;
                } catch (const std::exception& e) {
                    // If parsing fails, use default values
                    metrics.read_iops = metrics.write_iops = 0;
                    metrics.read_throughput_mb = metrics.write_throughput_mb = 0;
                    metrics.read_latency_ms = metrics.write_latency_ms = 0;
                    metrics.queue_depth = 0;
                }
                
                break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error getting performance metrics: " << e.what() << std::endl;
    }
    
    return metrics;
}

double SSDMetrics::getEstimatedLifeRemaining(const std::string& device_path) {
    double life_remaining = -1.0; // -1 indicates unable to determine
    
    if (device_path.find("nvme") != std::string::npos) {
        try {
            // For NVMe devices
            auto nvme_metrics = getNVMeMetrics(device_path);
            
            // Look for percentage used or available life
            auto it = nvme_metrics.find("Percentage Used");
            if (it != nvme_metrics.end()) {
                try {
                    life_remaining = 100.0 - std::stod(it->second);
                } catch (...) {
                    life_remaining = -1.0;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error getting NVMe life: " << e.what() << std::endl;
        }
    } else {
        try {
            // For SATA SSDs
            auto smart_data = getSMARTData(device_path);
            
            // Look for Media_Wearout_Indicator or similar attributes
            for (const auto& data : smart_data) {
                if (data.attribute_name.find("Media_Wearout_Indicator") != std::string::npos ||
                    data.attribute_name.find("Wear_Leveling_Count") != std::string::npos) {
                    life_remaining = static_cast<double>(data.current_value);
                    break;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error getting SATA SSD life: " << e.what() << std::endl;
        }
    }
    
    return life_remaining;
}

void SSDMetrics::saveMetricsHistory(const std::string& output_file) {
    std::ofstream file(output_file, std::ios::app);
    
    if (!file.is_open()) {
        std::cerr << "Failed to open output file: " << output_file << std::endl;
        return;
    }
    
    // Write header if file is empty
    file.seekp(0, std::ios::end);
    if (file.tellp() == 0) {
        file << "timestamp,device,read_iops,write_iops,read_throughput_mb,write_throughput_mb,"
             << "read_latency_ms,write_latency_ms,queue_depth" << std::endl;
    }
    
    // Write metrics data
    for (const auto& [device, metrics_list] : metrics_history_) {
        for (const auto& metrics : metrics_list) {
            auto time_t = std::chrono::system_clock::to_time_t(metrics.timestamp);
            file << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << ","
                 << device << ","
                 << metrics.read_iops << ","
                 << metrics.write_iops << ","
                 << metrics.read_throughput_mb << ","
                 << metrics.write_throughput_mb << ","
                 << metrics.read_latency_ms << ","
                 << metrics.write_latency_ms << ","
                 << metrics.queue_depth << std::endl;
        }
    }
    
    file.close();
}

void SSDMetrics::startPeriodicMonitoring(const std::string& device_path, 
                                       int interval_seconds,
                                       const std::string& output_file) {
    if (monitoring_active_) {
        stopPeriodicMonitoring();
    }
    
    monitoring_active_ = true;
    
    // Create and start monitoring thread
    std::thread([this, device_path, interval_seconds, output_file]() {
        while (monitoring_active_) {
            try {
                auto metrics = getPerformanceMetrics(device_path);
                
                // Store metrics in history
                metrics_history_[device_path].push_back(metrics);
                
                // Limit history size to prevent excessive memory usage
                if (metrics_history_[device_path].size() > 1000) {
                    metrics_history_[device_path].erase(metrics_history_[device_path].begin());
                }
                
                // Save to file
                saveMetricsHistory(output_file);
                
            } catch (const std::exception& e) {
                std::cerr << "Error in monitoring thread: " << e.what() << std::endl;
            }
            
            // Sleep for specified interval
            std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
        }
    }).detach();
}

void SSDMetrics::stopPeriodicMonitoring() {
    monitoring_active_ = false;
    
    // Give thread time to stop
    std::this_thread::sleep_for(std::chrono::seconds(1));
}