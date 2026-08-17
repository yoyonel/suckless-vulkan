#include "render_graph.h"
#include "vulkan_state_mapper.h"
#include <unordered_map>
#include <queue>
#include <stdexcept>
#include <algorithm>
#include "../app_log.h"

namespace rhi {

RenderGraph::RenderGraph() {
    passes.reserve(16);
    sortedPasses.reserve(16);
}

RenderGraph::~RenderGraph() = default;

void RenderGraph::Reset() {
    passes.clear();
    sortedPasses.clear();
    nextResourceHandle = 1;
    physicalResources.clear();
    transientResources.clear();
    resourceLifetimes.clear();
    resourceNames.clear();
}

ResourceHandle RenderGraph::CreateVirtualImage(std::string_view name) {
    ResourceHandle handle = nextResourceHandle++;
    resourceNames[handle] = std::string(name);
    return handle;
}

ResourceHandle RenderGraph::CreateTransientImage(std::string_view name, const TransientImageDesc& desc) {
    ResourceHandle handle = CreateVirtualImage(name);
    transientResources[handle] = desc;
    return handle;
}

void RenderGraph::AddPass(std::string_view name, 
                          std::initializer_list<PassDependency> reads, 
                          std::initializer_list<PassDependency> writes, 
                          std::function<void(VkCommandBuffer)> execute) {
    RenderPassNode node;
    node.name = name;
    node.inputs.assign(reads.begin(), reads.end());
    node.outputs.assign(writes.begin(), writes.end());
    node.executeCallback = std::move(execute);
    passes.push_back(std::move(node));
}

void RenderGraph::AddPass(std::string_view name, 
                          const std::vector<PassDependency>& reads, 
                          const std::vector<PassDependency>& writes, 
                          std::function<void(VkCommandBuffer)> execute) {
    passes.push_back({std::string(name), reads, writes, std::move(execute), {}});
}

static void process_pass_reads(size_t passIndex, 
                               const std::vector<PassDependency>& inputs,
                               std::unordered_map<ResourceHandle, std::vector<size_t>>& readers,
                               const std::unordered_map<ResourceHandle, std::vector<size_t>>& writers,
                               std::vector<std::vector<size_t>>& adj, 
                               std::vector<int>& inDegree) {
    for (const auto& read : inputs) {
        readers[read.resource].push_back(passIndex);
        auto it = writers.find(read.resource);
        if (it != writers.end() && !it->second.empty()) {
            size_t prevWriter = it->second.back();
            if (prevWriter != passIndex) {
                adj[prevWriter].push_back(passIndex);
                inDegree[passIndex]++;
            }
        }
    }
}

static void process_pass_writes(size_t passIndex,
                                const std::vector<PassDependency>& outputs,
                                std::unordered_map<ResourceHandle, std::vector<size_t>>& readers,
                                std::unordered_map<ResourceHandle, std::vector<size_t>>& writers,
                                std::vector<std::vector<size_t>>& adj,
                                std::vector<int>& inDegree) {
    for (const auto& write : outputs) {
        auto& wList = writers[write.resource];
        if (!wList.empty()) {
            size_t prevWriter = wList.back();
            if (prevWriter != passIndex) {
                adj[prevWriter].push_back(passIndex);
                inDegree[passIndex]++;
            }
        }
        auto rIt = readers.find(write.resource);
        if (rIt != readers.end()) {
            for (size_t readerIdx : rIt->second) {
                if (readerIdx != passIndex) {
                    adj[readerIdx].push_back(passIndex);
                    inDegree[passIndex]++;
                }
            }
            rIt->second.clear();
        }
        wList.push_back(passIndex);
    }
}

void RenderGraph::BuildAdjacencyList(std::vector<std::vector<size_t>>& adj, std::vector<int>& inDegree) {
    std::unordered_map<ResourceHandle, std::vector<size_t>> resourceWriters;
    std::unordered_map<ResourceHandle, std::vector<size_t>> resourceReaders;

    for (size_t i = 0; i < passes.size(); ++i) {
        process_pass_reads(i, passes[i].inputs, resourceReaders, resourceWriters, adj, inDegree);
        process_pass_writes(i, passes[i].outputs, resourceReaders, resourceWriters, adj, inDegree);
    }
}

bool RenderGraph::PerformTopologicalSort(const std::vector<std::vector<size_t>>& adj, std::vector<int>& inDegree) {
    std::queue<size_t> q;
    for (size_t i = 0; i < inDegree.size(); ++i) {
        if (inDegree[i] == 0) {
            q.push(i);
        }
    }

    while (!q.empty()) {
        size_t u = q.front();
        q.pop();

        sortedPasses.push_back(passes[u]);

        for (size_t v : adj[u]) {
            inDegree[v]--;
            if (inDegree[v] == 0) {
                q.push(v);
            }
        }
    }

    return sortedPasses.size() == passes.size();
}

void RenderGraph::GenerateTransitions() {
    std::unordered_map<ResourceHandle, ResourceState> currentState;
    for (const auto& [handle, phys] : physicalResources) {
        if (phys.initialState != ResourceState::Undefined) {
            currentState[handle] = phys.initialState;
        }
    }

    for (auto& pass : sortedPasses) {
        pass.transitions.clear();
        for (const auto& dep : pass.inputs) {
            ResourceState current = currentState.count(dep.resource) ? currentState[dep.resource] : ResourceState::Undefined;
            if (current != dep.state) {
                pass.transitions.push_back({dep.resource, current, dep.state});
                currentState[dep.resource] = dep.state;
            }
        }
        for (const auto& dep : pass.outputs) {
            ResourceState current = currentState.count(dep.resource) ? currentState[dep.resource] : ResourceState::Undefined;
            if (current != dep.state) {
                pass.transitions.push_back({dep.resource, current, dep.state});
                currentState[dep.resource] = dep.state;
            }
        }
    }
}

bool RenderGraph::Compile() {
    if (passes.empty()) return true;

    sortedPasses.clear();

    std::vector<std::vector<size_t>> adj(passes.size());
    std::vector<int> inDegree(passes.size(), 0);

    BuildAdjacencyList(adj, inDegree);

    if (!PerformTopologicalSort(adj, inDegree)) {
        return false;
    }

    CalculateLifetimes();
    CalculateMemoryAliasing();
    GenerateTransitions();

    return true;
}

void RenderGraph::CalculateMemoryAliasing() {
    if (transientResources.empty()) return;
    
    LOG_DEBUG("RenderGraph", "--- Memory Aliasing ---");
    // Sort transient resources by FirstPass
    std::vector<ResourceHandle> sortedTransients;
    sortedTransients.reserve(transientResources.size());
    for (const auto& [res, desc] : transientResources) {
        sortedTransients.push_back(res);
    }
    
    std::sort(sortedTransients.begin(), sortedTransients.end(), [&](ResourceHandle a, ResourceHandle b) {
        return resourceLifetimes[a].first < resourceLifetimes[b].first;
    });

    struct MemorySlot {
        size_t freeAfterPass;
        uint32_t slotIndex;
    };
    std::vector<MemorySlot> slots;
    
    size_t totalMemorySaved = 0;

    for (ResourceHandle res : sortedTransients) {
        size_t firstPass = resourceLifetimes[res].first;
        size_t lastPass = resourceLifetimes[res].second;
        
        // Find first free slot
        uint32_t chosenSlot = UINT32_MAX;
        for (auto& slot : slots) {
            if (slot.freeAfterPass < firstPass) {
                chosenSlot = slot.slotIndex;
                slot.freeAfterPass = lastPass;
                break;
            }
        }
        
        if (chosenSlot == UINT32_MAX) {
            chosenSlot = static_cast<uint32_t>(slots.size());
            slots.push_back({lastPass, chosenSlot});
        } else {
            const auto& desc = transientResources[res];
            totalMemorySaved += static_cast<size_t>(desc.width) * static_cast<size_t>(desc.height) * 4ULL; // Approximation for logs
        }
        
        std::string name = resourceNames.count(res) ? resourceNames[res] : "Unknown";
        LOG_DEBUG("RenderGraph", "Transient %s assigned to Memory Slot %u (Lifespan: %zu -> %zu)", name.c_str(), chosenSlot, firstPass, lastPass);
    }
    (void)totalMemorySaved;
}

void RenderGraph::CalculateLifetimes() {
    resourceLifetimes.clear();

    for (size_t i = 0; i < sortedPasses.size(); ++i) {
        const auto& pass = sortedPasses[i];
        
        for (const auto& read : pass.inputs) {
            if (resourceLifetimes.count(read.resource) == 0) {
                resourceLifetimes[read.resource] = {i, i};
            } else {
                resourceLifetimes[read.resource].second = i;
            }
        }
        
        for (const auto& write : pass.outputs) {
            if (resourceLifetimes.count(write.resource) == 0) {
                resourceLifetimes[write.resource] = {i, i};
            } else {
                resourceLifetimes[write.resource].second = i;
            }
        }
    }
}

void RenderGraph::BindPhysicalResource(ResourceHandle handle, VkImage image, VkFormat format, VkImageAspectFlags aspect,
                                       ResourceState initialState) {
    physicalResources[handle] = {image, format, aspect, initialState};
}

static void emit_pass_barriers(VkCommandBuffer cb, const RenderPassNode& pass, const std::unordered_map<ResourceHandle, PhysicalResource>& physicalResources) {
    VkImageMemoryBarrier vkBarriers[16];
    uint32_t barrierCount = 0;
    VkPipelineStageFlags srcStages = 0;
    VkPipelineStageFlags dstStages = 0;

    for (const auto& transition : pass.transitions) {
        if (barrierCount >= 16) break;
        auto it = physicalResources.find(transition.resource);
        if (it == physicalResources.end()) continue;

        const PhysicalResource& phys = it->second;
        ResourceState fromState = transition.from;
        ResourceState toState = transition.to;

        if ((phys.aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0) {
            if (fromState == ResourceState::RenderTarget) fromState = ResourceState::DepthStencilWrite;
            if (toState == ResourceState::RenderTarget) toState = ResourceState::DepthStencilWrite;
        }

        VulkanStateMapping srcInfo = map_resource_state_to_vulkan(fromState);
        VulkanStateMapping dstInfo = map_resource_state_to_vulkan(toState);

        VkImageMemoryBarrier& barrier = vkBarriers[barrierCount++];
        barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = phys.image;
        barrier.subresourceRange.aspectMask = phys.aspect;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout = srcInfo.layout;
        barrier.srcAccessMask = srcInfo.accessMask;
        barrier.newLayout = dstInfo.layout;
        barrier.dstAccessMask = dstInfo.accessMask;

        srcStages |= srcInfo.stageMask;
        dstStages |= dstInfo.stageMask;
    }

    if (barrierCount > 0 && cb != VK_NULL_HANDLE) {
        if (srcStages == 0) srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        if (dstStages == 0) dstStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        vkCmdPipelineBarrier(cb, srcStages, dstStages, 0, 0, nullptr, 0, nullptr, barrierCount, vkBarriers);
    }
}

void RenderGraph::Execute(VkCommandBuffer cb) {
    GenerateTransitions();
    for (auto& pass : sortedPasses) {
        if (!pass.transitions.empty()) {
            emit_pass_barriers(cb, pass, physicalResources);
        }

        // 2. Execute pass payload
        if (pass.executeCallback) {
            pass.executeCallback(cb);
        }
    }
}

} // namespace rhi
