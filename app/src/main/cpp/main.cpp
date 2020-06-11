#include <jni.h>
#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>
#include <glm/glm.hpp>

#define TAG "MoleculeVRNativeMain"
#define LOG(...) ((void)__android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__))

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;
};

const std::vector<Vertex> vertices = {
        {{0.0f,  -0.5f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f,  0.5f},  {0.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f},  {0.0f, 0.0f, 1.0f}}
};

android_app *app;
VkInstance instance;
VkDebugUtilsMessengerEXT messenger;
VkSurfaceKHR surface;
VkPhysicalDevice physicalDevice;
VkDevice device;
VkQueue queue;
VkCommandPool commandPool;
VkSwapchainKHR swapchain;
VkExtent2D swapchainExtent;
uint32_t imageCount, currentImage;
std::vector<VkImage> swapchainImages;
std::vector<VkImageView> swapchainImageViews;
VkShaderModule vertexShader, fragmentShader;
VkRenderPass renderPass;
VkPipelineLayout pipelineLayout;
VkPipeline graphicsPipeline;
std::vector<VkFramebuffer> framebuffers;
VkBuffer vertexBuffer;
VkDeviceMemory vertexMemory;
std::vector<VkCommandBuffer> commandBuffers;
std::vector<VkFence> frameFences, orderFences;
std::vector<VkSemaphore> availableSemaphores, finishedSemaphores;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData) {
    (void) pUserData;
    int severity;
    const char *type;
    if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
        severity = ANDROID_LOG_VERBOSE;
    else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        severity = ANDROID_LOG_INFO;
    else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        severity = ANDROID_LOG_WARN;
    else
        severity = ANDROID_LOG_ERROR;
    if (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
        type = "ValidationLayerGeneral";
    else if (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
        type = "ValidationLayerValidation";
    else
        type = "ValidationLayerPerformance";
    __android_log_print(severity, type, "%s\n", pCallbackData->pMessage);
    return VK_FALSE;
}

void initialize() {
    std::vector<const char *> layers, extensions;
    layers.push_back("VK_LAYER_KHRONOS_validation");
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "MoleculeVR";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.pEngineName = "ETUGraphics";
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_1;

    VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
    messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = debugCallback;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    instanceInfo.pNext = &messengerInfo;

    VkAndroidSurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.window = app->window;

    vkCreateInstance(&instanceInfo, nullptr, &instance);
    auto createDebugUtilsMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    createDebugUtilsMessenger(instance, &messengerInfo, nullptr, &messenger);
    vkCreateAndroidSurfaceKHR(instance, &surfaceInfo, nullptr, &surface);
}

void pickDevice() {
    uint32_t deviceCount;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());
    physicalDevice = physicalDevices.at(0);

    float queuePriority = 1.0f;
    VkPhysicalDeviceFeatures deviceFeatures{};
    std::vector<const char *> deviceLayers, deviceExtensions;
    deviceLayers.push_back("VK_LAYER_KHRONOS_validation");
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);

    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = 0;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.pEnabledFeatures = &deviceFeatures;
    deviceInfo.enabledLayerCount = static_cast<uint32_t>(deviceLayers.size());
    deviceInfo.ppEnabledLayerNames = deviceLayers.data();
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = 0;

    vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device);
    vkGetDeviceQueue(device, 0, 0, &queue);
    vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
}

void createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    imageCount = capabilities.minImageCount;
    swapchainExtent = capabilities.currentExtent;

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.oldSwapchain = VK_NULL_HANDLE;
    swapchainInfo.minImageCount = capabilities.minImageCount;
    swapchainInfo.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.imageExtent = swapchainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
    swapchainImageViews.resize(imageCount);

    for (size_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageInfo.image = swapchainImages[i];
        imageInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageInfo.subresourceRange.baseMipLevel = 0;
        imageInfo.subresourceRange.levelCount = 1;
        imageInfo.subresourceRange.baseArrayLayer = 0;
        imageInfo.subresourceRange.layerCount = 1;

        vkCreateImageView(device, &imageInfo, nullptr, &swapchainImageViews[i]);
    }
}

void createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);
}

