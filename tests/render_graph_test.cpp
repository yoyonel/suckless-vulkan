#include "app_log.h"
#include "rhi/render_graph.h"
#include "vk_engine.h"
#include <chrono>
#include <cstdio>
#include <cstring>

struct TestStats {
    int passed = 0;
    int failed = 0;
};

static void check(bool condition, const char* label, bool& success) {
    if (!condition) {
        success = false;
        LOG_ERROR("test", "FAIL: %s", label);
    }
}

bool test_render_graph() {
    bool success = true;

    rhi::RenderGraph graph;

    auto texA = graph.CreateVirtualImage("TexA");
    auto texB = graph.CreateVirtualImage("TexB");
    auto texC = graph.CreateVirtualImage("TexC");

    std::vector<std::string> executionOrder;

    // Pass 1 (reads nothing, writes A) - Declared first
    graph.AddPass("Pass1", {}, {{texA, rhi::ResourceState::RenderTarget}}, [&](VkCommandBuffer cb) {
        (void)cb;
        executionOrder.push_back("Pass1");
    });

    // Pass 2 (reads A, writes B) - Declared second
    graph.AddPass("Pass2", {{texA, rhi::ResourceState::ShaderRead}}, {{texB, rhi::ResourceState::RenderTarget}}, [&](VkCommandBuffer cb) {
        (void)cb;
        executionOrder.push_back("Pass2");
    });

    // Pass 3 (reads B, writes C) - Declared third
    graph.AddPass("Pass3", {{texB, rhi::ResourceState::ShaderRead}}, {{texC, rhi::ResourceState::RenderTarget}}, [&](VkCommandBuffer cb) {
        (void)cb;
        executionOrder.push_back("Pass3");
    });

    bool compiled = graph.Compile();
    check(compiled, "Graph Compilation without cycles", success);

    const auto& sorted = graph.GetSortedPasses();
    check(sorted.size() == 3, "Graph sorted size", success);
    if (sorted.size() == 3) {
        // Pass1 outputs TexA
        check(sorted[0].transitions.size() == 1, "Pass1 transitions", success);
        if (sorted[0].transitions.size() == 1) {
            check(sorted[0].transitions[0].resource == texA, "Pass1 TexA", success);
            check(sorted[0].transitions[0].from == rhi::ResourceState::Undefined, "Pass1 TexA From", success);
            check(sorted[0].transitions[0].to == rhi::ResourceState::RenderTarget, "Pass1 TexA To", success);
        }

        // Pass2 inputs TexA, outputs TexB
        check(sorted[1].transitions.size() == 2, "Pass2 transitions", success);
        if (sorted[1].transitions.size() == 2) {
            check(sorted[1].transitions[0].resource == texA, "Pass2 TexA", success);
            check(sorted[1].transitions[0].from == rhi::ResourceState::RenderTarget, "Pass2 TexA From", success);
            check(sorted[1].transitions[0].to == rhi::ResourceState::ShaderRead, "Pass2 TexA To", success);
        }

        // Pass3 inputs TexB, outputs TexC
        check(sorted[2].transitions.size() == 2, "Pass3 transitions", success);
        if (sorted[2].transitions.size() == 2) {
            check(sorted[2].transitions[0].resource == texB, "Pass3 TexB", success);
            check(sorted[2].transitions[0].from == rhi::ResourceState::RenderTarget, "Pass3 TexB From", success);
            check(sorted[2].transitions[0].to == rhi::ResourceState::ShaderRead, "Pass3 TexB To", success);
        }
    }

    graph.Execute(VK_NULL_HANDLE);

    check(executionOrder.size() == 3, "All passes executed", success);
    if (executionOrder.size() == 3) {
        check(executionOrder[0] == "Pass1", "Pass1 executed first", success);
        check(executionOrder[1] == "Pass2", "Pass2 executed second", success);
        check(executionOrder[2] == "Pass3", "Pass3 executed third", success);
    }

    return success;
}

