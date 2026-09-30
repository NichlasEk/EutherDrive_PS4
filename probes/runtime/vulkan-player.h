// SPDX-License-Identifier: MIT
// OpenGNM Vulkan presentation experiment. One owner, bounded waits, no GPU readback.
#pragma once
#include <vulkan/vulkan.h>
#include "vulkan-shaders.h"
static void report(const char *, ...);
static VkInstance ed_vk_instance;
static VkPhysicalDevice ed_vk_physical;
static VkDevice ed_vk_device;
static VkQueue ed_vk_queue;
static VkSwapchainKHR ed_vk_swap;
static VkRenderPass ed_vk_pass;
static VkPipelineLayout ed_vk_layout;
static VkPipeline ed_vk_pipeline;
static VkDescriptorSetLayout ed_vk_set_layout;
static VkDescriptorPool ed_vk_descriptors;
static VkDescriptorSet ed_vk_sets[2];
static VkSampler ed_vk_sampler;
static VkCommandPool ed_vk_pool;
static VkCommandBuffer ed_vk_cmd;
static VkFence ed_vk_fence, ed_vk_acquire;
static VkImage ed_vk_images[2];
static VkImageView ed_vk_views[2];
static VkFramebuffer ed_vk_targets[2];
#ifdef ED_VK_OFFSCREEN
static VkBuffer ed_vk_readback;
static void *ed_vk_readback_pixels;
#endif
static int ed_vk_ready, ed_vk_lost, ed_vk_presented;
static VkResult ed_vk_error;
static const char *ed_vk_operation;
static struct EdVkTexture {
    VkImage image; VkDeviceMemory memory; VkImageView view;
    unsigned char *mapped; VkSubresourceLayout sub;
    int width, height, initialized;
} ed_vk_textures[2];
// Bootstrap breadcrumbs survive a driver crash, including calls that never return.
static void ed_vk_checkpoint(const char *operation) {
#ifndef ED_VK_OFFSCREEN
    if (!ed_vk_ready) report("VULKAN init: %s",operation);
    else if (!ed_vk_presented) {
        extern void vk_ps4_log_raw(const char *);
        vk_ps4_log_raw(operation);
    }
#else
    (void)operation;
#endif
}
#define ED_VK_TRY(call) do { ed_vk_operation=#call; ed_vk_checkpoint(#call); ed_vk_error=(call); if(ed_vk_error!=VK_SUCCESS)return 0; } while(0)
static uint32_t ed_vk_memory_type_flags(uint32_t bits,VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(ed_vk_physical,&props);
    for(uint32_t i=0;i<props.memoryTypeCount;++i)
        if((bits&(1u<<i)) && (props.memoryTypes[i].propertyFlags&required)==required)return i;
    return UINT32_MAX;
}
static uint32_t ed_vk_memory_type(uint32_t bits) {
    return ed_vk_memory_type_flags(bits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}
static int ed_vk_texture(int index, const uint32_t *pixels, int width, int height) {
    struct EdVkTexture *t=&ed_vk_textures[index];
    // Previous frame is fenced before any host write or resource replacement.
    if(t->width!=width || t->height!=height) {
        if(t->view)vkDestroyImageView(ed_vk_device,t->view,NULL);
        if(t->image)vkDestroyImage(ed_vk_device,t->image,NULL);
        if(t->mapped)vkUnmapMemory(ed_vk_device,t->memory);
        if(t->memory)vkFreeMemory(ed_vk_device,t->memory,NULL);
        memset(t,0,sizeof(*t));
        // Doom3 ICD supports linear RGBA8. The shader swaps uploaded BGRA channels.
        VkImageCreateInfo image={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
            .format=VK_FORMAT_R8G8B8A8_UNORM,.extent={(uint32_t)width,(uint32_t)height,1},
            .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_LINEAR,
            .usage=VK_IMAGE_USAGE_SAMPLED_BIT,.initialLayout=VK_IMAGE_LAYOUT_PREINITIALIZED};
        ED_VK_TRY(vkCreateImage(ed_vk_device,&image,NULL,&t->image));
        VkMemoryRequirements req;vkGetImageMemoryRequirements(ed_vk_device,t->image,&req);
        uint32_t type=ed_vk_memory_type(req.memoryTypeBits);
        if(type==UINT32_MAX) { ed_vk_error=VK_ERROR_FEATURE_NOT_PRESENT; return 0; }
        VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
        ED_VK_TRY(vkAllocateMemory(ed_vk_device,&alloc,NULL,&t->memory));
        ED_VK_TRY(vkBindImageMemory(ed_vk_device,t->image,t->memory,0));
        ED_VK_TRY(vkMapMemory(ed_vk_device,t->memory,0,VK_WHOLE_SIZE,0,(void**)&t->mapped));
        VkImageSubresource sub={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT};
        vkGetImageSubresourceLayout(ed_vk_device,t->image,&sub,&t->sub);
        if(t->sub.rowPitch<(VkDeviceSize)width*4 || t->sub.offset+(VkDeviceSize)(height-1)*t->sub.rowPitch+(VkDeviceSize)width*4>req.size) {
            ed_vk_error=VK_ERROR_INITIALIZATION_FAILED;return 0;
        }
        VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=t->image,
            .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=image.format,
            .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        ED_VK_TRY(vkCreateImageView(ed_vk_device,&view,NULL,&t->view));
        VkDescriptorImageInfo info={ed_vk_sampler,t->view,VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=ed_vk_sets[index],
            .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};
        vkUpdateDescriptorSets(ed_vk_device,1,&write,0,NULL);
        t->width=width;t->height=height;
    }
    for(int y=0;y<height;++y)memcpy(t->mapped+t->sub.offset+y*t->sub.rowPitch,pixels+y*width,(size_t)width*4);
    return 1;
}
static int ed_vk_init(void) {
    VkInstanceCreateInfo instance={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ED_VK_TRY(vkCreateInstance(&instance,NULL,&ed_vk_instance));
    uint32_t count=1;ED_VK_TRY(vkEnumeratePhysicalDevices(ed_vk_instance,&count,&ed_vk_physical));
    if(count!=1 || !ed_vk_physical) { ed_vk_error=VK_ERROR_INITIALIZATION_FAILED; return 0; }
    float priority=1;
    VkDeviceQueueCreateInfo queue={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=0,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo device={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue};
    ED_VK_TRY(vkCreateDevice(ed_vk_physical,&device,NULL,&ed_vk_device));
    vkGetDeviceQueue(ed_vk_device,0,0,&ed_vk_queue);
    if(!ed_vk_queue) { ed_vk_error=VK_ERROR_INITIALIZATION_FAILED; return 0; }
    // Native ICD owns VideoOut directly, like its tested fullscreen probes; no WSI loader.
#ifndef ED_VK_OFFSCREEN
    VkSwapchainCreateInfoKHR swap={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.minImageCount=2,
        .imageFormat=VK_FORMAT_R8G8B8A8_UNORM,.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent={1280,720},.imageArrayLayers=1,.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode=VK_PRESENT_MODE_FIFO_KHR,.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR};
    ED_VK_TRY(vkCreateSwapchainKHR(ed_vk_device,&swap,NULL,&ed_vk_swap));
    count=2;ED_VK_TRY(vkGetSwapchainImagesKHR(ed_vk_device,ed_vk_swap,&count,ed_vk_images));
    if(count!=2)return 0;
#else
    for(int i=0;i<2;++i) {
        VkImageCreateInfo image={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
            .format=VK_FORMAT_R8G8B8A8_UNORM,.extent={1280,720,1},.mipLevels=1,.arrayLayers=1,
            .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
            .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
        ED_VK_TRY(vkCreateImage(ed_vk_device,&image,NULL,&ed_vk_images[i]));
        VkMemoryRequirements req;vkGetImageMemoryRequirements(ed_vk_device,ed_vk_images[i],&req);
        VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=ed_vk_memory_type_flags(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        VkDeviceMemory memory;ED_VK_TRY(vkAllocateMemory(ed_vk_device,&alloc,NULL,&memory));
        ED_VK_TRY(vkBindImageMemory(ed_vk_device,ed_vk_images[i],memory,0));
    }
    VkBufferCreateInfo buffer={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=1280*720*4,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    ED_VK_TRY(vkCreateBuffer(ed_vk_device,&buffer,NULL,&ed_vk_readback));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(ed_vk_device,ed_vk_readback,&req);
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=ed_vk_memory_type(req.memoryTypeBits)};
    VkDeviceMemory memory;ED_VK_TRY(vkAllocateMemory(ed_vk_device,&alloc,NULL,&memory));
    ED_VK_TRY(vkBindBufferMemory(ed_vk_device,ed_vk_readback,memory,0));
    ED_VK_TRY(vkMapMemory(ed_vk_device,memory,0,VK_WHOLE_SIZE,0,&ed_vk_readback_pixels));
#endif
    VkAttachmentDescription attachment={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,
        .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=
#ifdef ED_VK_OFFSCREEN
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
#else
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
#endif
    };
    VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&reference};
    VkRenderPassCreateInfo pass={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,
        .pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass};
    ED_VK_TRY(vkCreateRenderPass(ed_vk_device,&pass,NULL,&ed_vk_pass));
    for(unsigned i=0;i<2;++i) {
        VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=ed_vk_images[i],
            .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        ED_VK_TRY(vkCreateImageView(ed_vk_device,&view,NULL,&ed_vk_views[i]));
        VkFramebufferCreateInfo target={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=ed_vk_pass,
            .attachmentCount=1,.pAttachments=&ed_vk_views[i],.width=1280,.height=720,.layers=1};
        ED_VK_TRY(vkCreateFramebuffer(ed_vk_device,&target,NULL,&ed_vk_targets[i]));
    }
    VkDescriptorSetLayoutBinding binding={0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo set={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    ED_VK_TRY(vkCreateDescriptorSetLayout(ed_vk_device,&set,NULL,&ed_vk_set_layout));
    VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2};
    VkDescriptorPoolCreateInfo descriptors={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=2,.poolSizeCount=1,.pPoolSizes=&size};
    ED_VK_TRY(vkCreateDescriptorPool(ed_vk_device,&descriptors,NULL,&ed_vk_descriptors));
    VkDescriptorSetLayout layouts[]={ed_vk_set_layout,ed_vk_set_layout};
    VkDescriptorSetAllocateInfo sets={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=ed_vk_descriptors,.descriptorSetCount=2,.pSetLayouts=layouts};
    ED_VK_TRY(vkAllocateDescriptorSets(ed_vk_device,&sets,ed_vk_sets));
    VkSamplerCreateInfo sampler={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    ED_VK_TRY(vkCreateSampler(ed_vk_device,&sampler,NULL,&ed_vk_sampler));
    VkPipelineLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&ed_vk_set_layout};
    ED_VK_TRY(vkCreatePipelineLayout(ed_vk_device,&layout,NULL,&ed_vk_layout));
    VkShaderModule modules[2];
    VkShaderModuleCreateInfo shader={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof(ed_vk_vertex),.pCode=ed_vk_vertex};
    ED_VK_TRY(vkCreateShaderModule(ed_vk_device,&shader,NULL,&modules[0]));
    shader.codeSize=sizeof(ed_vk_fragment);shader.pCode=ed_vk_fragment;
    ED_VK_TRY(vkCreateShaderModule(ed_vk_device,&shader,NULL,&modules[1]));
    VkPipelineShaderStageCreateInfo stages[]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=modules[0],.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=modules[1],.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vertex={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo samples={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState color={.colorWriteMask=15};
    VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&color};
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=states};
    VkGraphicsPipelineCreateInfo pipeline={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,
        .pVertexInputState=&vertex,.pInputAssemblyState=&assembly,.pViewportState=&viewport,.pRasterizationState=&raster,
        .pMultisampleState=&samples,.pColorBlendState=&blend,.pDynamicState=&dynamic,.layout=ed_vk_layout,.renderPass=ed_vk_pass};
    ED_VK_TRY(vkCreateGraphicsPipelines(ed_vk_device,VK_NULL_HANDLE,1,&pipeline,NULL,&ed_vk_pipeline));
    vkDestroyShaderModule(ed_vk_device,modules[0],NULL);vkDestroyShaderModule(ed_vk_device,modules[1],NULL);
    VkCommandPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=0};
    ED_VK_TRY(vkCreateCommandPool(ed_vk_device,&pool,NULL,&ed_vk_pool));
    VkCommandBufferAllocateInfo command={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=ed_vk_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    ED_VK_TRY(vkAllocateCommandBuffers(ed_vk_device,&command,&ed_vk_cmd));
    VkFenceCreateInfo fence={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    ED_VK_TRY(vkCreateFence(ed_vk_device,&fence,NULL,&ed_vk_fence));
    ED_VK_TRY(vkCreateFence(ed_vk_device,&fence,NULL,&ed_vk_acquire));
    ed_vk_ready=1;return 1;
}
static void ed_vk_prepare_texture(int index) {
    struct EdVkTexture *t=&ed_vk_textures[index];
    VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_SHADER_READ_BIT,.oldLayout=t->initialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_PREINITIALIZED,
        .newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=t->image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(ed_vk_cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);
    t->initialized=1;
}
static void ed_vk_draw_texture(int index,int left,int top,int width,int height) {
    VkViewport viewport={(float)left,(float)top,(float)width,(float)height,0,1};
    VkRect2D scissor={{left,top},{(uint32_t)width,(uint32_t)height}};
    vkCmdSetViewport(ed_vk_cmd,0,1,&viewport);vkCmdSetScissor(ed_vk_cmd,0,1,&scissor);
    vkCmdBindDescriptorSets(ed_vk_cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,ed_vk_layout,0,1,&ed_vk_sets[index],0,NULL);
    vkCmdDraw(ed_vk_cmd,3,1,0,0);
}
static int ed_vk_present(const uint32_t *pixels,int width,int height,int left,int top,int scaled_width,int scaled_height,const uint32_t *hud) {
    if(ed_vk_lost || !ed_vk_ready)return 0;
    if(!ed_vk_texture(0,pixels,width,height) || (hud && !ed_vk_texture(1,hud,1280,64)))return 0;
#ifndef ED_VK_OFFSCREEN
    ED_VK_TRY(vkResetFences(ed_vk_device,1,&ed_vk_acquire));
    uint32_t index;
    ED_VK_TRY(vkAcquireNextImageKHR(ed_vk_device,ed_vk_swap,2000000000ULL,VK_NULL_HANDLE,ed_vk_acquire,&index));
    ED_VK_TRY(vkWaitForFences(ed_vk_device,1,&ed_vk_acquire,VK_TRUE,2000000000ULL));
#else
    uint32_t index=0;
#endif
    ED_VK_TRY(vkResetFences(ed_vk_device,1,&ed_vk_fence));
    ED_VK_TRY(vkResetCommandBuffer(ed_vk_cmd,0));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    ED_VK_TRY(vkBeginCommandBuffer(ed_vk_cmd,&begin));
    ed_vk_prepare_texture(0);
    if(hud)ed_vk_prepare_texture(1);
    VkClearValue clear={.color={{0.063f,0.125f,0.125f,1}}};
    VkRenderPassBeginInfo pass={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=ed_vk_pass,
        .framebuffer=ed_vk_targets[index],.renderArea={{0,0},{1280,720}},.clearValueCount=1,.pClearValues=&clear};
    vkCmdBeginRenderPass(ed_vk_cmd,&pass,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(ed_vk_cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,ed_vk_pipeline);
    ed_vk_draw_texture(0,left,top,scaled_width,scaled_height);
    if(hud)ed_vk_draw_texture(1,0,0,1280,64);
    vkCmdEndRenderPass(ed_vk_cmd);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT};
    vkCmdPipelineBarrier(ed_vk_cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,NULL,0,NULL);
#ifdef ED_VK_OFFSCREEN
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={1280,720,1}};
    vkCmdCopyImageToBuffer(ed_vk_cmd,ed_vk_images[index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,ed_vk_readback,1,&copy);
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(ed_vk_cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
#endif
    ED_VK_TRY(vkEndCommandBuffer(ed_vk_cmd));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&ed_vk_cmd};
    ED_VK_TRY(vkQueueSubmit(ed_vk_queue,1,&submit,ed_vk_fence));
    // This bounds every GPU wait; never reuse mapped texture after failed fence.
    VkResult wait=vkWaitForFences(ed_vk_device,1,&ed_vk_fence,VK_TRUE,2000000000ULL);
    if(wait!=VK_SUCCESS) { ed_vk_error=wait;ed_vk_operation="frame fence";ed_vk_lost=1;return 0; }
#ifndef ED_VK_OFFSCREEN
    VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.swapchainCount=1,.pSwapchains=&ed_vk_swap,.pImageIndices=&index};
    ED_VK_TRY(vkQueuePresentKHR(ed_vk_queue,&present));
    if(ed_vk_flip_error) {
        ed_vk_error=VK_ERROR_SURFACE_LOST_KHR;ed_vk_operation="VideoOut flip";ed_vk_lost=1;return 0;
    }
#endif
    // Keep driver evidence through the first real submit/fence/flip.
#ifndef ED_VK_OFFSCREEN
    if (!ed_vk_presented) {
        extern void vk_ps4_log_close(void);vk_ps4_log_close();
    }
#endif
    ed_vk_presented=1;
    return 1;
}
#undef ED_VK_TRY
