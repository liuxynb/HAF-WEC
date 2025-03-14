CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra
LDFLAGS = -lstdc++fs -pthread  # 添加 -pthread 标志

all: ssd_monitor

ssd_monitor: ssd_monitor_example.o ssd_metrics.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

ssd_monitor_example.o: ssd_monitor_example.cpp ssd_metrics.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

ssd_metrics.o: ssd_metrics.cpp ssd_metrics.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f *.o ssd_monitor