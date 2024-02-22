// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#include <windows.h>
#include <string>
#include "Console.h"
#include <stdint.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <TlHelp32.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <format>
#include <chrono>
#include "../PresentMonAPI2/source/PresentMonAPI.h"
#include "../PresentMonAPI2/source/Internal.h"
#include "CliOptions.h"

#undef ENABLE_FRAME_DATA_WRITE
#undef ENABLE_PRESENT_MODE
#undef ENABLE_METRICS
#undef ENABLE_ETL
#undef ENABLE_STATIC_QUERIES

using namespace std::chrono;
using namespace std::chrono_literals;

HANDLE gCloseEvent;
bool gQuit = false;
uint32_t gCurrentPid = 0;
std::ofstream g_csv_file;
std::string g_process_name;
std::string g_etl_file_name;
int32_t g_metrics_offset = 0;
bool g_record_frames = false;
const double kWindowSize = 2000.0;
const uint32_t kSleepTime = 4;
const uint32_t kNumFramesInBuf = 1000;
std::vector<char> g_cpu_name;

PM_SESSION_HANDLE g_hSession = nullptr;

    // Main menu actions
enum MenuActions{
  kProcessETL = 1,
  kProcessLive,
  kChurnEvents,
  kQuit
};

int g_menu_action;

void PrintError(PM_STATUS status) {
  std::string s{};
#define PROCESS_VAL(p) \
  case (p):            \
    s = #p;            \
    break;

  switch (status) {
    PROCESS_VAL(PM_STATUS::PM_STATUS_SUCCESS);
    PROCESS_VAL(PM_STATUS::PM_STATUS_NO_DATA);
    PROCESS_VAL(PM_STATUS::PM_STATUS_DATA_LOSS);
    PROCESS_VAL(PM_STATUS::PM_STATUS_SERVICE_ERROR);
    PROCESS_VAL(PM_STATUS::PM_STATUS_INVALID_PID);
    PROCESS_VAL(PM_STATUS::PM_STATUS_INVALID_ETL_FILE);
    PROCESS_VAL(PM_STATUS::PM_STATUS_FAILURE);
  }
#undef PROCESS_VAL
  if (s.length() > 0) {
    ConsolePrintLn(s.c_str());
    CommitConsole();
  }
}

template<typename T>
std::string TranslateOptionalTelemetry(T telemetry_item) {
  std::string data;
  data = (telemetry_item.valid == true) ? std::to_string(telemetry_item.data) : "NA";
  return data;
}

