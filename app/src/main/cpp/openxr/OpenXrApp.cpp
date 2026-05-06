#include "OpenXrApp.h"
#include "../util/XrLog.h"
#include <chrono>

bool OpenXrApp::initialize(){
    if(!session_.initialize()) return false;
    if(!session_.createSession()) return false;
    if(!session_.createReferenceSpace()) return false;
    renderer_.initialize();
    swapchain_.create();
    return true;
}

bool OpenXrApp::start(){ if(!session_.begin()) return false; running_=true; thread_=std::thread(&OpenXrApp::loop,this); return true; }
void OpenXrApp::pause(){ running_=false; }
void OpenXrApp::resume(){ if(!running_){ running_=true; if(!thread_.joinable()) thread_=std::thread(&OpenXrApp::loop,this);} }
void OpenXrApp::destroy(){ running_=false; if(thread_.joinable()) thread_.join(); }
void OpenXrApp::loop(){ while(running_){ session_.pollEvents(); input_.sync(); auto i=swapchain_.acquireImage(); (void)i; renderer_.renderFrame(); swapchain_.releaseImage(); std::this_thread::sleep_for(std::chrono::milliseconds(16)); }}
