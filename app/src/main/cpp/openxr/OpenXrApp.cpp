#include "OpenXrApp.h"
#include "../util/XrLog.h"
#include <GLES3/gl3.h>
#include <chrono>
#include <vector>

bool OpenXrApp::initialize() { XR_LOGI("DDDVR/OpenXR", "OpenXrApp created; init deferred to render thread"); return true; }

bool OpenXrApp::start() {
    pendingStart_ = true;
    if (running_) return true;
    running_ = true;
    sessionRunning_ = false;
    initOk_ = false;
    initialized_ = false;
    firstFrameSubmitted_ = false;
    exitRequested_ = false;
    restartRequested_ = false;
    stoppedBySeethroughOrFocusLoss_ = false;
    frameCount_ = 0;
    frameCountBeforeStop_ = 0;
    beginSessionCount_ = 0;
    endSessionCount_ = 0;
    fboOkSeen_ = false;
    startTime_ = std::chrono::steady_clock::now();
    lastWaitLog_ = startTime_;
    XR_LOGI("DDDVR/OpenXR", "OpenXrApp::start requested (pending)");
    thread_ = std::thread(&OpenXrApp::loop, this);
    XR_LOGI("DDDVR/OpenXR", "OpenXrApp::start returned without waiting for XR_READY");
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
    if (!initOk_) {
        XR_LOGE("DDDVR/OpenXR", "OpenXrApp::loop initialization failed: %s", lastError_.c_str());
        XR_LOGI("DDDVR/OpenXR", "stopAndJoinThread reason=init_failed");
        XR_LOGI("DDDVR/OpenXR", "failed initialization cleanup");
        swapchain_.destroy();
        session_.shutdown();
        running_ = false;
        return;
    }
    initialized_ = true;

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
        const XrSessionState state = session_.currentState();
        if (!sessionRunning_ && state == XR_SESSION_STATE_READY) {
            XR_LOGI("DDDVR/OpenXRSession", "XR_SESSION_READY_AFTER_STOPPING");
            sessionRunning_ = session_.begin();
            XR_LOGI("DDDVR/OpenXRSession", "sessionRunning=%s", sessionRunning_ ? "true" : "false");
            if (sessionRunning_ && pendingStart_) {
                pendingStart_ = false;
                XR_LOGI("DDDVR/OpenXR", "pendingStart consumed");
                XR_LOGI("DDDVR/OpenXR", "OpenXrApp started");
            }
            if (sessionRunning_) {
                beginSessionCount_ += 1;
                XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_RESUMED_OR_STARTED");
                if (stoppedBySeethroughOrFocusLoss_) {
                    XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_RESUMED_AFTER_SEETHROUGH");
                } else {
                    XR_LOGI("DDDVR/OpenXR", "XR_SESSION_RESUMED_AFTER_STOPPING");
                }
            }
        }
        if (state == XR_SESSION_STATE_STOPPING) {
            XR_LOGI("DDDVR/OpenXRSession", "XR_SESSION_STOPPING_NORMAL frameCount=%llu", (unsigned long long)frameCount_);
            XR_LOGI("DDDVR/OpenXR", "XR_SESSION_STOPPED_BY_RUNTIME");
            if (sessionRunning_) {
                const bool endOk = session_.end();
                if (endOk) {
                    sessionRunning_ = false;
                    endSessionCount_ += 1;
                    frameCountBeforeStop_ = frameCount_;
                    stoppedBySeethroughOrFocusLoss_ = true;
                    XR_LOGI("DDDVR/OpenXR", "XR_SESSION_ENDED_OK frameCountBeforeStop=%llu", (unsigned long long)frameCountBeforeStop_);
                    XR_LOGI("DDDVR/OpenXR", "XR_SESSION_WAITING_FOR_READY_AFTER_STOPPING");
                    XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_WAITING_FOR_READY_AFTER_STOPPING");
                    XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_STOPPED_BY_SEETHROUGH");
                    XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_WAITING_FOR_READY_AFTER_SEETHROUGH");
                    if (frameCount_ > 0) XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_STOPPED_AFTER_SUCCESSFUL_FRAME_LOOP");
                }
            } else {
                XR_LOGI("DDDVR/OpenXR", "XR_SESSION_STOPPING_IGNORED_NOT_RUNNING");
            }
            continue;
        }
        if (state == XR_SESSION_STATE_EXITING) {
            XR_LOGI("DDDVR/OpenXR", "XR_SESSION_EXITING exitRequested=1 restartRequested=0");
            exitRequested_ = true;
            restartRequested_ = false;
            running_ = false;
            break;
        }
        if (state == XR_SESSION_STATE_LOSS_PENDING) {
            XR_LOGW("DDDVR/OpenXR", "XR_SESSION_LOSS_PENDING exitRequested=1 restartRequested=1");
            exitRequested_ = true;
            restartRequested_ = true;
            running_ = false;
            break;
        }
        if (!sessionRunning_) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastWaitLog_ > std::chrono::seconds(1)) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
                XR_LOGI("DDDVR/OpenXR", "waiting READY state=%d elapsed=%lld running=%d initialized=%d sessionRunning=%d", session_.currentState(), (long long)elapsed, running_ ? 1 : 0, initialized_ ? 1 : 0, sessionRunning_ ? 1 : 0);
                if (session_.currentState() == XR_SESSION_STATE_IDLE && elapsed >= 12) {
                    XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER SESSION_STUCK_IDLE_AFTER_SWAPCHAIN");
                    XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER XR_SESSION_READY_EVENT_NOT_RECEIVED");
                    XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER XR_READY_NOT_RECEIVED");
                }
                if (elapsed >= 8 && !sessionRunning_ && session_.currentState() != XR_SESSION_STATE_READY) {
                    XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER XR_SESSION_READY_EVENT_NOT_RECEIVED state=%d", session_.currentState());
                }
                if (elapsed >= 8 && (session_.currentState() == XR_SESSION_STATE_VISIBLE || session_.currentState() == XR_SESSION_STATE_SYNCHRONIZED)) {
                    XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER XR_VISIBILITY_CHANGED_TO_FALSE state=%d", session_.currentState());
                }
                lastWaitLog_ = now;
            }
            XR_LOGI("DDDVR/OpenXR", "XR_RENDER_SKIPPED_SESSION_NOT_RUNNING state=%d", state);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState fs{XR_TYPE_FRAME_STATE}; XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_BEGIN xrWaitFrame"); XrResult wr = xrWaitFrame(session_.session(), &wi, &fs); if (frameCounter < 10 || frameCounter % 120 == 0 || wr != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_END xrWaitFrame result=%d", wr); if (wr != XR_SUCCESS) continue;
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO}; XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_BEGIN xrBeginFrame"); XrResult br = xrBeginFrame(session_.session(), &bi); if (frameCounter < 10 || frameCounter % 120 == 0 || br != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_END xrBeginFrame result=%d", br); if (br != XR_SUCCESS) continue;
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; li.displayTime = fs.predictedDisplayTime; li.space = session_.appSpace(); XrViewState vs{XR_TYPE_VIEW_STATE}; uint32_t count = 0; XrResult lr = xrLocateViews(session_.session(), &li, &vs, (uint32_t)views.size(), &count, views.data()); if (frameCounter < 10 || frameCounter % 120 == 0 || lr != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "xrLocateViews result=%d", lr);
        bool acquired = false; bool fboOk = false;
        if (lr == XR_SUCCESS && swapchain_.acquireImage()) { acquired = true; glBindFramebuffer(GL_FRAMEBUFFER, fbo); fboOk = true; for (int eye=0; eye<2; ++eye){ auto pre=glGetError(); glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, swapchain_.activeColorTexture(),0,eye); auto fb=glCheckFramebufferStatus(GL_FRAMEBUFFER); auto post=glGetError(); if (frameCounter < 10 || frameCounter % 120 == 0 || fb!=GL_FRAMEBUFFER_COMPLETE || post!=GL_NO_ERROR) XR_LOGI("DDDVR/OpenXRRenderer","eye=%d imageArrayIndex=%d tex=%u viewport=%dx%d swap=%dx%d fbo status=0x%x glErrPre=0x%x glErrPost=0x%x",eye,eye,swapchain_.activeColorTexture(),swapchain_.width(),swapchain_.height(),swapchain_.width(),swapchain_.height(),fb,pre,post); if (fb==GL_FRAMEBUFFER_COMPLETE && !fboOkSeen_){ XR_LOGI("DDDVR/OpenXRCheck", "FBO_OK"); fboOkSeen_=true; }
            if (fb!=GL_FRAMEBUFFER_COMPLETE){ XR_LOGE("DDDVR/OpenXRCheck", "FBO_FAIL status=0x%x glErr=0x%x", fb, post); XR_LOGE("DDDVR/OpenXR","CURRENT_BLOCKER: FBO incomplete eye=%d status=0x%x glErr=0x%x",eye,fb,post); fboOk=false; break;} renderer_.renderEye(eye, swapchain_.width(), swapchain_.height()); } }
        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO}; ei.displayTime = fs.predictedDisplayTime; ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; ei.layerCount = 0;
        XrCompositionLayerProjectionView pv[2]{}; XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION}; const XrCompositionLayerBaseHeader* layers[1];
        if (lr==XR_SUCCESS && acquired && fboOk){ for(int eye=0;eye<2;++eye){pv[eye].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;pv[eye].pose=views[eye].pose;pv[eye].fov=views[eye].fov;pv[eye].subImage.swapchain=swapchain_.handle();pv[eye].subImage.imageRect.extent={swapchain_.width(),swapchain_.height()};pv[eye].subImage.imageArrayIndex=eye;} layer.space=session_.appSpace();layer.viewCount=2;layer.views=pv;layers[0]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer); ei.layerCount=1; ei.layers=layers; }
        if (acquired) {
            swapchain_.releaseImage();
        }
        XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_BEGIN xrEndFrame");
        XrResult er = xrEndFrame(session_.session(), &ei); if (frameCounter < 10 || frameCounter % 120 == 0 || er != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_END xrEndFrame result=%d", er);
        if (er == XR_SUCCESS && !firstFrameSubmitted_) {
            XR_LOGI("DDDVR/OpenXRCheck", "FRAME_LOOP_OK");
            firstFrameSubmitted_ = true;
            XR_LOGI("DDDVR/OpenXR", "first successful xrEndFrame");
        }
        frameCount_ = ++frameCounter;
    }
    XR_LOGI("DDDVR/OpenXR", "XR_LIFECYCLE_SUMMARY initialized=%d sessionState=%d sessionRunning=%d frameCount=%llu frameCountBeforeStop=%llu beginSessionCount=%llu endSessionCount=%llu stoppedBySeethrough=%d resumedAfterSeethrough=%d exitRequested=%d restartRequested=%d",
            initialized_ ? 1 : 0, (int)session_.currentState(), sessionRunning_ ? 1 : 0,
            (unsigned long long)frameCount_, (unsigned long long)frameCountBeforeStop_,
            (unsigned long long)beginSessionCount_, (unsigned long long)endSessionCount_,
            stoppedBySeethroughOrFocusLoss_ ? 1 : 0, (stoppedBySeethroughOrFocusLoss_ && sessionRunning_) ? 1 : 0,
            exitRequested_ ? 1 : 0, restartRequested_ ? 1 : 0);
    XR_LOGI("DDDVR/OpenXR", "swapchain destroy reason=loop exit");
    swapchain_.destroy();
    XR_LOGI("DDDVR/OpenXRCheck", "SUMMARY loader=%d instance=%d system=%d gl=%d session=%d referenceSpace=%d swapchain=%d fbo=%d frameLoop=%d", 1,1,1,1,1,1,1, fboOkSeen_ ? 1 : 0, firstFrameSubmitted_ ? 1 : 0);
    XR_LOGI("DDDVR/OpenXR", "normal shutdown");
    XR_LOGI("DDDVR/OpenXR", "OpenXrSession destroy reason=loop exit");
    session_.shutdown();
}

void OpenXrApp::stopAndJoinThread(const char* reason){ XR_LOGI("DDDVR/OpenXR", "stopAndJoinThread reason=%s", reason); running_=false; if(thread_.joinable()) thread_.join(); sessionRunning_=false; }
void OpenXrApp::pause(){ XR_LOGI("DDDVR/OpenXR", "OpenXrApp::pause requested nonFatal=1"); androidPaused_ = true; }
void OpenXrApp::resume(){ XR_LOGI("DDDVR/OpenXR", "OpenXrApp::resume requested"); androidPaused_ = false; }
void OpenXrApp::destroy(){ XR_LOGI("DDDVR/OpenXR", "OpenXrApp::destroy requested"); stopAndJoinThread("destroy"); }