#ifdef ENABLE_FRAME_DATA_WRITE
void WriteToCSV(PM_FRAME_DATA* data) {
  try {
    g_csv_file << "\n";
    g_csv_file << data->application << ",";
    g_csv_file << data->process_id << ",";
    g_csv_file << std::hex << "0x" << data->swap_chain_address << std::dec
               << ",";
    g_csv_file << data->runtime << ",";
    g_csv_file << data->sync_interval << ",";
    g_csv_file << data->present_flags << ",";
    g_csv_file << data->dropped << ",";
    g_csv_file << data->time_in_seconds << ",";
    g_csv_file << data->ms_in_present_api << ",";
    g_csv_file << data->ms_between_presents << ",";
    g_csv_file << data->allows_tearing << ",";
    g_csv_file << data->present_mode << ",";
    g_csv_file << data->ms_until_render_complete << ",";
    g_csv_file << data->ms_until_displayed << ",";
    g_csv_file << data->ms_between_display_change << ",";
    g_csv_file << data->ms_until_render_start << ",";
    g_csv_file << data->ms_gpu_active << ",";
    g_csv_file << std::to_string(data->qpc_time) << ",";
    // power telemetry
    g_csv_file << TranslateOptionalTelemetry(data->gpu_power_w).c_str() << ",";
    g_csv_file << TranslateOptionalTelemetry(data->gpu_sustained_power_limit_w).c_str() << ",";
    g_csv_file << TranslateOptionalTelemetry(data->gpu_voltage_v).c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(data->gpu_frequency_mhz).c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(data->gpu_temperature_c).c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(data->gpu_utilization).c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(
                      data->gpu_render_compute_utilization)
                      .c_str()
               << ",";
    g_csv_file
        << TranslateOptionalTelemetry(data->gpu_media_utilization).c_str()
        << ",";

    for (int i = 0; i < MAX_PM_FAN_COUNT; i++) {
      g_csv_file << TranslateOptionalTelemetry(data->fan_speed_rpm[i]).c_str()
                 << ",";
    }

    g_csv_file << TranslateOptionalTelemetry(data->vram_frequency_mhz).c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(data->vram_effective_frequency_gbs)
                      .c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(data->vram_temperature_c).c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(data->vram_power_w).c_str() << ",";
    g_csv_file << TranslateOptionalTelemetry(data->vram_voltage_v).c_str()
               << ",";

    for (int i = 0; i < MAX_PM_PSU_COUNT; i++) {
      g_csv_file << TranslateOptionalTelemetry(data->psu_type[i]).c_str()
                 << ",";
      g_csv_file << TranslateOptionalTelemetry(data->psu_power[i]).c_str()
                 << ",";
      g_csv_file << TranslateOptionalTelemetry(data->psu_voltage[i]).c_str()
                 << ",";
    }

    // Throttling flags
    g_csv_file << TranslateOptionalTelemetry(data->gpu_power_limited).c_str()
               << ",";
    g_csv_file
        << TranslateOptionalTelemetry(data->gpu_temperature_limited).c_str()
        << ",";
    g_csv_file << TranslateOptionalTelemetry(data->gpu_current_limited).c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(data->gpu_voltage_limited).c_str()
               << ",";
    g_csv_file
        << TranslateOptionalTelemetry(data->gpu_utilization_limited).c_str()
        << ",";

    g_csv_file << TranslateOptionalTelemetry(data->vram_power_limited).c_str()
               << ",";
    g_csv_file
        << TranslateOptionalTelemetry(data->vram_temperature_limited).c_str()
        << ",";
    g_csv_file << TranslateOptionalTelemetry(data->vram_current_limited).c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(data->vram_voltage_limited).c_str()
               << ",";
    g_csv_file
        << TranslateOptionalTelemetry(data->vram_utilization_limited).c_str()
        << ",";

    g_csv_file << TranslateOptionalTelemetry(data->cpu_utilization).c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(data->cpu_power_w).c_str() << ",";
    g_csv_file << TranslateOptionalTelemetry(data->cpu_power_limit_w).c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(data->cpu_temperature_c).c_str()
               << ",";
    g_csv_file << TranslateOptionalTelemetry(data->cpu_frequency).c_str()
               << ",";
  } catch (...) {
    std::cout << "Failed CSV output of frame data.\n";
  }
}

