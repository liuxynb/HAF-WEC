#include "ssd_metrics.hpp"
#include <iostream>
#include <iomanip>

int main() {
    SSDMetrics metrics;
    
    // 发现系统中的SSD
    std::cout << "发现系统中的SSD设备：\n";
    auto disks = metrics.discoverSSDs();
    bool used_fallback = false;
    
    if (disks.empty()) {
        std::cout << "尝试使用备用方法检测块设备...\n";
        disks = metrics.discoverBlockDevices();
        used_fallback = true;
        
        if (disks.empty()) {
            std::cout << "未找到任何存储设备\n";
            std::cout << "请确保您有权限访问存储设备，可尝试使用 sudo 运行此程序\n";
            return 1;
        }
    }
    
    for (size_t i = 0; i < disks.size(); i++) {
        std::cout << i + 1 << ". " << disks[i].device_path << " - " 
                  << disks[i].model << " (" << disks[i].interface_type << ")\n";
    }
    
    // 选择一个设备进行监控
    size_t choice = 0;
    if (disks.size() > 1) {
        std::cout << "请选择要监控的设备 (1-" << disks.size() << "): ";
        std::cin >> choice;
        
        if (choice < 1 || choice > disks.size()) {
            std::cout << "无效选择\n";
            return 1;
        }
    } else {
        choice = 1;
    }
    
    std::string device_path = disks[choice - 1].device_path;
    std::cout << "监控设备: " << device_path << "\n\n";
    
    // 显示设备信息
    std::cout << "设备信息:\n";
    std::cout << "  型号: " << disks[choice - 1].model << "\n";
    std::cout << "  接口: " << disks[choice - 1].interface_type << "\n";
    if (disks[choice - 1].size_bytes > 0) {
        std::cout << "  容量: " << disks[choice - 1].size_bytes / 1000000000.0 << " GB\n\n";
    } else {
        std::cout << "  容量: 未知\n\n";
    }
    
    if (used_fallback) {
        std::cout << "警告：使用了备用检测方法，某些高级功能可能不可用\n";
        std::cout << "建议安装 smartmontools 和 nvme-cli 工具包以获得完整功能\n\n";
    }
    
    // 获取SMART数据
    std::cout << "SMART数据:\n";
    auto smart_data = metrics.getSMARTData(device_path);
    
    if (smart_data.empty()) {
        std::cout << "  无法获取SMART数据，可能是权限问题或设备不支持\n\n";
    } else {
        std::cout << std::setw(5) << "ID" << " | " 
                << std::setw(30) << "属性" << " | "
                << std::setw(8) << "当前值" << " | "
                << std::setw(8) << "最差值" << " | "
                << std::setw(8) << "阈值" << " | "
                << "原始值\n";
        std::cout << std::string(80, '-') << "\n";
        
        for (const auto& data : smart_data) {
            std::cout << std::setw(5) << data.id << " | " 
                    << std::setw(30) << data.attribute_name << " | "
                    << std::setw(8) << data.current_value << " | "
                    << std::setw(8) << data.worst_value << " | "
                    << std::setw(8) << data.threshold << " | "
                    << data.raw_value << "\n";
        }
    }
    
    // 获取NVMe特定指标（如果适用）
    if (disks[choice - 1].interface_type == "NVMe") {
        std::cout << "\nNVMe指标:\n";
        auto nvme_metrics = metrics.getNVMeMetrics(device_path);
        
        if (nvme_metrics.empty()) {
            std::cout << "  无法获取NVMe指标，可能是权限问题或工具缺失\n";
        } else {
            for (const auto& [key, value] : nvme_metrics) {
                std::cout << "  " << key << ": " << value << "\n";
            }
        }
    }
    
    // 估计剩余寿命
    double life = metrics.getEstimatedLifeRemaining(device_path);
    std::cout << "\n估计剩余寿命: ";
    if (life >= 0) {
        std::cout << life << "%\n";
    } else {
        std::cout << "无法确定\n";
    }
    
    // 获取性能指标
    std::cout << "\n当前性能指标:\n";
    auto perf = metrics.getPerformanceMetrics(device_path);
    
    std::cout << "  读取IOPS: " << perf.read_iops << "\n";
    std::cout << "  写入IOPS: " << perf.write_iops << "\n";
    std::cout << "  读取吞吐量: " << perf.read_throughput_mb << " MB/s\n";
    std::cout << "  写入吞吐量: " << perf.write_throughput_mb << " MB/s\n";
    std::cout << "  读取延迟: " << perf.read_latency_ms << " ms\n";
    std::cout << "  写入延迟: " << perf.write_latency_ms << " ms\n";
    std::cout << "  队列深度: " << perf.queue_depth << "\n";
    
    // 启动周期性监控
    std::cout << "\n启动周期性监控 (10秒间隔)，按Ctrl+C停止...\n";
    metrics.startPeriodicMonitoring(device_path, 10, "ssd_metrics_history.csv");
    
    // 等待用户输入以停止程序
    std::cout << "监控数据正在保存到 ssd_metrics_history.csv\n";
    std::cout << "按Enter键停止监控并退出程序...\n";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();
    
    metrics.stopPeriodicMonitoring();
    std::cout << "监控已停止，程序退出\n";
    
    return 0;
}