#include "Channel.h"
#include "IPolicy.h"
#include "IDriver.h"
#include "Entry.h"
#include <concurrentqueue\blockingconcurrentqueue.h>
#include <variant>
#include <semaphore>
#include "PanicLogger.h"
#include "StackTrace.h"
#include "GlobalPolicy.h"
#include "../str/String.h"
#include "../Exception.h"

namespace pmon::util::log
{
	namespace
	{
		// command packets that can be put on the entry queue in place of log entries
		// used to control the worker thread. Each packet encodes what functionality
		// to call in the variant visit routine
		struct Packet_
		{
			std::binary_semaphore semaphore{ 0 };
			void WaitUntilProcessed() { semaphore.acquire(); }
		};
		struct AttachDriverPacket_ : public Packet_
		{
			std::shared_ptr<IDriver> pDriver;
			AttachDriverPacket_(std::shared_ptr<IDriver> pDriver) : pDriver{ std::move(pDriver) } {}
			void Process(ChannelInternal_& channel)
			{
				channel.AttachDriver(std::move(pDriver));
				semaphore.release();
			}
		};
		struct AttachPolicyPacket_ : public Packet_
		{
			std::shared_ptr<IPolicy> pPolicy;
			AttachPolicyPacket_(std::shared_ptr<IPolicy> pPolicy) : pPolicy{ std::move(pPolicy) } {}
			void Process(ChannelInternal_& channel)
			{
				channel.AttachPolicy(std::move(pPolicy));
				semaphore.release();
			}
		};
		struct AttachObjectPacket_ : public Packet_
		{
			std::shared_ptr<void> pObject;
			AttachObjectPacket_(std::shared_ptr<void> pObject) : pObject{ std::move(pObject) } {}
			void Process(ChannelInternal_& channel)
			{
				channel.AttachObject(std::move(pObject));
				semaphore.release();
			}
		};
		struct FlushPacket_ : public Packet_
		{
			void Process(ChannelInternal_& channel)
			{
				channel.Flush();
				semaphore.release();
			}
		};
		struct KillPacket_ : public Packet_
		{
			void Process(ChannelInternal_& channel)
			{
				channel.SignalExit();
				semaphore.release();
			}
		};
		struct FlushEntryPointPacket_ : public Packet_
		{
			void Process(ChannelInternal_& channel)
			{
				channel.DisableTraceResolution();
				channel.Flush();
				semaphore.release();
			}
		};
		// aliases for variant / queue typenames
		using QueueElementType_ = std::variant<Entry,
			std::shared_ptr<AttachDriverPacket_>,
			std::shared_ptr<AttachPolicyPacket_>,
			std::shared_ptr<AttachObjectPacket_>,
			std::shared_ptr<FlushPacket_>,
			std::shared_ptr<KillPacket_>,
			std::shared_ptr<FlushEntryPointPacket_>>;
		using QueueType_ = moodycamel::BlockingConcurrentQueue<QueueElementType_>;
		// shortcut to get the underlying queue variant type when given the type-erased pointer
		// (also do some sanity checking here via assert)
		struct QueueAccessor_
		{
			static QueueType_& Access(ChannelInternal_* pChan)
			{
				assert(pChan);
				auto pQueueVoid = pChan->pEntryQueue_.get();
				assert(pQueueVoid);
				return *static_cast<QueueType_*>(pQueueVoid);
			}
		};
		QueueType_& Queue_(ChannelInternal_* pChan)
		{
			return QueueAccessor_::Access(pChan);
		}
		// internal implementation of the channel, with public functions hidden from the external interface
		// but available to packet processing functions to use
		ChannelInternal_::ChannelInternal_(std::vector<std::shared_ptr<IDriver>> driverPtrs)
			:
			driverPtrs_{ std::move(driverPtrs) },
			pEntryQueue_{ std::make_shared<QueueType_>() }
		{
			worker_ = mt::Thread(L"log-chan", [this] {
				try {
					auto visitor = [this](auto& el) {
						// log entry is handled differently than command packets
						using ElementType = std::decay_t<decltype(el)>;
						if constexpr (std::is_same_v<ElementType, Entry>) {
							Entry& entry = el;
							// process all policies, tranforming entry in-place
							for (auto& pPolicy : policyPtrs_) {
								try {
									// if any policy returns false, drop entry
									if (!pPolicy->TransformFilter(entry)) {
										return;
									}
								}
								catch (...) {
									pmlog_panic_(str::ToWide(ReportException()));
								}
							}
							// resolve trace if one is present
							if (entry.pTrace_ && !entry.pTrace_->Resolved()) {
								try {
									if (resolvingTraces_) {
										entry.pTrace_->Resolve();
									}
								}
								catch (...) {
									pmlog_panic_(str::ToWide(ReportException()));
								}
							}
							// submit entry to all drivers (by copy)
							for (auto& pDriver : driverPtrs_) {
								try { pDriver->Submit(entry); }
								catch (...) {
									pmlog_panic_(str::ToWide(ReportException()));
								}
							}
							if (driverPtrs_.empty()) {
								pmlog_panic_(L"No drivers in logging channel while processing entry");
							}
						}
						// if not log entry object, then shared_ptr to a command packet w/ Process member
						else {
							el->Process(*this);
						}
					};
					QueueElementType_ el;
					while (!exiting_) {
						Queue_(this).wait_dequeue(el);
						std::visit(visitor, el);
					}
				}
				catch (...) {
					pmlog_panic_(str::ToWide(ReportException()));
				}
			});
		}
 		ChannelInternal_::~ChannelInternal_() = default;
		void ChannelInternal_::Flush()
		{
			for (auto& pDriver : driverPtrs_) {
				pDriver->Flush();
			}
		}
		void ChannelInternal_::SignalExit()
		{
			exiting_ = true;
		}
		void ChannelInternal_::DisableTraceResolution()
		{
			resolvingTraces_ = false;
		}
		void ChannelInternal_::AttachDriver(std::shared_ptr<IDriver> pDriver)
		{
			driverPtrs_.push_back(std::move(pDriver));
		}
		void ChannelInternal_::AttachPolicy(std::shared_ptr<IPolicy> pPolicy)
		{
			policyPtrs_.push_back(std::move(pPolicy));
		}
		void ChannelInternal_::AttachObject(std::shared_ptr<void> pObj)
		{
			objectPtrs_.push_back(std::move(pObj));
		}
		void ChannelInternal_::EnqueueEntry(Entry&& e)
		{
			Queue_(this).enqueue(std::move(e));
		}
		template<class P, typename ...Args>
		void ChannelInternal_::EnqueuePacketWait(Args&& ...args)
		{
			auto pPacket = std::make_shared<P>(std::forward<Args>(args)...);
			Queue_(this).enqueue(pPacket);
			pPacket->WaitUntilProcessed();
		}
		template<class P, typename ...Args>
		void ChannelInternal_::EnqueuePacketAsync(Args&& ...args)
		{
			Queue_(this).enqueue(std::make_shared<P>(std::forward<Args>(args)...));
		}
	}


