#ifndef SUCKLESS_VULKAN_RENDER_GRAPH_H
#define SUCKLESS_VULKAN_RENDER_GRAPH_H

#include <string>
#include <string_view>
#include <vector>
#include <initializer_list>
#include <functional>
#include <unordered_map>
#include <vulkan/vulkan.h>

#include <cstdint>

namespace rhi {

// Identifiant d'une ressource virtuelle dans le graphe
using ResourceHandle = uint32_t;

struct PhysicalResource {
    VkImage image;
    VkFormat format;
    VkImageAspectFlags aspect;
};

struct TransientImageDesc {
    uint32_t width;
    uint32_t height;
    VkFormat format;
    VkImageUsageFlags usage;
    VkImageAspectFlags aspect;
};

enum class ResourceState : std::uint8_t {
    Undefined,
    RenderTarget,
    ShaderRead,
    ComputeWrite,
    TransferRead,
    TransferWrite
};

struct ResourceTransition {
    ResourceHandle resource;
    ResourceState from;
    ResourceState to;
};

struct PassDependency {
    ResourceHandle resource;
    ResourceState state;
};

struct RenderPassNode {
    std::string name;
    std::vector<PassDependency> inputs;
    std::vector<PassDependency> outputs;
    std::function<void(VkCommandBuffer)> executeCallback;
    std::vector<ResourceTransition> transitions; // Barriers to execute before the pass
};

class RenderGraph {
public:
    RenderGraph();
    ~RenderGraph();

    // Clears frame nodes and bindings without freeing vector capacity
    void Reset();

    // Declaration Phase (Cold Path or Zero-Alloc Frame Build)
    ResourceHandle CreateVirtualImage(std::string_view name);
    ResourceHandle CreateTransientImage(std::string_view name, const TransientImageDesc& desc);

    void AddPass(std::string_view name, 
                 std::initializer_list<PassDependency> reads, 
                 std::initializer_list<PassDependency> writes, 
                 std::function<void(VkCommandBuffer)> execute);

    void AddPass(std::string_view name, 
                 const std::vector<PassDependency>& reads, 
                 const std::vector<PassDependency>& writes, 
                 std::function<void(VkCommandBuffer)> execute);

    // Compilation Phase
    bool Compile();

    // Execution Phase (Hot Path)
    void BindPhysicalResource(ResourceHandle handle, VkImage image, VkFormat format, VkImageAspectFlags aspect);
    void Execute(VkCommandBuffer cb);

    const std::vector<RenderPassNode>& GetSortedPasses() const { return sortedPasses; }

private:
    void BuildAdjacencyList(std::vector<std::vector<size_t>>& adj, std::vector<int>& inDegree);
    bool PerformTopologicalSort(const std::vector<std::vector<size_t>>& adj, std::vector<int>& inDegree);
    void CalculateLifetimes();
    void CalculateMemoryAliasing();
    void GenerateTransitions();

    std::vector<RenderPassNode> passes;
    std::vector<RenderPassNode> sortedPasses;
    uint32_t nextResourceHandle = 1;
    std::unordered_map<ResourceHandle, PhysicalResource> physicalResources;
    std::unordered_map<ResourceHandle, TransientImageDesc> transientResources;
    std::unordered_map<ResourceHandle, std::pair<size_t, size_t>> resourceLifetimes; // First and last pass index
    std::unordered_map<ResourceHandle, std::string> resourceNames;
};

} // namespace rhi

#endif // SUCKLESS_VULKAN_RENDER_GRAPH_H
