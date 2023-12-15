#pragma once
#include "../../PresentMonAPI/PresentMonAPI.h"
#include "Middleware.h"
#include "../../Interprocess/source/Interprocess.h"
#include "../../PresentMonUtils/MemBuffer.h"
#include "../../Streamer/StreamClient.h"
#include <optional>
#include <string>
#include "../../CommonUtilities/source/hash/Hash.h"

namespace pmon::mid
{
	// Used to calculate correct start frame based on metric offset
	struct MetricOffsetData {
		uint64_t queryToFrameDataDelta = 0;
		uint64_t metricOffset = 0;
	};

	struct fpsSwapChainData {
		std::vector<double> displayed_fps;
		std::vector<double> frame_times_ms;
		std::vector<double> gpu_sum_ms;
		std::vector<double> cpu_busy_ms;
		std::vector<double> cpu_wait_ms;
		std::vector<double> display_busy_ms;
		std::vector<double> dropped;
		std::vector<double> allowsTearing;

		uint64_t present_start_0 = 0;             // The first frame's PresentStartTime (qpc)
		uint64_t present_start_n = 0;             // The last frame's PresentStartTime (qpc)
		uint64_t present_stop_0 = 0;             // The first frame's PresentStopTime (qpc)
		uint64_t gpu_duration_0 = 0;             // The first frame's GPUDuration (qpc)
		uint64_t display_n_screen_time = 0;       // The last presented frame's ScreenTime (qpc)
		uint64_t display_0_screen_time = 0;       // The first presented frame's ScreenTime (qpc)
		uint64_t display_1_screen_time = 0;       // The second presented frame's ScreenTime (qpc)
		uint32_t display_count = 0;               // The number of presented frames
		uint32_t num_presents = 0;                // The number of frames
		bool     displayed_0 = false;             // Whether the first frame was displayed
		std::string applicationName;

		// Properties of the most-recent processed frame:
		int32_t sync_interval = 0;
		PM_PRESENT_MODE present_mode = PM_PRESENT_MODE_UNKNOWN;

		// Only used by GetGfxLatencyData():
		std::vector<double> render_latency_ms;
		std::vector<double> display_latency_ms;
		uint64_t render_latency_sum = 0;
		uint64_t display_latency_sum = 0;
	};

	struct DeviceInfo
	{
		PM_DEVICE_VENDOR deviceVendor;
		std::string deviceName;
		uint32_t deviceId;
		std::optional<uint32_t> adapterId;
		std::optional<double> gpuSustainedPowerLimit;
		std::optional<uint64_t> gpuMemorySize;
		std::optional<uint64_t> gpuMemoryMaxBandwidth;
	};

	struct MetricInfo
	{
		// Map of array indices to associated data
		std::unordered_map<uint32_t, std::vector<double>> data;
	};