VkShaderModule readShader(const char *path) {
    AAssetManager *manager = app->activity->assetManager;
    AAsset *asset = AAssetManager_open(manager, path, AASSET_MODE_BUFFER);
    off_t size = AAsset_getLength(asset);
    std::vector<uint32_t> code(size / 4);
    AAsset_read(asset, code.data(), size);
    AAsset_close(asset);

    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = size;
    shaderInfo.pCode = code.data();

    VkShaderModule shader;
    vkCreateShaderModule(device, &shaderInfo, nullptr, &shader);
    return shader;
}

void createPipeline() {
    vertexShader = readShader("shaders/shader.vert.spv");
    fragmentShader = readShader("shaders/shader.frag.spv");

    VkPipelineShaderStageCreateInfo vertexInfo{};
    vertexInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexInfo.module = vertexShader;
    vertexInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentInfo{};
    fragmentInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentInfo.module = fragmentShader;
    fragmentInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertexInfo, fragmentInfo};

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2]{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo inputInfo{};
    inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    inputInfo.vertexBindingDescriptionCount = 1;
    inputInfo.pVertexBindingDescriptions = &bindingDescription;
    inputInfo.vertexAttributeDescriptionCount = 2;
    inputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
    assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    assemblyInfo.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) swapchainExtent.width;
    viewport.height = (float) swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportInfo{};
    viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportInfo.viewportCount = 1;
    viewportInfo.pViewports = &viewport;
    viewportInfo.scissorCount = 1;
    viewportInfo.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizerInfo{};
    rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizerInfo.depthClampEnable = VK_FALSE;
    rasterizerInfo.rasterizerDiscardEnable = VK_FALSE;
    rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizerInfo.lineWidth = 1.0f;
    rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizerInfo.depthBiasEnable = VK_FALSE;
    rasterizerInfo.depthBiasConstantFactor = 0.0f;
    rasterizerInfo.depthBiasClamp = 0.0f;
    rasterizerInfo.depthBiasSlopeFactor = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisamplingInfo{};
    multisamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisamplingInfo.sampleShadingEnable = VK_FALSE;
    multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisamplingInfo.minSampleShading = 1.0f;
    multisamplingInfo.pSampleMask = nullptr;
    multisamplingInfo.alphaToCoverageEnable = VK_FALSE;
    multisamplingInfo.alphaToOneEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                     VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT |
                                     VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blendInfo{};
    blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendInfo.logicOpEnable = VK_FALSE;
    blendInfo.logicOp = VK_LOGIC_OP_COPY;
    blendInfo.attachmentCount = 1;
    blendInfo.pAttachments = &blendAttachment;
    blendInfo.blendConstants[0] = 0.0f;
    blendInfo.blendConstants[1] = 0.0f;
    blendInfo.blendConstants[2] = 0.0f;
    blendInfo.blendConstants[3] = 0.0f;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pSetLayouts = nullptr;
    layoutInfo.pushConstantRangeCount = 0;
    layoutInfo.pPushConstantRanges = nullptr;

    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &inputInfo;
    pipelineInfo.pInputAssemblyState = &assemblyInfo;
    pipelineInfo.pViewportState = &viewportInfo;
    pipelineInfo.pRasterizationState = &rasterizerInfo;
    pipelineInfo.pMultisampleState = &multisamplingInfo;
    pipelineInfo.pDepthStencilState = nullptr;
    pipelineInfo.pColorBlendState = &blendInfo;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);
}

void createFramebuffers() {
    framebuffers.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        VkImageView attachments[] = {swapchainImageViews[i]};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;
        vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]);
    }
}

uint32_t chooseMemoryType(uint32_t filter, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; index++)
        if ((filter & (1 << index)) &&
            (memoryProperties.memoryTypes[index].propertyFlags & flags) == flags)
            return index;

    return UINT_MAX;
}

void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties, VkBuffer &buffer,
                  VkDeviceMemory &bufferMemory) {
    VkMemoryRequirements memoryRequirements;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.usage = usage;
    bufferInfo.size = size;

    vkCreateBuffer(device, &bufferInfo, NULL, &buffer);
    vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = chooseMemoryType(memoryRequirements.memoryTypeBits, properties);

    vkAllocateMemory(device, &allocateInfo, NULL, &bufferMemory);
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

VkCommandBuffer beginSingleTimeCommand() {
    VkCommandBufferAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandPool = commandPool;
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void endSingleTimeCommand(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommand();

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;

    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    endSingleTimeCommand(commandBuffer);
}

void createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(Vertex) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingMemory);

    void *data;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t) bufferSize);
    vkUnmapMemory(device, stagingMemory);

    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexMemory);
    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
}