bool test_render_graph_stress() {
    bool success = true;
    rhi::RenderGraph graph;

    std::vector<rhi::ResourceHandle> res;
    res.reserve(50);
    for (int i = 0; i < 50; ++i) {
        res.push_back(graph.CreateVirtualImage("Res_" + std::to_string(i)));
    }

    graph.AddPass("GBuffer", {},
                  {{res[0], rhi::ResourceState::RenderTarget},
                   {res[1], rhi::ResourceState::RenderTarget},
                   {res[2], rhi::ResourceState::RenderTarget},
                   {res[3], rhi::ResourceState::RenderTarget}},
                  [](VkCommandBuffer) {});
    graph.AddPass("SSAO", {{res[1], rhi::ResourceState::ShaderRead}, {res[2], rhi::ResourceState::ShaderRead}}, {{res[4], rhi::ResourceState::RenderTarget}},
                  [](VkCommandBuffer) {});
    graph.AddPass("DeferredLighting",
                  {{res[0], rhi::ResourceState::ShaderRead},
                   {res[1], rhi::ResourceState::ShaderRead},
                   {res[2], rhi::ResourceState::ShaderRead},
                   {res[3], rhi::ResourceState::ShaderRead},
                   {res[4], rhi::ResourceState::ShaderRead}},
                  {{res[5], rhi::ResourceState::RenderTarget}}, [](VkCommandBuffer) {});

    for (int i = 6; i < 46; i += 2) {
        graph.AddPass("Compute_" + std::to_string(i), {{res[i - 1], rhi::ResourceState::ShaderRead}},
                      {{res[i], rhi::ResourceState::RenderTarget}, {res[i + 1], rhi::ResourceState::RenderTarget}}, [](VkCommandBuffer) {});
    }

    graph.AddPass("PostProcess", {{res[45], rhi::ResourceState::ShaderRead}}, {{res[46], rhi::ResourceState::RenderTarget}}, [](VkCommandBuffer) {});

    bool compiled = graph.Compile();
    check(compiled, "Stress Graph Compilation", success);

    auto start = std::chrono::high_resolution_clock::now();
    const int ITERATIONS = 10000;
    for (int i = 0; i < ITERATIONS; ++i) {
        graph.Compile();
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    LOG_INFO("test", "Stress Test Compile() x%d took %lld us. (Avg: %.2f us/frame)", ITERATIONS, (long long)duration, duration / (float)ITERATIONS);

    return success;
}

bool test_render_graph_initial_state() {
    bool success = true;
    rhi::RenderGraph graph;

    auto texPersistent = graph.CreateVirtualImage("PersistentTex");

    graph.AddPass("PassCompute", {{texPersistent, rhi::ResourceState::ComputeShaderRead}}, {{texPersistent, rhi::ResourceState::ComputeShaderWrite}},
                  [](VkCommandBuffer) {});

    // Bind with initialState = ShaderResource (simulating previous frame state)
    VkImage dummyImage = VK_NULL_HANDLE;
    graph.BindPhysicalResource(texPersistent, dummyImage, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, rhi::ResourceState::ShaderResource);

    bool compiled = graph.Compile();
    check(compiled, "Graph with initial state compiled", success);

    graph.Execute(VK_NULL_HANDLE);

    const auto& sorted = graph.GetSortedPasses();
    check(sorted.size() == 1, "One sorted pass", success);
    if (sorted.size() == 1) {
        check(!sorted[0].transitions.empty(), "Pass has transitions", success);
        if (!sorted[0].transitions.empty()) {
            check(sorted[0].transitions[0].from == rhi::ResourceState::ShaderResource, "Transition 'from' matches initialState (not Undefined)", success);
            check(sorted[0].transitions[0].to == rhi::ResourceState::ComputeShaderRead, "Transition 'to' is ComputeShaderRead", success);
        }
    }

    return success;
}