void RecordFrames(bool is_etl) {
  PM_STATUS pmStatus = PM_STATUS::PM_STATUS_SUCCESS;
  uint32_t in_out_num_frames = 1;
  uint32_t frames_recorded = 0;
  PM_FRAME_DATA* out_data = new PM_FRAME_DATA[kNumFramesInBuf];
  if (out_data == nullptr) {
    return;
  }

  // Setup csv file
  time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  tm local_time;
  localtime_s(&local_time, &now);
  std::string csv_filename;
  if (is_etl) {
    std::size_t found = g_etl_file_name.find_last_of("/\\");
    csv_filename = g_etl_file_name.substr(found+1);
  } else {
    csv_filename = g_process_name;
  }

  try {
    csv_filename += "_" + std::to_string(local_time.tm_year + 1900) +
                    std::to_string(local_time.tm_mon + 1) +
                    std::to_string(local_time.tm_mday + 1) +
                    std::to_string(local_time.tm_hour) +
                    std::to_string(local_time.tm_min) +
                    std::to_string(local_time.tm_sec) + ".csv";
  } catch (const std::exception& e) {
    std::cout
        << "a standard exception was caught, with message '"
        << e.what() << "'" << std::endl;
    return;
  }
  try {
    g_csv_file.open(csv_filename);
  } catch (...) {
    std::cout << "Unabled to open csv file" << std::endl;
    return;
  }

  g_csv_file << "Application,ProcessID,SwapChainAddress,Runtime,"
                "SyncInterval,PresentFlags,Dropped,TimeInSeconds,"
                "msInPresentAPI, msBetweenPresents,"
                "AllowsTearing,PresentMode,msUntilRenderComplete,"
                "msUntilDisplayed,msBetweenDisplayChange,msUntilRenderStart,"
                "msGPUActive,QPCTime,GpuPower,GpuSustainedPowerLimit,"
                "GpuVoltage,GpuFrequency,"
                "GpuTemp,GpuUtilization,GpuRenderComputeUtilization,"
                "GpuMediaUtilization,GpuFan[0],GpuFan[1],GpuFan[2],GpuFan[3],"
                "GpuFan[4],VramFrequency,VramEffectiveFreq,VramReadBandwidth,"
                "VramWriteBandwidthCounter,VramTemperature,"
                "VramPower,VramVoltage,"
                "PsuType[0],PsuPower[0],PsuVoltage[0],"
                "PsuType[1],PsuPower[1],PsuVoltage[1],"
                "PsuType[2],PsuPower[2],PsuVoltage[2],"
                "PsuType[3],PsuPower[3],PsuVoltage[3],"
                "PsuType[4],PsuPower[4],PsuVoltage[4],"
                "GpuPowerLimited,"
                "GpuTemperatureLimited,GpuCurrentLimited,"
                "GpuVoltageLimited,GpuUtilizationLimited,VramPowerLimited,"
                "VramTemperatureLimited,VramCurrentLimited,VramVoltageLimited,"
                "VramUtilzationLimited,CpuUtilization,CpuPower,CpuPowerLimit,"
                "CpuTemperature,CpuFrequency";

  for (;;) {
    Sleep(kSleepTime);
    in_out_num_frames = kNumFramesInBuf;
    if (is_etl) {
      pmStatus =
          pmGetEtlFrameData(&in_out_num_frames, out_data);

    } else {
      if (g_menu_action == MenuActions::kProcessLive) {
        pmStatus = pmGetFrameData(gCurrentPid, &in_out_num_frames, out_data);
      } else {
        pmStatus =
            pmGetStreamAllFrameData(&in_out_num_frames, out_data);
      }
    }
    if (pmStatus == PM_STATUS::PM_STATUS_DATA_LOSS) {
      ConsolePrintLn("Data loss occurred during recording. Exiting...");
      break;
    } else if (pmStatus == PM_STATUS::PM_STATUS_PROCESS_NOT_EXIST) {
      if (is_etl) {
        ConsolePrintLn("Finished processing ETL file. Exiting...");
        
      } else {
        ConsolePrintLn("Process has closed. Exiting...");
      }
      break;
    } else if (pmStatus == PM_STATUS::PM_STATUS_NO_DATA) {
      // No frames were recorded, continue along
      continue;
    } else if (pmStatus == PM_STATUS::PM_STATUS_SUCCESS) {
      frames_recorded += in_out_num_frames;
      ConsolePrintLn("Total Frames Recorded: %d", frames_recorded);
      for (uint32_t i = 0; i < in_out_num_frames; i++) {
        WriteToCSV(&out_data[i]);
      }
    } else {
      PrintError(pmStatus);
    }
    CommitConsole();
    if (gQuit) {
      ConsolePrintLn("Exiting recording mode...");
      break;
    }
  }
  CommitConsole();
  delete[] out_data;
}
#endif

#ifdef ENABLE_PRESENT_MODE
void PrintPresentMode(PM_PRESENT_MODE present_mode) {
  switch (present_mode) {
    case PM_PRESENT_MODE::PM_PRESENT_MODE_HARDWARE_LEGACY_FLIP:
      ConsolePrintLn("Hardware: Legacy Flip");
      break;
    case PM_PRESENT_MODE::PM_PRESENT_MODE_HARDWARE_LEGACY_COPY_TO_FRONT_BUFFER:
      ConsolePrintLn("Hardware: Legacy Copy to front buffer");
      break;
    case PM_PRESENT_MODE::PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP:
      ConsolePrintLn("Hardware: Independent Flip");
      break;
    case PM_PRESENT_MODE::PM_PRESENT_MODE_COMPOSED_FLIP:
      ConsolePrintLn("Composed: Flip");
      break;
    case PM_PRESENT_MODE::PM_PRESENT_MODE_HARDWARE_COMPOSED_INDEPENDENT_FLIP:
      ConsolePrintLn("Hardware Composed: Independent Flip");
      break;
    case PM_PRESENT_MODE::PM_PRESENT_MODE_COMPOSED_COPY_WITH_GPU_GDI:
      ConsolePrintLn("Composed: Copy with GPU GDI");
      break;
    case PM_PRESENT_MODE::PM_PRESENT_MODE_COMPOSED_COPY_WITH_CPU_GDI:
      ConsolePrintLn("Composed: Copy with CPU GDI");
      break;
    default:
      ConsolePrintLn("Present Mode: Unknown");
      break;
  }
  return;
}
#endif

