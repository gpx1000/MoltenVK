/*
 * MVKSwapchain.h
 *
 * Copyright (c) 2015-2023 The Brenwill Workshop Ltd. (http://www.brenwill.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "MVKDevice.h"
#include "MVKImage.h"
#include "MVKSmallVector.h"
#include <mutex>

#import "CAMetalLayer+MoltenVK.h"
#import <Metal/Metal.h>

class MVKWatermark;

@class MVKBlockObserver;


#pragma mark -
#pragma mark MVKSwapchain

/** Represents a Vulkan swapchain. */
class MVKSwapchain : public MVKVulkanAPIDeviceObject {

public:

	/** Returns the Vulkan type of this object. */
	VkObjectType getVkObjectType() override { return VK_OBJECT_TYPE_SWAPCHAIN_KHR; }

	/** Returns the debug report object type of this object. */
	VkDebugReportObjectTypeEXT getVkDebugReportObjectType() override { return VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT; }

	/** Returns the number of images in this swapchain. */
	inline uint32_t getImageCount() { return (uint32_t)_presentableImages.size(); }

	/** Returns the image at the specified index. */
	inline MVKPresentableSwapchainImage* getPresentableImage(uint32_t index) { return _presentableImages[index]; }

	/**
	 * Returns the array of presentable images associated with this swapchain.
	 *
	 * If pSwapchainImages is null, the value of pCount is updated with the number of
	 * presentable images associated with this swapchain.
	 *
	 * If pSwapchainImages is not null, then pCount images are copied into the array.
	 * If the number of available images is less than pCount, the value of pCount is
	 * updated to indicate the number of images actually returned in the array.
	 *
	 * Returns VK_SUCCESS if successful. Returns VK_INCOMPLETE if the number of supported
	 * images is larger than pCount. Returns other values if an error occurs.
	 */
	VkResult getImages(uint32_t* pCount, VkImage* pSwapchainImages);

	/** Returns the index of the next acquireable image. */
	VkResult acquireNextImage(uint64_t timeout,
							  VkSemaphore semaphore,
							  VkFence fence,
							  uint32_t deviceMask,
							  uint32_t* pImageIndex);

	/** Releases swapchain images. */
	VkResult releaseImages(const VkReleaseSwapchainImagesInfoEXT* pReleaseInfo);

	/** Returns whether the parent surface is now lost and this swapchain must be recreated. */
	bool getIsSurfaceLost() { return _surfaceLost; }

	/** Returns whether this swapchain is optimally sized for the surface. */
	bool hasOptimalSurface();

	/** Returns the status of the surface. Surface loss takes precedence over sub-optimal errors. */
	VkResult getSurfaceStatus() {
		if (_device->getConfigurationResult() != VK_SUCCESS) { return _device->getConfigurationResult(); }
		if (getIsSurfaceLost()) { return VK_ERROR_SURFACE_LOST_KHR; }
		if ( !hasOptimalSurface() ) { return VK_SUBOPTIMAL_KHR; }
		return VK_SUCCESS;
	}

	/** Adds HDR metadata to this swapchain. */
	void setHDRMetadataEXT(const VkHdrMetadataEXT& metadata);
	
	/** VK_GOOGLE_display_timing - returns the duration of the refresh cycle */
	VkResult getRefreshCycleDuration(VkRefreshCycleDurationGOOGLE *pRefreshCycleDuration);
	
	/** VK_GOOGLE_display_timing - returns past presentation times */
	VkResult getPastPresentationTiming(uint32_t *pCount, VkPastPresentationTimingGOOGLE *pPresentationTimings);

	/** Marks parts of the underlying CAMetalLayer as needing update. */
	void setLayerNeedsDisplay(const VkPresentRegionKHR* pRegion);

	void destroy() override;

#pragma mark Construction
	
	MVKSwapchain(MVKDevice* device, const VkSwapchainCreateInfoKHR* pCreateInfo);

	~MVKSwapchain() override;

protected:
	friend class MVKPresentableSwapchainImage;

	void propagateDebugName() override;
	void initCAMetalLayer(const VkSwapchainCreateInfoKHR* pCreateInfo,
						  VkSwapchainPresentScalingCreateInfoEXT* pScalingInfo,
						  uint32_t imgCnt);
	void initSurfaceImages(const VkSwapchainCreateInfoKHR* pCreateInfo, uint32_t imgCnt);
	void releaseLayer();
	void releaseUndisplayedSurfaces();
	uint64_t getNextAcquisitionID();
    void willPresentSurface(id<MTLTexture> mtlTexture, id<MTLCommandBuffer> mtlCmdBuff);
    void renderWatermark(id<MTLTexture> mtlTexture, id<MTLCommandBuffer> mtlCmdBuff);
    void markFrameInterval();
	void recordPresentTime(const MVKImagePresentInfo& presentInfo, uint64_t actualPresentTime = 0);

	CAMetalLayer* _mtlLayer = nil;
    MVKWatermark* _licenseWatermark = nullptr;
	MVKSmallVector<MVKPresentableSwapchainImage*, kMVKMaxSwapchainImageCount> _presentableImages;
	MVKSmallVector<VkPresentModeKHR, 2> _compatiblePresentModes;
	static const int kMaxPresentationHistory = 60;
	VkPastPresentationTimingGOOGLE _presentTimingHistory[kMaxPresentationHistory];
	std::atomic<uint64_t> _currentAcquisitionID = 0;
    MVKBlockObserver* _layerObserver = nil;
	std::mutex _presentHistoryLock;
	std::mutex _layerLock;
	uint64_t _lastFrameTime = 0;
	VkExtent2D _mtlLayerDrawableExtent = {0, 0};
	uint32_t _currentPerfLogFrameCount = 0;
	uint32_t _presentHistoryCount = 0;
	uint32_t _presentHistoryIndex = 0;
	uint32_t _presentHistoryHeadIndex = 0;
	std::atomic<bool> _surfaceLost = false;
	bool _isDeliberatelyScaled = false;
};


#pragma mark -
#pragma mark Support functions

/**
 * Returns the natural extent of the CAMetalLayer.
 *
 * The natural extent is the size of the bounds property of the layer,
 * multiplied by the contentsScale property of the layer, rounded
 * to nearest integer using half-to-even rounding.
 */
static inline VkExtent2D mvkGetNaturalExtent(CAMetalLayer* mtlLayer) {
	return mvkVkExtent2DFromCGSize(mtlLayer.naturalDrawableSizeMVK);
}
