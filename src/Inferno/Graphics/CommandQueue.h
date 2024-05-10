#pragma once

#include <DirectX.h>
#include <Types.h>

namespace Inferno {
    class CommandQueue {
        ComPtr<ID3D12Fence> _fence;
        Microsoft::WRL::Wrappers::Event _fenceEvent;
        uint64 _nextFenceValue = 1, _lastCompletedValue = 0;
        ComPtr<ID3D12CommandQueue> _queue;
        D3D12_COMMAND_LIST_TYPE _type;
        std::mutex _eventMutex;

    public:
        CommandQueue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, wstring_view name) : _type(type) {
            assert(device);
            D3D12_COMMAND_QUEUE_DESC desc{};
            desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.Type = type;

            ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_queue)));
            ThrowIfFailed(_queue->SetName(name.data()));

            ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)));
            ThrowIfFailed(_fence->SetName(name.data()));

            //ThrowIfFailed(_fence->Signal((uint64)type << 56));

            _fenceEvent.Attach(CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE));
            if (!_fenceEvent.IsValid())
                throw std::exception("CreateEvent");
        }

        bool IsFenceComplete(uint64 value) {
            // Avoid querying the fence value by testing against the last one seen.
            // The max() is to protect against an unlikely race condition that could cause the last
            // completed fence value to regress.
            if (value > _lastCompletedValue)
                _lastCompletedValue = std::max(_lastCompletedValue, _fence->GetCompletedValue());

            return value <= _lastCompletedValue;
        }

        void WaitForFence(uint64 value) {
            if (IsFenceComplete(value))
                return;

            std::lock_guard lock(_eventMutex);
            ThrowIfFailed(_fence->SetEventOnCompletion(value, _fenceEvent.Get()));
            WaitForSingleObject(_fenceEvent.Get(), INFINITE);
            _lastCompletedValue = value;
        }

        void WaitForIdle() {
            WaitForFence(IncrementFence());
        }

        // Signal the next fence value (with the GPU)
        int64 IncrementFence() {
            std::lock_guard lock(_eventMutex);
            ThrowIfFailed(_queue->Signal(_fence.Get(), _nextFenceValue));
            return _nextFenceValue++;
        }

        int64 Execute(ID3D12GraphicsCommandList* cmdList) {
            ThrowIfFailed(cmdList->Close());
            ID3D12CommandList* ppCommandLists[] = { cmdList };
            _queue->ExecuteCommandLists(1, ppCommandLists);
            return IncrementFence();
        }

        D3D12_COMMAND_LIST_TYPE GetType() const { return _type; }

        ID3D12CommandQueue* Get() const { return _queue.Get(); }
    };

}