	class ConcreteMiddleware : public Middleware
	{
	public:
		ConcreteMiddleware(std::optional<std::string> pipeNameOverride = {}, std::optional<std::string> introNsmOverride = {});
		void Speak(char* buffer) const override;
		const PM_INTROSPECTION_ROOT* GetIntrospectionData() override;
		void FreeIntrospectionData(const PM_INTROSPECTION_ROOT* pRoot) override;
		PM_STATUS StartStreaming(uint32_t processId) override;
		PM_STATUS StopStreaming(uint32_t processId) override;
		PM_STATUS SetTelemetryPollingPeriod(uint32_t deviceId, uint32_t timeMs) override;
		PM_DYNAMIC_QUERY* RegisterDynamicQuery(std::span<PM_QUERY_ELEMENT> queryElements, double windowSizeMs, double metricOffsetMs) override;
		void FreeDynamicQuery(const PM_DYNAMIC_QUERY* pQuery) override {}
		void PollDynamicQuery(const PM_DYNAMIC_QUERY* pQuery, uint32_t processId, uint8_t* pBlob, uint32_t* numSwapChains) override;
		void PollStaticQuery(const PM_QUERY_ELEMENT& element, uint32_t processId, uint8_t* pBlob) override;
		PM_FRAME_QUERY* RegisterFrameEventQuery(std::span<PM_QUERY_ELEMENT> queryElements, uint32_t& blobSize) override;
		void FreeFrameEventQuery(const PM_FRAME_QUERY* pQuery) override;
		void ConsumeFrameEvents(const PM_FRAME_QUERY* pQuery, uint32_t processId, uint8_t* pBlob, uint32_t& numFrames) override;
	private:
		struct HandleDeleter {
			void operator()(HANDLE handle) const {
				// Custom deletion logic for HANDLE
				CloseHandle(handle);
			}
		};
		PM_STATUS SendRequest(MemBuffer* requestBuffer);
		PM_STATUS ReadResponse(MemBuffer* responseBuffer);
		PM_STATUS CallPmService(MemBuffer* requestBuffer, MemBuffer* responseBuffer);
		PmNsmFrameData* GetFrameDataStart(StreamClient* client, uint64_t& index, uint64_t dataOffset, uint64_t& queryFrameDataDelta, double& windowSampleSizeMs);
		uint64_t GetAdjustedQpc(uint64_t current_qpc, uint64_t frame_data_qpc, uint64_t queryMetricsOffset, LARGE_INTEGER frequency, uint64_t& queryFrameDataDelta);
		bool DecrementIndex(NamedSharedMem* nsm_view, uint64_t& index);
		PM_STATUS SetActiveGraphicsAdapter(uint32_t adapter_id);
		void GetStaticGpuMetrics();

		void CalculateFpsMetric(fpsSwapChainData& swapChain, const PM_QUERY_ELEMENT& element, uint8_t* pBlob, LARGE_INTEGER qpcFrequency);
		void CalculateGpuCpuMetric(std::unordered_map<PM_METRIC, MetricInfo>& metricInfo, const PM_QUERY_ELEMENT& element, uint8_t* pBlob);
		void CalculateMetric(double& pBlob, std::vector<double>& inData, PM_STAT stat, bool ascending = true);
		double GetPercentile(std::vector<double>& data, double percentile);
		bool GetGpuMetricData(size_t telemetry_item_bit, PresentMonPowerTelemetryInfo& power_telemetry_info, std::unordered_map<PM_METRIC, MetricInfo>& metricInfo);
		bool GetCpuMetricData(size_t telemetryBit, CpuTelemetryInfo& cpuTelemetry, std::unordered_map<PM_METRIC, MetricInfo>& metricInfo);
		void GetCpuInfo();
		std::string GetProcessName(uint32_t processId);

		void CalculateMetrics(const PM_DYNAMIC_QUERY* pQuery, uint32_t processId, uint8_t* pBlob, uint32_t* numSwapChains, LARGE_INTEGER qpcFrequency, std::unordered_map<uint64_t, fpsSwapChainData>& swapChainData, std::unordered_map<PM_METRIC, MetricInfo>& metricInfo);
		void SaveMetricCache(const PM_DYNAMIC_QUERY* pQuery, uint32_t processId, uint8_t* pBlob);
		void CopyMetricCacheToBlob(const PM_DYNAMIC_QUERY* pQuery, uint32_t processId, uint8_t* pBlob);

		std::unique_ptr<void, HandleDeleter> pNamedPipeHandle;
		uint32_t clientProcessId = 0;
		// Stream clients mapping to process id
		std::map<uint32_t, std::unique_ptr<StreamClient>> presentMonStreamClients;
		std::unique_ptr<ipc::MiddlewareComms> pComms;
		// Dynamic query handle to frame data delta
		std::unordered_map<std::pair<PM_DYNAMIC_QUERY*, uint32_t>, uint64_t> queryFrameDataDeltas;
		// Dynamic query handle to cache data
		std::unordered_map<std::pair<PM_DYNAMIC_QUERY*, uint32_t>, std::unique_ptr<uint8_t[]>> cachedMetricDatas;
		std::vector<DeviceInfo> cachedGpuInfo;
		std::vector<DeviceInfo> cachedCpuInfo;
		double cachedGpuMemMaxBandwidth = 0.;
		double cachedGpuMemSize = 0.;
		uint32_t currentGpuInfoIndex = UINT32_MAX;
	};
}