#ifdef ENABLE_METRICS
void PrintSwapChainMetrics(PM_FPS_DATA* fps_data, uint32_t num_gfx_swap_chains,
                           PM_GFX_LATENCY_DATA* latency_data,
                           uint32_t num_latency_swap_chains) {
  ConsolePrintLn("pid = %i", gCurrentPid);
  for (uint32_t i = 0; i < num_gfx_swap_chains; i++) {
    ConsolePrintLn("Swapchain = %016llX", fps_data[i].swap_chain);
    ConsolePrintLn("Presented fps Average = %f", fps_data[i].presented_fps.avg);
    ConsolePrintLn("Raw Frametimes = %f ms", fps_data[i].frame_time_ms.raw);
    if (num_gfx_swap_chains == 1) {
      // If we only have a single swap chain print all of the available
      // metrics.
      ConsolePrintLn("Presented fps High = %f FPS", fps_data[i].presented_fps.high);
      ConsolePrintLn("Presented fps Low = %f", fps_data[i].presented_fps.low);
      ConsolePrintLn("Presented fps 90%% = %f", fps_data[i].presented_fps.percentile_90);
      ConsolePrintLn("Presented fps 95%% = %f", fps_data[i].presented_fps.percentile_95);
      ConsolePrintLn("Presented fps 99%% = %f", fps_data[i].presented_fps.percentile_99);
      ConsolePrintLn("Displayed fps Average = %f", fps_data[i].displayed_fps.avg);
      ConsolePrintLn("Displayed fps High = %f", fps_data[i].displayed_fps.high);
      ConsolePrintLn("Displayed fps Low = %f", fps_data[i].displayed_fps.low);
      ConsolePrintLn("Displayed fps 90%% = %f",
                     fps_data[i].displayed_fps.percentile_90);
      ConsolePrintLn("Displayed fps 95%% = %f",
                     fps_data[i].displayed_fps.percentile_95);
      ConsolePrintLn("Displayed fps 99%% = %f",
                     fps_data[i].displayed_fps.percentile_99);
      ConsolePrintLn("GPU busy = %f ms", fps_data[i].gpu_busy.avg);
      ConsolePrintLn("Dropped Frames = %f %", fps_data[i].percent_dropped_frames);
      ConsolePrintLn("Vsync = %d", fps_data[i].sync_interval);
      PrintPresentMode(fps_data[i].present_mode);
    }
    for (uint32_t j = 0; j < num_latency_swap_chains; j++) {
      // Print out the latency data with the matching swapchain if
      // one exists
      if (latency_data[j].swap_chain == fps_data[i].swap_chain) {
        ConsolePrintLn("Render Latency = %f",
                       latency_data[i].render_latency_ms.avg);
        ConsolePrintLn("Display Latency = %f",
                       latency_data[i].display_latency_ms.avg);
      }
    }
  }
}
#endif

void PrintDeviceVendor(char const* vendorLabel, PM_DEVICE_VENDOR deviceVendor) {
    ConsolePrint(vendorLabel);
    switch (deviceVendor) {
    case PM_DEVICE_VENDOR_INTEL:
        ConsolePrintLn("PM_DEVICE_VENDOR_INTEL");
        break;
    case PM_DEVICE_VENDOR_NVIDIA:
        ConsolePrintLn("PM_DEVICE_VENDOR_NVIDIA");
        break;
    case PM_DEVICE_VENDOR_AMD:
        ConsolePrintLn("PM_DEVICE_VENDOR_AMD");
        break;
    case PM_DEVICE_VENDOR_UNKNOWN:
        ConsolePrintLn("PM_DEVICE_VENDOR_UNKNOWN");
        break;
    default:
        ConsolePrintLn("PM_DEVICE_VENDOR_UNKNOWN");
        break;
    }
    return;
}

bool GetUserInput(std::string& input){
  try {
    std::getline(std::cin, input);
    return true;
  } catch (const std::exception& e) {
    std::cout << "a standard exception was caught, with message '" << e.what()
              << "'" << std::endl;
    std::cout << "Exiting SampleClient" << std::endl;
    return false;
  }
} 

void OutputString(const char* output) {
  try {
    std::cout << output;
  } catch (const std::exception& e) {
    std::cout << "a standard exception was caught, with message '" << e.what()
              << "'" << std::endl;
    return;
  }
}

void PrintMetric(char const* format, double metric_data, bool valid) {
  if (valid) {
    ConsolePrintLn(format, metric_data);
  }
}

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
  switch (fdwCtrlType) {
    case CTRL_C_EVENT:
      SetEvent(gCloseEvent);
      gQuit = true;
      return TRUE;
    default:
      return FALSE;
  }
}