void createCommandBuffers() {
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = imageCount;

    commandBuffers.resize(imageCount);
    vkAllocateCommandBuffers(device, &allocateInfo, commandBuffers.data());

    for (size_t i = 0; i < imageCount; i++) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pInheritanceInfo = nullptr;

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[i];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchainExtent;

        VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        VkBuffer vertexBuffers[] = {vertexBuffer};
        VkDeviceSize offsets[] = {0};

        vkBeginCommandBuffer(commandBuffers[i], &beginInfo);
        vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);
        vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffers[i]);
        vkEndCommandBuffer(commandBuffers[i]);
    }
}

void createSyncObject() {
    currentImage = 0;

    frameFences.resize(imageCount);
    orderFences.resize(imageCount, VK_NULL_HANDLE);
    availableSemaphores.resize(imageCount);
    finishedSemaphores.resize(imageCount);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < imageCount; i++) {
        vkCreateFence(device, &fenceInfo, nullptr, &frameFences[i]);
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &availableSemaphores[i]);
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &finishedSemaphores[i]);
    }
}

void setup() {
    initialize();
    pickDevice();
    createSwapchain();
    createRenderPass();
    createPipeline();
    createFramebuffers();
    createVertexBuffer();
    createCommandBuffers();
    createSyncObject();
}

void draw() {
    vkWaitForFences(device, 1, &frameFences[currentImage], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, availableSemaphores[currentImage],
                          VK_NULL_HANDLE, &imageIndex);

    if (orderFences[imageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(device, 1, &orderFences[imageIndex], VK_TRUE, UINT64_MAX);

    orderFences[imageIndex] = frameFences[currentImage];

    VkSemaphore waitSemaphores[] = {availableSemaphores[currentImage]};
    VkSemaphore signalSemaphores[] = {finishedSemaphores[currentImage]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkResetFences(device, 1, &frameFences[currentImage]);
    vkQueueSubmit(queue, 1, &submitInfo, frameFences[currentImage]);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr;

    vkQueuePresentKHR(queue, &presentInfo);
    currentImage = (currentImage + 1) % imageCount;
}

void clear() {
    vkDeviceWaitIdle(device);
    for (size_t i = 0; i < imageCount; i++) {
        vkDestroySemaphore(device, finishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, availableSemaphores[i], nullptr);
        vkDestroyFence(device, frameFences[i], nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexMemory, nullptr);
    for (auto framebuffer : framebuffers)
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    vkDestroyShaderModule(device, fragmentShader, nullptr);
    vkDestroyShaderModule(device, vertexShader, nullptr);
    for (auto imageView : swapchainImageViews)
        vkDestroyImageView(device, imageView, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    auto destroyDebugUtilsMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    destroyDebugUtilsMessenger(instance, messenger, nullptr);
    vkDestroyInstance(instance, nullptr);
}

void handle_cmd(android_app *pApp, int32_t cmd) {
    if (cmd == APP_CMD_INIT_WINDOW) {
        app = pApp;
        setup();
        pApp->userData = (void *) 1;
    } else if (cmd == APP_CMD_TERM_WINDOW) {
        pApp->userData = nullptr;
        clear();
        app = nullptr;
    }
}

extern "C" void android_main(struct android_app *pApp) {
    pApp->onAppCmd = handle_cmd;

    int events;
    android_poll_source *pSource;


    uint32_t previousFrame = 0, currentFrame = 0;
    uint64_t currentTime, previousTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    do {
        if (ALooper_pollAll(0, nullptr, &events, (void **) &pSource) >= 0)
            if (pSource)
                pSource->process(pApp, pSource);

        if (pApp->userData) {
            draw();

            currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

            if (currentTime - previousTime > 1000) {
                LOG("FPS: %d\n", currentFrame - previousFrame);
                previousFrame = currentFrame;
                previousTime = currentTime;
            }

            currentFrame++;
        }
    } while (!pApp->destroyRequested);
}
