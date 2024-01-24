#pragma once
#include "../../PresentMonAPI2/source/PresentMonAPI.h"
#include "../../PresentMonAPIWrapperCommon/source/Introspection.h"
#include <format>
#include <string>
#include <memory>
#include "BlobContainer.h"

namespace pmapi
{
    class DynamicQuery
    {
        friend class Session;
    public:
        DynamicQuery() = default;
        ~DynamicQuery() { Reset(); }
        DynamicQuery(DynamicQuery&& other) noexcept
        {
            *this = std::move(other);
        }
        DynamicQuery& operator=(DynamicQuery&& rhs) noexcept
        {
            hQuery_ = rhs.hQuery_;
            blobSize_ = rhs.blobSize_;
            rhs.Clear_();
            return *this;
        }
        size_t GetBlobSize() const
        {
            return blobSize_;
        }
        void Poll(uint32_t pid, uint8_t* pBlob, uint32_t& numSwapChains) const
        {
            if (auto sta = pmPollDynamicQuery(hQuery_, pid, pBlob, &numSwapChains); sta != PM_STATUS_SUCCESS) {
                throw Exception{ std::format("dynamic poll call failed with error id={}", (int)sta) };
            }
        }
        void Poll(uint32_t pid, BlobContainer& blobs) const
        {
            assert(!Empty());
            assert(blobs.CheckHandle(hQuery_));
            Poll(pid, blobs.GetFirst(), blobs.AcquireNumBlobsInRef_());
        }
        BlobContainer MakeBlobContainer(uint32_t nBlobs) const
        {
            assert(!Empty());
            return { hQuery_, blobSize_, nBlobs };
        }
        void Reset()
        {
            if (!Empty()) {
                // TODO: report error noexcept
                pmFreeDynamicQuery(hQuery_);
            }
            Clear_();
        }
        bool Empty() const
        {
            return hQuery_ == nullptr;
        }
        operator bool() const { return !Empty(); }
    private:
        // function
        DynamicQuery(PM_SESSION_HANDLE hSession, std::span<PM_QUERY_ELEMENT> elements, double winSizeMs, double metricOffsetMs)
        {
            if (auto sta = pmRegisterDynamicQuery(hSession, &hQuery_, elements.data(),
                elements.size(), winSizeMs, metricOffsetMs); sta != PM_STATUS_SUCCESS) {
                throw Exception{ std::format("dynamic query register call failed with error id={}", (int)sta) };
            }
            if (elements.size() > 0) {
                blobSize_ = elements.back().dataOffset + elements.back().dataSize;
            }
        }
        // zero out members, useful after emptying via move or reset
        void Clear_() noexcept
        {
            hQuery_ = nullptr;
            blobSize_ = 0ull;
        }
        // data
        PM_DYNAMIC_QUERY_HANDLE hQuery_ = nullptr;
        size_t blobSize_ = 0ull;
    };
}