	// implementation of external interfaces
	Channel::Channel(std::vector<std::shared_ptr<IDriver>> driverPtrs)
		:
		ChannelInternal_{ std::move(driverPtrs) }
	{}
	Channel::~Channel()
	{
		try {
			EnqueuePacketAsync<KillPacket_>();
		}
		catch (...) {
			pmlog_panic_(L"Failure enqueing kill packet in Channel dtor");
		}
	}
	void Channel::Submit(Entry&& e) noexcept
	{
		try {
			EnqueueEntry(std::move(e));
		}
		catch (...) {
			pmlog_panic_(L"Exception thrown in Channel::Submit");
		}
	}
	void Channel::Flush()
	{
		EnqueuePacketWait<FlushPacket_>();
	}
	void Channel::AttachDriver(std::shared_ptr<IDriver> pDriver)
	{
		EnqueuePacketWait<AttachDriverPacket_>(std::move(pDriver));
	}
	void Channel::AttachPolicy(std::shared_ptr<IPolicy> pPolicy)
	{
		EnqueuePacketWait<AttachPolicyPacket_>(std::move(pPolicy));
	}
	void Channel::AttachObject(std::shared_ptr<void> pObj)
	{
		EnqueuePacketWait<AttachObjectPacket_>(std::move(pObj));
	}
	void Channel::FlushEntryPointExit()
	{
		EnqueuePacketWait<FlushEntryPointPacket_>();
	}
}