#include "OpenXrApp.h"
#include "../util/XrLog.h"
#include <GLES3/gl3.h>
#include <chrono>
#include <vector>

bool OpenXrApp::initialize() { XR_LOGI("DDDVR/OpenXR", "OpenXrApp created; init deferred to render thread"); return true; }

bool OpenXrApp::start() {
    pendingStart_ = true;
    if (running_) return initialized_;
    running_ = true;
    sessionRunning_ = false;
    initDone_ = false;
    initOk_ = false;
    initialized_ = false;
    firstFrameSubmitted_ = false;
    startTime_ = std::chrono::steady_clock::now();
    lastWaitLog_ = startTime_;
    XR_LOGI("DDDVR/OpenXR", "OpenXrApp::start requested (pending)");
    thread_ = std::thread(&OpenXrApp::loop, this);
    std::unique_lock<std::mutex> lk(initMutex_);
    initCv_.wait(lk, [this]{ return initDone_; });
    if (!initOk_) {
        XR_LOGE("DDDVR/OpenXR", "OpenXrApp::start failed: %s", lastError_.c_str());
        stopAndJoinThread("init_failed");
        return false;
    }
    return true;
}

bool OpenXrApp::initOnRenderThread() {
    XR_LOGI("DDDVR/OpenXR", "OpenXrApp initializeOnRenderThread begin");
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
    if (!initOk_) {
        XR_LOGI("DDDVR/OpenXR", "stopAndJoinThread reason=init_failed");
        XR_LOGI("DDDVR/OpenXR", "failed initialization cleanup");
        swapchain_.destroy();
        session_.shutdown();
        running_ = false;
        return;
    }
    initialized_ = true;
    if (pendingStart_) {
        XR_LOGI("DDDVR/OpenXR", "pendingStart consumed");
        pendingStart_ = false;
    }
    XR_LOGI("DDDVR/OpenXR", "OpenXrApp started");
    std::vector<XrView> views(2, {XR_TYPE_VIEW});
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    bool loggedInvalidLoopState = false;
    uint64_t frameCounter = 0;

    while (running_) {
        if (!initialized_) {
            if (!loggedInvalidLoopState) {
                XR_LOGE("DDDVR/OpenXR", "OpenXrApp::loop invalid state: initialized=false");
                loggedInvalidLoopState = true;
            }
        frameCounter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        session_.pollEvents();
        if (!sessionRunning_ && session_.currentState() == XR_SESSION_STATE_READY) {
            XR_LOGI("DDDVR/OpenXRSession", "XR_SESSION_STATE_READY");
            sessionRunning_ = session_.begin();
            XR_LOGI("DDDVR/OpenXRSession", "sessionRunning=%s", sessionRunning_ ? "true" : "false");
        }
        if (sessionRunning_ && session_.currentState() == XR_SESSION_STATE_STOPPING) {
            XR_LOGI("DDDVR/OpenXRSession", "XR_SESSION_STATE_STOPPING");
            session_.end();
            sessionRunning_ = false;
            continue;
        }
        if (session_.currentState() == XR_SESSION_STATE_EXITING || session_.currentState() == XR_SESSION_STATE_LOSS_PENDING) {
            XR_LOGI("DDDVR/OpenXR", "OpenXrApp stop reason=session state %d", session_.currentState());
            running_ = false;
            XR_LOGI("DDDVR/OpenXR", "stopAndJoinThread reason=session_exit");
            break;
        }
        if (!sessionRunning_) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastWaitLog_ > std::chrono::seconds(1)) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
                XR_LOGI("DDDVR/OpenXR", "waiting READY state=%d elapsed=%lld running=%d initialized=%d sessionRunning=%d", session_.currentState(), (long long)elapsed, running_ ? 1 : 0, initialized_ ? 1 : 0, sessionRunning_ ? 1 : 0);
                lastWaitLog_ = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState fs{XR_TYPE_FRAME_STATE}; XrResult wr = xrWaitFrame(session_.session(), &wi, &fs); if (frameCounter < 10 || frameCounter % 120 == 0 || wr != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "xrWaitFrame result=%d", wr); if (wr != XR_SUCCESS) continue;
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO}; XrResult br = xrBeginFrame(session_.session(), &bi); if (frameCounter < 10 || frameCounter % 120 == 0 || br != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "xrBeginFrame result=%d", br); if (br != XR_SUCCESS) continue;
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; li.displayTime = fs.predictedDisplayTime; li.space = session_.appSpace(); XrViewState vs{XR_TYPE_VIEW_STATE}; uint32_t count = 0; XrResult lr = xrLocateViews(session_.session(), &li, &vs, (uint32_t)views.size(), &count, views.data()); if (frameCounter < 10 || frameCounter % 120 == 0 || lr != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "xrLocateViews result=%d", lr);
        bool acquired = false; bool fboOk = false;
        if (lr == XR_SUCCESS && swapchain_.acquireImage()) { acquired = true; glBindFramebuffer(GL_FRAMEBUFFER, fbo); fboOk = true; for (int eye=0; eye<2; ++eye){ auto pre=glGetError(); glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, swapchain_.activeColorTexture(),0,eye); auto fb=glCheckFramebufferStatus(GL_FRAMEBUFFER); auto post=glGetError(); if (frameCounter < 10 || frameCounter % 120 == 0 || fb!=GL_FRAMEBUFFER_COMPLETE || post!=GL_NO_ERROR) XR_LOGI("DDDVR/OpenXRRenderer","eye=%d imageArrayIndex=%d tex=%u viewport=%dx%d swap=%dx%d fbo status=0x%x glErrPre=0x%x glErrPost=0x%x",eye,eye,swapchain_.activeColorTexture(),swapchain_.width(),swapchain_.height(),swapchain_.width(),swapchain_.height(),fb,pre,post); if (fb==GL_FRAMEBUFFER_COMPLETE){ XR_LOGI("DDDVR/OpenXRCheck", "FBO_OK"); }
            if (fb!=GL_FRAMEBUFFER_COMPLETE){ XR_LOGE("DDDVR/OpenXRCheck", "FBO_FAIL"); XR_LOGE("DDDVR/OpenXR","CURRENT_BLOCKER: FBO incomplete eye=%d status=0x%x glErr=0x%x",eye,fb,post); fboOk=false; break;} renderer_.renderEye(eye, swapchain_.width(), swapchain_.height()); } }
        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO}; ei.displayTime = fs.predictedDisplayTime; ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; ei.layerCount = 0;
        XrCompositionLayerProjectionView pv[2]{}; XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION}; const XrCompositionLayerBaseHeader* layers[1];
        if (lr==XR_SUCCESS && acquired && fboOk){ for(int eye=0;eye<2;++eye){pv[eye].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;pv[eye].pose=views[eye].pose;pv[eye].fov=views[eye].fov;pv[eye].subImage.swapchain=swapchain_.handle();pv[eye].subImage.imageRect.extent={swapchain_.width(),swapchain_.height()};pv[eye].subImage.imageArrayIndex=eye;} layer.space=session_.appSpace();layer.viewCount=2;layer.views=pv;layers[0]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer); ei.layerCount=1; ei.layers=layers; }
        if (acquired) {
            swapchain_.releaseImage();
        }
        XrResult er = xrEndFrame(session_.session(), &ei); if (frameCounter < 10 || frameCounter % 120 == 0 || er != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "xrEndFrame result=%d", er);
        if (er == XR_SUCCESS && !firstFrameSubmitted_) {
            XR_LOGI("DDDVR/OpenXRCheck", "FRAME_LOOP_OK");
            firstFrameSubmitted_ = true;
            XR_LOGI("DDDVR/OpenXR", "first successful xrEndFrame");
        }
    }
    XR_LOGI("DDDVR/OpenXR", "swapchain destroy reason=loop exit");
    swapchain_.destroy();
    XR_LOGI("DDDVR/OpenXRCheck", "SUMMARY shutdown");
    XR_LOGI("DDDVR/OpenXR", "normal shutdown");
    XR_LOGI("DDDVR/OpenXR", "OpenXrSession destroy reason=loop exit");
    session_.shutdown();
}

void OpenXrApp::stopAndJoinThread(const char* reason){ XR_LOGI("DDDVR/OpenXR", "stopAndJoinThread reason=%s", reason); running_=false; if(thread_.joinable()) thread_.join(); sessionRunning_=false; }
void OpenXrApp::pause(){ XR_LOGI("DDDVR/OpenXR", "OpenXrApp::pause requested"); }
void OpenXrApp::resume(){ XR_LOGI("DDDVR/OpenXR", "OpenXrApp::resume requested"); }
void OpenXrApp::destroy(){ XR_LOGI("DDDVR/OpenXR", "OpenXrApp::destroy requested"); stopAndJoinThread("destroy"); }
