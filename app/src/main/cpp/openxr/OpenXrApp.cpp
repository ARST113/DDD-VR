#include "OpenXrApp.h"
#include "../util/XrLog.h"
#include <openxr/openxr.h>
#include <GLES3/gl3.h>
#include <chrono>

bool OpenXrApp::initialize(){
    XR_LOGI("DDDVR/OpenXRBuild", "built with OpenXR");
    if(!session_.initialize()) return false;
    if(!session_.createSession()) return false;
    if(!session_.createReferenceSpace()) return false;
    renderer_.initialize();
    swapchain_.create(session_.session(), (int32_t)session_.recommendedWidth(), (int32_t)session_.recommendedHeight());
    return true;
}

bool OpenXrApp::start(){ if(!session_.begin()) return false; running_=true; thread_=std::thread(&OpenXrApp::loop,this); return true; }
void OpenXrApp::pause(){ running_=false; }
void OpenXrApp::resume(){ if(!running_){ running_=true; if(!thread_.joinable()) thread_=std::thread(&OpenXrApp::loop,this);} }
void OpenXrApp::destroy(){ running_=false; if(thread_.joinable()) thread_.join(); }

void OpenXrApp::loop(){
    std::vector<XrView> views(2, {XR_TYPE_VIEW});
    GLuint fbo=0; glGenFramebuffers(1,&fbo);
    while(running_){
        session_.pollEvents();
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState fs{XR_TYPE_FRAME_STATE}; auto wr=xrWaitFrame(session_.session(), &wi, &fs); XR_LOGI("DDDVR/OpenXRRenderer","xrWaitFrame=%d",wr);
        if (wr != XR_SUCCESS) { continue; }
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO}; auto br=xrBeginFrame(session_.session(), &bi); XR_LOGI("DDDVR/OpenXRRenderer","xrBeginFrame=%d",br);
        if (br != XR_SUCCESS) { continue; }
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; li.displayTime=fs.predictedDisplayTime; li.space=session_.appSpace();
        XrViewState vs{XR_TYPE_VIEW_STATE}; uint32_t count=0; auto lr=xrLocateViews(session_.session(), &li, &vs, (uint32_t)views.size(), &count, views.data()); XR_LOGI("DDDVR/OpenXRRenderer","xrLocateViews=%d count=%u",lr,count);
        if (lr != XR_SUCCESS) { XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO}; ei.displayTime=fs.predictedDisplayTime; ei.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE; ei.layerCount=0; xrEndFrame(session_.session(), &ei); continue; }
        int idx = swapchain_.acquireImage(); XR_LOGI("DDDVR/OpenXRRenderer","swapchain image=%d tex=%u",idx,swapchain_.activeColorTexture());
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        bool fboOk = true;
        for (int eye=0; eye<2; ++eye) {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, swapchain_.activeColorTexture(), 0, eye);
            auto fb = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            auto err = glGetError();
            XR_LOGI("DDDVR/OpenXRRenderer","eye=%d fbo status=0x%x glErr=0x%x",eye,fb,err);
            if (fb != GL_FRAMEBUFFER_COMPLETE) { fboOk = false; break; }
            renderer_.renderEye(eye, swapchain_.width(), swapchain_.height());
        }
        XrCompositionLayerProjectionView pv[2]{};
        for(int eye=0; eye<2; ++eye){ pv[eye].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW; pv[eye].pose=views[eye].pose; pv[eye].fov=views[eye].fov; pv[eye].subImage.swapchain=swapchain_.handle(); pv[eye].subImage.imageRect.offset={0,0}; pv[eye].subImage.imageRect.extent={swapchain_.width(), swapchain_.height()}; pv[eye].subImage.imageArrayIndex=eye; }
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION}; layer.space=session_.appSpace(); layer.viewCount=2; layer.views=pv;
        const XrCompositionLayerBaseHeader* layers[]={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO}; ei.displayTime=fs.predictedDisplayTime; ei.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE; ei.layerCount=fboOk?1:0; ei.layers=fboOk?layers:nullptr;
        auto er=xrEndFrame(session_.session(), &ei); XR_LOGI("DDDVR/OpenXRRenderer","xrEndFrame=%d",er);
        swapchain_.releaseImage();
    }
}