void SetRecordFrames() {
  bool valid_selection = false;
  std::string action;

  while (valid_selection == false) {
    OutputString("Select Action:\n");
    OutputString("(1) Display Metrics\n");
    OutputString("(2) Record Frames\n");
    if (GetUserInput(action) == false) {
      gQuit = true;
      return;
    }
    if (action.length() != 0) {
      int action_num = std::stoi(action);
      if (action_num == 1) {
        g_record_frames = false;
        return;
      }
      if (action_num == 2) {
        g_record_frames = true;
        return;
      }
    } else {
      gQuit = true;
    }
  }
}

int32_t DisplayMainMenu() {
  bool valid_selection = false;
  std::string action;
  int action_num = MenuActions::kQuit;

  while (valid_selection == false) {
    OutputString("Set Action:\n");
    OutputString("(1) Process ETL File\n");
    OutputString("(2) Real Time PresentMon for Single Process\n");
    OutputString("(3) Churn Frame Event Data\n");
    OutputString("(4) Quit\n");
    if (GetUserInput(action) == false) {
      break;
    }
    if (action.length() != 0) {
      action_num = std::stoi(action);
      if (action_num >= MenuActions::kProcessETL &&
          action_num <= MenuActions::kQuit) {
        break;
      }
    }
  }
  return action_num;
}

int32_t GetMetricsOffset() {
  bool valid_selection = false;
  std::string action;
  int metrics_offset;

  while (valid_selection == false) {
    OutputString("Set Metrics Offset(ms) (Enter 0 for most recent metrics):");
    if (GetUserInput(action) == true) {
      if (action.length() != 0) {
        // Ensure the metric input is valid
        try {
          metrics_offset = std::stoi(action);
        } catch (std::invalid_argument) {
          OutputString("Invalid offset.\n");
          continue;
        } catch (std::out_of_range) {
          OutputString("Invalid offset.\n");
          continue;
        }
        // Metrics offsets must be positive
        if (metrics_offset < 0) {
          OutputString("Invalid offset.\n");
          continue;
        }
        valid_selection = true;
      }
    }
  }
  return metrics_offset;
}

DWORD FindProcessId(const std::string& process_name) {
  PROCESSENTRY32 process_info;
  process_info.dwSize = sizeof(process_info);

  HANDLE processes_snapshot =
      CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
  if (processes_snapshot == INVALID_HANDLE_VALUE) {
    return 0;
  }

  Process32First(processes_snapshot, &process_info);

  if (strcmp(process_info.szExeFile, process_name.c_str()) == 0) {
    CloseHandle(processes_snapshot);
    return process_info.th32ProcessID;
  }

  while (Process32Next(processes_snapshot, &process_info)) {
    if (strcmp(process_info.szExeFile, process_name.c_str()) == 0) {
      CloseHandle(processes_snapshot);
      return process_info.th32ProcessID;
    }
  }

  CloseHandle(processes_snapshot);
  return 0;
}

#ifdef ENABLE_ETL
void ProcessEtl() {
  bool valid_selection = false;
  PM_STATUS status;

  while (valid_selection == false) {
    OutputString("Enter ETL filename:");
    if (GetUserInput(g_etl_file_name) == true) {
      if (g_etl_file_name.length() != 0) {
        status = pmStartStreamEtl(g_etl_file_name.c_str());
        if (status == PM_STATUS::PM_STATUS_SUCCESS) {
          break;
        }
        OutputString("Invalid ETL file.\n");
      } else {
        OutputString("No ETL file specified, exiting.\n");
        return;
      }
    }
  }
  RecordFrames(true);
}
#endif

