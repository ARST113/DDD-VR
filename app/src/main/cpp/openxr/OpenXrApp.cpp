#include "OpenXrApp.h"
#include "../util/XrLog.h"
#include <GLES3/gl3.h>
#include <chrono>
#include <vector>

bool OpenXrApp::initialize() { XR_LOGI("DDDVR/OpenXR", "OpenXrApp created; init deferred to render thread"); return true; }

bool OpenXrApp::start() {
    if (!initialized_) {
        XR_LOGE("DDDVR/OpenXR", "OpenXrApp::start skipped: not initialized");
        return false;
    }
    if (!session_.hasInstance()) {
        XR_LOGE("DDDVR/OpenXR", "OpenXrApp::start skipped: invalid XrInstance");
        return false;
    }
    if (running_) return true;
    running_ = true; sessionRunning_ = false; initDone_ = false; initOk_ = false; initialized_ = false;
    thread_ = std::thread(&OpenXrApp::loop, this);
    std::unique_lock<std::mutex> lk(initMutex_);
    initCv_.wait(lk, [this] { return initDone_; });
    if (!initOk_) { XR_LOGE("DDDVR/OpenXR", "OpenXrApp::start failed: %s", lastError_.c_str()); stopAndJoinThread(); return false; }
    initialized_ = true;
    return true;
}

bool OpenXrApp::initOnRenderThread() {
    auto fail = [this](const char* phase, const std::string& reason) { lastError_ = reason; XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER: %s failed: %s", phase, reason.c_str()); return false; };
    if (!session_.initializeLoaderAndInstance()) return fail("InstanceReady", session_.lastError());
    if (!session_.prepareGraphics()) return fail("GraphicsReady", session_.lastError());
    if (!session_.createSession()) return fail("SessionReady", session_.lastError());
    if (!session_.createReferenceSpace()) return fail("SpaceReady", session_.lastError());
    renderer_.initialize(); XR_LOGI("DDDVR/OpenXRRenderer", "debug cinema quad mode"); XR_LOGI("DDDVR/OpenXRRenderer", "no video texture attached yet");
    if (!swapchain_.create(session_.session(), (int32_t)session_.recommendedWidth(), (int32_t)session_.recommendedHeight())) return fail("SwapchainReady", "swapchain create failed");
    return true;
}

void OpenXrApp::loop() {
    initOk_ = initOnRenderThread();
    { std::lock_guard<std::mutex> lk(initMutex_); initDone_ = true; }
    initCv_.notify_all();
    if (!initOk_) { running_ = false; return; }
    std::vector<XrView> views(2, {XR_TYPE_VIEW});
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    bool loggedInvalidLoopState = false;

    while (running_) {
        if (!initialized_) {
            if (!loggedInvalidLoopState) {
                XR_LOGE("DDDVR/OpenXR", "OpenXrApp::loop invalid state: initialized=false");
                loggedInvalidLoopState = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        session_.pollEvents();
        if (!sessionRunning_ && session_.currentState() == XR_SESSION_STATE_READY) sessionRunning_ = session_.begin();
        if (!sessionRunning_) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState fs{XR_TYPE_FRAME_STATE}; XrResult wr = xrWaitFrame(session_.session(), &wi, &fs); XR_LOGI("DDDVR/OpenXRRenderer", "xrWaitFrame=%d", wr); if (wr != XR_SUCCESS) continue;
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO}; XrResult br = xrBeginFrame(session_.session(), &bi); XR_LOGI("DDDVR/OpenXRRenderer", "xrBeginFrame=%d", br); if (br != XR_SUCCESS) continue;
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; li.displayTime = fs.predictedDisplayTime; li.space = session_.appSpace(); XrViewState vs{XR_TYPE_VIEW_STATE}; uint32_t count = 0; XrResult lr = xrLocateViews(session_.session(), &li, &vs, (uint32_t)views.size(), &count, views.data()); XR_LOGI("DDDVR/OpenXRRenderer", "xrLocateViews=%d", lr);
        bool acquired = false; bool fboOk = false;
        if (lr == XR_SUCCESS && swapchain_.acquireImage()) { acquired = true; glBindFramebuffer(GL_FRAMEBUFFER, fbo); fboOk = true; for (int eye=0; eye<2; ++eye){ auto pre=glGetError(); glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, swapchain_.activeColorTexture(),0,eye); auto fb=glCheckFramebufferStatus(GL_FRAMEBUFFER); auto post=glGetError(); XR_LOGI("DDDVR/OpenXRRenderer","eye=%d imageArrayIndex=%d tex=%u viewport=%dx%d swap=%dx%d fbo status=0x%x glErrPre=0x%x glErrPost=0x%x",eye,eye,swapchain_.activeColorTexture(),swapchain_.width(),swapchain_.height(),swapchain_.width(),swapchain_.height(),fb,pre,post); if (fb!=GL_FRAMEBUFFER_COMPLETE){XR_LOGE("DDDVR/OpenXR","CURRENT_BLOCKER: FBO incomplete eye=%d status=0x%x glErr=0x%x",eye,fb,post); fboOk=false; break;} renderer_.renderEye(eye, swapchain_.width(), swapchain_.height()); } }
        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO}; ei.displayTime = fs.predictedDisplayTime; ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; ei.layerCount = 0;
        XrCompositionLayerProjectionView pv[2]{}; XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION}; const XrCompositionLayerBaseHeader* layers[1];
        if (lr==XR_SUCCESS && acquired && fboOk){ for(int eye=0;eye<2;++eye){pv[eye].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;pv[eye].pose=views[eye].pose;pv[eye].fov=views[eye].fov;pv[eye].subImage.swapchain=swapchain_.handle();pv[eye].subImage.imageRect.extent={swapchain_.width(),swapchain_.height()};pv[eye].subImage.imageArrayIndex=eye;} layer.space=session_.appSpace();layer.viewCount=2;layer.views=pv;layers[0]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer); ei.layerCount=1; ei.layers=layers; }
        XrResult er = xrEndFrame(session_.session(), &ei); XR_LOGI("DDDVR/OpenXRRenderer", "xrEndFrame=%d", er);
        if (acquired) swapchain_.releaseImage();
    }
    swapchain_.destroy(); session_.shutdown();
}

void OpenXrApp::stopAndJoinThread(){ running_=false; if(thread_.joinable()) thread_.join(); sessionRunning_=false; }
void OpenXrApp::pause(){ stopAndJoinThread(); }
void OpenXrApp::resume(){ if (!running_) start(); }
void OpenXrApp::destroy(){ stopAndJoinThread(); }
