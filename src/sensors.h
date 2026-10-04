#pragma once

// Hardware sensors for the HUD. Nothing is opened or polled for metrics that are switched off;
// SensorsShutdown() releases everything (called when the HUD is hidden).
struct SensorWant {
    bool cpuLoad = false, cpuTemp = false, gpuLoad = false, gpuTemp = false, vram = false;
    bool any() const { return cpuLoad || cpuTemp || gpuLoad || gpuTemp || vram; }
};

// -1 means "not available".
struct SensorData {
    double cpuLoad = -1;      // %
    double cpuTemp = -1;      // deg C
    double gpuLoad = -1;      // %
    double gpuTemp = -1;      // deg C
    double vramUsedMB = -1;
    double vramTotalMB = -1;
};

void SensorsPoll(const SensorWant& want, SensorData& out);
void SensorsShutdown();
