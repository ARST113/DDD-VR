#pragma once
class OpenXrSwapchain { public: bool create(); int acquireImage(); void releaseImage(); };
