#include "vulkan_state_mapper.h"

VulkanStateMapping map_resource_state_to_vulkan(ResourceState state) {
    VulkanStateMapping mapping{};
    mapping.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    mapping.accessMask = 0;
    mapping.stageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    switch (state) {
    case ResourceState::Undefined:
        mapping.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        mapping.accessMask = 0;
        mapping.stageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        break;

    case ResourceState::Common:
        mapping.layout = VK_IMAGE_LAYOUT_GENERAL;
        mapping.accessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        break;

    case ResourceState::VertexBuffer:
        mapping.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        mapping.accessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        break;

    case ResourceState::IndexBuffer:
        mapping.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        mapping.accessMask = VK_ACCESS_INDEX_READ_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        break;

    case ResourceState::UniformBuffer:
        mapping.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        mapping.accessMask = VK_ACCESS_UNIFORM_READ_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        break;

    case ResourceState::ShaderResource:
        mapping.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        mapping.accessMask = VK_ACCESS_SHADER_READ_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        break;

    case ResourceState::ComputeShaderRead:
        mapping.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        mapping.accessMask = VK_ACCESS_SHADER_READ_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        break;

    case ResourceState::ComputeShaderWrite:
        mapping.layout = VK_IMAGE_LAYOUT_GENERAL;
        mapping.accessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        break;

    case ResourceState::ComputeReadWrite:
        mapping.layout = VK_IMAGE_LAYOUT_GENERAL;
        mapping.accessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        break;

    case ResourceState::RenderTarget:
        mapping.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        mapping.accessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        break;

    case ResourceState::DepthStencilRead:
        mapping.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        mapping.accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        break;

    case ResourceState::DepthStencilWrite:
        mapping.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        mapping.accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        break;

    case ResourceState::TransferSrc:
        mapping.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        mapping.accessMask = VK_ACCESS_TRANSFER_READ_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;

    case ResourceState::TransferDst:
        mapping.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        mapping.accessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mapping.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;

    case ResourceState::Present:
        mapping.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        mapping.accessMask = 0;
        mapping.stageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        break;
    }

    return mapping;
}
