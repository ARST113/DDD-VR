#include "OpenXrApp.h"
#include "../util/XrLog.h"
#if __has_include(<openxr/openxr.h>)
#include <openxr/openxr.h>
#define HAS_OPENXR 1
#else
#define HAS_OPENXR 0
#endif
#include <chrono>

bool OpenXrApp::initialize(){
    if(!session_.initialize()) return false;
    if(!session_.createSession()) return false;
    if(!session_.createReferenceSpace()) return false;
    renderer_.initialize();
#if HAS_OPENXR
    swapchain_.create(session_.session(), 2048, 2048);
#endif
    return true;
}

bool OpenXrApp::start(){ if(!session_.begin()) return false; running_=true; thread_=std::thread(&OpenXrApp::loop,this); return true; }
void OpenXrApp::pause(){ running_=false; }
void OpenXrApp::resume(){ if(!running_){ running_=true; if(!thread_.joinable()) thread_=std::thread(&OpenXrApp::loop,this);} }
void OpenXrApp::destroy(){ running_=false; if(thread_.joinable()) thread_.join(); }

void OpenXrApp::loop(){
#if HAS_OPENXR
    std::vector<XrView> views(2, {XR_TYPE_VIEW});
    while(running_){
        session_.pollEvents();
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState fs{XR_TYPE_FRAME_STATE}; xrWaitFrame(session_.session(), &wi, &fs);
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO}; xrBeginFrame(session_.session(), &bi);
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; li.displayTime=fs.predictedDisplayTime; li.space=session_.appSpace();
        XrViewState vs{XR_TYPE_VIEW_STATE}; uint32_t count=0; xrLocateViews(session_.session(), &li, &vs, (uint32_t)views.size(), &count, views.data());
        swapchain_.acquireImage();
        renderer_.renderFrame();
        swapchain_.releaseImage();
        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO}; ei.displayTime=fs.predictedDisplayTime; ei.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE; ei.layerCount=0; xrEndFrame(session_.session(), &ei);
    }
#else
    while(running_){ std::this_thread::sleep_for(std::chrono::milliseconds(16)); }
#endif
}
