#include "OpenXrApp.h"
#include "../util/XrLog.h"
#if __has_include(<openxr/openxr.h>)
#include <openxr/openxr.h>
#define HAS_OPENXR 1
#else
#define HAS_OPENXR 0
#endif
#include <GLES3/gl3.h>
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
    GLuint fbo=0; glGenFramebuffers(1,&fbo);
    while(running_){
        session_.pollEvents();
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState fs{XR_TYPE_FRAME_STATE}; auto wr=xrWaitFrame(session_.session(), &wi, &fs); XR_LOGI("DDDVR/OpenXRRenderer","xrWaitFrame=%d",wr);
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO}; auto br=xrBeginFrame(session_.session(), &bi); XR_LOGI("DDDVR/OpenXRRenderer","xrBeginFrame=%d",br);

        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; li.displayTime=fs.predictedDisplayTime; li.space=session_.appSpace();
        XrViewState vs{XR_TYPE_VIEW_STATE}; uint32_t count=0; auto lr=xrLocateViews(session_.session(), &li, &vs, (uint32_t)views.size(), &count, views.data()); XR_LOGI("DDDVR/OpenXRRenderer","xrLocateViews=%d count=%u",lr,count);

        int idx = swapchain_.acquireImage(); XR_LOGI("DDDVR/OpenXRRenderer","swapchain image=%d",idx);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        // Render each eye to same image as debug path.
        renderer_.renderEye(0, swapchain_.width(), swapchain_.height());
        renderer_.renderEye(1, swapchain_.width(), swapchain_.height());
        auto fb = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        XR_LOGI("DDDVR/OpenXRRenderer","fbo status=0x%x glErr=0x%x",fb,glGetError());
        swapchain_.releaseImage();

        XrCompositionLayerProjectionView projectionViews[2]{};
        for (int eye=0; eye<2; ++eye) {
            projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projectionViews[eye].pose = views[eye].pose;
            projectionViews[eye].fov = views[eye].fov;
            projectionViews[eye].subImage.swapchain = swapchain_.handle();
            projectionViews[eye].subImage.imageRect.offset = {0,0};
            projectionViews[eye].subImage.imageRect.extent = {swapchain_.width(), swapchain_.height()};
            projectionViews[eye].subImage.imageArrayIndex = eye;
        }
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = session_.appSpace();
        layer.viewCount = 2;
        layer.views = projectionViews;
        const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};

        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO}; ei.displayTime=fs.predictedDisplayTime; ei.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE; ei.layerCount=1; ei.layers=layers;
        auto er=xrEndFrame(session_.session(), &ei); XR_LOGI("DDDVR/OpenXRRenderer","xrEndFrame=%d",er);
    }
#else
    while(running_){ std::this_thread::sleep_for(std::chrono::milliseconds(16)); }
#endif
}