void PollMetrics(uint32_t processId, double metricsOffset)
{
    PM_DYNAMIC_QUERY_HANDLE q1 = nullptr;
    PM_QUERY_ELEMENT elements[]{
        PM_QUERY_ELEMENT{.metric = PM_METRIC_APPLICATION, .stat = PM_STAT_NONE, .deviceId = 0, .arrayIndex = 0},
        PM_QUERY_ELEMENT{.metric = PM_METRIC_GPU_VENDOR, .stat = PM_STAT_MID_POINT, .deviceId = 0, .arrayIndex = 0},
        PM_QUERY_ELEMENT{.metric = PM_METRIC_GPU_NAME, .stat = PM_STAT_NONE, .deviceId = 0, .arrayIndex = 0},
        PM_QUERY_ELEMENT{.metric = PM_METRIC_CPU_VENDOR, .stat = PM_STAT_MID_POINT, .deviceId = 0, .arrayIndex = 0},
        PM_QUERY_ELEMENT{.metric = PM_METRIC_CPU_NAME, .stat = PM_STAT_NONE, .deviceId = 0, .arrayIndex = 0},
        PM_QUERY_ELEMENT{.metric = PM_METRIC_GPU_SUSTAINED_POWER_LIMIT, .stat = PM_STAT_MID_POINT, .deviceId = 1, .arrayIndex = 0},
        PM_QUERY_ELEMENT{.metric = PM_METRIC_GPU_MEM_SIZE, .stat = PM_STAT_NONE, .deviceId = 0, .arrayIndex = 1},
        PM_QUERY_ELEMENT{.metric = PM_METRIC_GPU_MEM_MAX_BANDWIDTH, .stat = PM_STAT_AVG, .deviceId = 1, .arrayIndex = 0},
        PM_QUERY_ELEMENT{.metric = PM_METRIC_CPU_POWER_LIMIT, .stat = PM_STAT_NONE, .deviceId = 0, .arrayIndex = 0}
    };
    if (auto result = pmRegisterDynamicQuery(g_hSession, &q1, elements, std::size(elements), 2000., metricsOffset); result != PM_STATUS_SUCCESS)
    {
        ConsolePrintLn("Invalid dynamic query specified!");
    }

    auto pBlob = std::make_unique<uint8_t[]>(elements[std::size(elements) - 1].dataOffset + elements[std::size(elements) - 1].dataSize);
    uint32_t numSwapChains = 1;


    PM_QUERY_ELEMENT staticQueryElement{ .metric = PM_METRIC_APPLICATION, .deviceId = 0, .arrayIndex = 0 };
    auto processName = std::make_unique<uint8_t[]>(260);
    pmPollStaticQuery(g_hSession, &staticQueryElement, processId, processName.get());
    staticQueryElement = { .metric = PM_METRIC_CPU_VENDOR, .deviceId = 0, .arrayIndex = 0 };
    auto cpuVendor = std::make_unique<uint8_t[]>(4);
    pmPollStaticQuery(g_hSession, &staticQueryElement, processId, cpuVendor.get());
    staticQueryElement = { .metric = PM_METRIC_CPU_NAME, .deviceId = 0, .arrayIndex = 0 };
    auto cpuName = std::make_unique<uint8_t[]>(260);
    pmPollStaticQuery(g_hSession, &staticQueryElement, processId, cpuName.get());
    staticQueryElement = { .metric = PM_METRIC_GPU_VENDOR, .deviceId = 1, .arrayIndex = 0 };
    auto gpuVendor = std::make_unique<uint8_t[]>(4);
    pmPollStaticQuery(g_hSession, &staticQueryElement, processId, gpuVendor.get());
    staticQueryElement = { .metric = PM_METRIC_GPU_NAME, .deviceId = 1, .arrayIndex = 0 };
    auto gpuName = std::make_unique<uint8_t[]>(260);
    pmPollStaticQuery(g_hSession, &staticQueryElement, processId, gpuName.get());
    staticQueryElement = { .metric = PM_METRIC_GPU_MEM_MAX_BANDWIDTH, .deviceId = 1, .arrayIndex = 0 };
    auto gpuMemMaxBw = std::make_unique<uint8_t[]>(8);
    pmPollStaticQuery(g_hSession, &staticQueryElement, processId, gpuMemMaxBw.get());
    staticQueryElement = { .metric = PM_METRIC_GPU_MEM_SIZE, .deviceId = 1, .arrayIndex = 0 };
    auto gpuMemSize = std::make_unique<uint8_t[]>(8);
    pmPollStaticQuery(g_hSession, &staticQueryElement, processId, gpuMemSize.get());
    staticQueryElement = { .metric = PM_METRIC_GPU_SUSTAINED_POWER_LIMIT, .deviceId = 1, .arrayIndex = 0 };
    auto gpuSustainedPowerLimit = std::make_unique<uint8_t[]>(8);
    pmPollStaticQuery(g_hSession, &staticQueryElement, processId, gpuSustainedPowerLimit.get());

    int numIterations = 0;
    for (;;)
    {
        PM_STATUS status;
        status = pmPollDynamicQuery(q1, processId, pBlob.get(), &numSwapChains);
        
        if (status == PM_STATUS_SUCCESS)
        {
            ConsolePrintLn("Static Process Name = %s", reinterpret_cast<char*>(processName.get()));
            PrintDeviceVendor("Static CPU Vendor = ", reinterpret_cast<PM_DEVICE_VENDOR&>(cpuVendor[0]));
            ConsolePrintLn("Static CPU Name = %s", reinterpret_cast<char*>(cpuName.get()));
            PrintDeviceVendor("Static GPU Vendor = ", reinterpret_cast<PM_DEVICE_VENDOR&>(gpuVendor[0]));
            ConsolePrintLn("Static GPU Name = %s", reinterpret_cast<char*>(gpuName.get()));
            PrintMetric("Static GPU Memory Max Bandwidth = %f", reinterpret_cast<double&>(gpuMemMaxBw[0]), true);
            ConsolePrintLn("Static GPU Memory Size = = %lld", reinterpret_cast<uint64_t&>(gpuMemSize[0]));

            CommitConsole();
        }

        if (gQuit == true) {
            break;
        }

        Sleep(kSleepTime);
    }

    pmFreeDynamicQuery(q1);

    return;
}

int main(int argc, char* argv[])
{
	if (auto e = clio::Options::Init(argc, argv)) {
		return *e;
	}
	auto& opt = clio::Options::Get();
	// validate options, better to do this with CLI11 validation but framework needs upgrade...
	if (bool(opt.controlPipe) != bool(opt.introNsm)) {
		OutputString("Must set both control pipe and intro NSM, or neither.\n");
		return -1;
	}

	bool streamingStarted = false;

	// finer granularity sleeps
	if (timeBeginPeriod(kSleepTime) != TIMERR_NOERROR) {
		OutputString("Not able to set the Windows sleep() resolution\n");
	}

	if (InitializeConsole() == false) {
		OutputString("\nFailed to initialize console.\n");
		return -1;
	}

	g_menu_action = DisplayMainMenu();
	if (g_menu_action == kQuit) {
		return 0;
	}

	PM_STATUS pmStatus{};
	try {
		if (opt.controlPipe) {
			pmStatus = pmOpenSession_(&g_hSession, (*opt.controlPipe).c_str(), (*opt.introNsm).c_str());
		}
		else {
			pmStatus = pmOpenSession(&g_hSession);
		}
		if (pmStatus != PM_STATUS::PM_STATUS_SUCCESS) {
			PrintError(pmStatus);
			return -1;
		}
	}
	catch (const std::bad_array_new_length& e) {
		std::cout
			<< "pmOpenSession caused bad array new length exception, with message '"
			<< e.what() << "'" << std::endl;
	}
	catch (const std::runtime_error& e) {
		std::cout
			<< "pmOpenSession caused std::runtime exception '"
			<< e.what() << "'" << std::endl;
	}

	if (g_menu_action == MenuActions::kProcessETL) {
#ifdef ENABLE_ETL
		ProcessEtl();
#endif // ENABLE_ETL
	}
	else if (g_menu_action == MenuActions::kProcessLive) {
		g_metrics_offset = GetMetricsOffset();
		gQuit = false;
		while (streamingStarted == false) {
			OutputString("Enter Process Name to monitor: \n");
			if (GetUserInput(g_process_name) == false) {
				return 0;
			}
			if (g_process_name.length() == 0) {
				pmCloseSession(g_hSession);
				return 0;
			}
			gCurrentPid = FindProcessId(g_process_name);
			if (gCurrentPid != 0) {
				SetRecordFrames();
				try {
					pmStatus = pmStartTrackingProcess(g_hSession, gCurrentPid);
					if (pmStatus == PM_STATUS::PM_STATUS_SUCCESS) {
						streamingStarted = true;
					}
					else {
						OutputString("Process Name Not Found.\n");
					}
				}
				catch (...) {
					pmStatus = PM_STATUS::PM_STATUS_FAILURE;
					OutputString("Unable to start stream\n");
				}
			}
		}

#ifdef ENABLE_STATIC_QUERIES
		uint32_t cpu_name_size = 0;
		pmStatus = pmGetCpuName(nullptr, &cpu_name_size);
		if (cpu_name_size != 0) {
			g_cpu_name.resize(cpu_name_size);
			pmStatus = pmGetCpuName(g_cpu_name.data(), &cpu_name_size);
		}
#endif

		std::string status_string;
		try {
			status_string = std::format("Process Name: {}\n", g_process_name);
			OutputString(status_string.c_str());
		}
		catch (...) {
			OutputString("Process Name: Unknown\n");
		}
		try {
			status_string = std::format("Monitoring Process Id: {}\n", gCurrentPid);
			OutputString(status_string.c_str());
		}
		catch (...) {
			OutputString("Process Id: Unknown\n");
		}

		std::cout << "Hit Ctrl-C to exit application." << std::endl;

		// Create an event
		gCloseEvent = CreateEvent(NULL,   // default security attributes
			TRUE,   // manual reset event
			FALSE,  // not signaled
			NULL);  // no name

		if (SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
			// Start metrics capture thread
			std::thread pollMetricsThread(PollMetrics, gCurrentPid, g_metrics_offset);

			// Wait for the metrics capture thread to finish
			pollMetricsThread.join();
	    }
    }
    else if (g_menu_action == MenuActions::kChurnEvents) {
        gQuit = false;
        while (streamingStarted == false) {
            OutputString("Enter Process Name to monitor: \n");
            if (GetUserInput(g_process_name) == false) {
                return 0;
            }
            if (g_process_name.length() == 0) {
                pmCloseSession(g_hSession);
                return 0;
            }
            gCurrentPid = FindProcessId(g_process_name);
            if (gCurrentPid != 0) {
                try {
                    pmStatus = pmStartTrackingProcess(g_hSession, gCurrentPid);
                    if (pmStatus == PM_STATUS::PM_STATUS_SUCCESS) {
                        streamingStarted = true;
                    }
                    else {
                        OutputString("Process Name Not Found.\n");
                    }
                }
                catch (...) {
                    pmStatus = PM_STATUS::PM_STATUS_FAILURE;
                    OutputString("Unable to start stream\n");
                }
            }
        }

        std::string status_string;
        try {
            status_string = std::format("Process Name: {}\n", g_process_name);
            OutputString(status_string.c_str());
        }
        catch (...) {
            OutputString("Process Name: Unknown\n");
        }
        try {
            status_string = std::format("Monitoring Process Id: {}\n", gCurrentPid);
            OutputString(status_string.c_str());
        }
        catch (...) {
            OutputString("Process Id: Unknown\n");
        }

        std::cout << "Hit Ctrl-C to exit application." << std::endl;


        PM_QUERY_ELEMENT queryElements[]{
            { PM_METRIC_GPU_POWER, PM_STAT_NONE, 1, 0 },
            { PM_METRIC_PRESENT_MODE, PM_STAT_NONE, 0, 0 },
            { PM_METRIC_CPU_FRAME_QPC, PM_STAT_NONE, 0, 0 },
        };

        PM_FRAME_QUERY_HANDLE hEventQuery = nullptr;
        uint32_t blobSize = 0;
        pmRegisterFrameQuery(g_hSession, &hEventQuery, queryElements, std::size(queryElements), &blobSize);
        constexpr uint32_t maxFrames = 50;
        auto pBlobs = std::make_unique<uint8_t[]>(blobSize * maxFrames);

        while (true) {
            std::cout << "Checking for new frames...\n";
            uint32_t numFrames = maxFrames;
            pmConsumeFrames(hEventQuery, gCurrentPid, pBlobs.get(), &numFrames);
            if (numFrames == 0) {
                std::cout << "No frames pending, waiting ~200ms\n";
                std::this_thread::sleep_for(200ms);
            }
            else {
                std::cout << std::format("Dumping [{}] frames...\n", numFrames);
                const auto* pBlob = pBlobs.get();
                for (auto i = 0u; i < numFrames; i++) {
                    const auto gpuPower     = *reinterpret_cast<const double*>(&pBlob[0]);
                    const auto presentMode  = *reinterpret_cast<const PM_PRESENT_MODE*>(&pBlob[8]);
                    const auto presentQpc   = *reinterpret_cast<const uint64_t*>(&pBlob[12]);
                    std::cout << std::format("GPWR: {} PMOD: {} PQPC: {}\n",
                        gpuPower, (int)presentMode, presentQpc
                    );
                    pBlob += blobSize;
                }
            }
        }
    }

	pmStopTrackingProcess(g_hSession, gCurrentPid);
	pmCloseSession(g_hSession);

	try {
		g_csv_file.close();
	}
	catch (...) {
		std::cout << "Unabled to close csv file" << std::endl;
	}

	return 0;
}