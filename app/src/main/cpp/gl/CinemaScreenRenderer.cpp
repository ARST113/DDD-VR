#include "CinemaScreenRenderer.h"
#include "../util/XrLog.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace {
constexpr int kScreenSegments = 48;
constexpr float kUiPlaneOffsetMeters = 0.12f;
constexpr float kUiPanelWidthScale = 1.28f;
constexpr float kUiPanelYOffsetMeters = -0.36f;
constexpr float kUiPanelHeightMeters = 0.32f;
constexpr float kUiProgressYOffsetMeters = -0.105f;
constexpr float kUiProgressWidthScale = 0.74f;
constexpr float kUiPlayButtonWidthMeters = 0.34f;
constexpr float kUiPlayButtonHeightMeters = 0.18f;
constexpr float kUiPlayButtonYOffsetMeters = 0.055f;

constexpr float kFourXvrHdrBrightness = 0.5f;
constexpr float kFourXvrHdrSmoothBase = 0.3996094f;

struct FourXvrHdrProfile {
    float saturationPower;
    float gammaPower;
    float smoothAmount;
    float brightness;
};

FourXvrHdrProfile makeFourXvrHdrProfile(bool dolbyVision) {
    const float saturationBase = dolbyVision ? 0.53f : 0.65f;
    return {
        saturationBase + kFourXvrHdrBrightness * 0.4f,
        0.42f + kFourXvrHdrBrightness * -0.14f,
        kFourXvrHdrSmoothBase + std::max(kFourXvrHdrBrightness - 0.5f, 0.f) * 0.8f,
        kFourXvrHdrBrightness
    };
}

constexpr GLfloat kIdentityColorMatrix[16] = {
    1.f, 0.f, 0.f, 0.f,
    0.f, 1.f, 0.f, 0.f,
    0.f, 0.f, 1.f, 0.f,
    0.f, 0.f, 0.f, 1.f
};
}

static GLuint compileShader(GLenum t, const char* src){
    GLuint sh=glCreateShader(t); glShaderSource(sh,1,&src,nullptr); glCompileShader(sh);
    GLint ok=0; glGetShaderiv(sh,GL_COMPILE_STATUS,&ok); if(!ok){char log[512]; glGetShaderInfoLog(sh,512,nullptr,log); XR_LOGE("DDDVR/OpenXRRenderer","shader compile error: %s",log);} return sh;
}

bool CinemaScreenRenderer::initialize(float screenWidthMeters, float screenDistanceMeters, float curveRadians){
    const char* vs =
        "#version 300 es\n"
        "layout(location=0) in vec3 aPos;"
        "layout(location=1) in vec2 aTexCoord;"
        "uniform mat4 uMvp;"
        "out vec2 vTexCoord;"
        "void main(){"
        "  gl_Position=uMvp*vec4(aPos,1.0);"
        "  vTexCoord=aTexCoord;"
        "}";
    const char* fs =
        "#version 300 es\n"
        "#extension GL_OES_EGL_image_external_essl3 : require\n"
        "precision highp float;"
        "in vec2 vTexCoord;"
        "uniform samplerExternalOES uTexture;"
        "uniform mat4 uTexMatrix;"
        "uniform vec4 uUvRect;"
        "uniform bool uHasVideo;"
        "uniform vec3 uFallbackColor;"
        "uniform vec4 uColorParams;"
        "uniform int uTransfer;"
        "uniform int uDolbyProfile;"
        "uniform bool uDolbyMappingEnabled;"
        "uniform ivec3 uDolbyMappingKind;"
        "uniform sampler2D uDolbyI2D;"
        "uniform sampler2D uDolbyCt2D;"
        "uniform sampler2D uDolbyCp2D;"
        "uniform sampler3D uDolbyI3D;"
        "uniform sampler3D uDolbyCt3D;"
        "uniform sampler3D uDolbyCp3D;"
        "uniform bool uDolbyColorEnabled;"
        "uniform mat3 uDolbyYccToRgb;"
        "uniform vec3 uDolbyYccOffset;"
        "uniform mat3 uDolbyColorMatrix;"
        "uniform bool uDolbyBlFullRange;"
        "uniform vec4 uHdrPowerValue;"
        "uniform mat4 uHdrColorMat;"
        "out vec4 fragColor;"
        "const float ST_M1=1.0/0.1593017578125;"
        "const float ST_M2=1.0/78.84375;"
        "const float ST_C1=0.8359375;"
        "const float ST_C2=18.8515625;"
        "const float ST_C3=18.6875;"
        "float hable(float x){"
        " const float A=0.15; const float B=0.50; const float C=0.10; const float D=0.20; const float E=0.02; const float F=0.30;"
        " return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;"
        "}"
        "vec3 st2084(vec3 x){"
        " x=clamp(x,vec3(0.0),vec3(1.2));"
        " vec3 xp=pow(x,vec3(ST_M2));"
        " return pow(max(xp-vec3(ST_C1),vec3(0.0))/(vec3(ST_C2)-vec3(ST_C3)*xp),vec3(ST_M1));"
        "}"
        "vec3 v4Rgb2Sat(vec3 c){"
        "  float fmax=max(c.r,max(c.g,c.b));"
        "  vec3 delta=max(c-vec3(fmax),vec3(-0.1));"
        "  return max(c+delta*vec3(uHdrPowerValue.x),vec3(0.0));"
        "}"
        "vec4 v4SmoothColor(vec4 inColor){"
        "  float t3=-2.0*uHdrPowerValue.z;"
        "  float t2=3.0*uHdrPowerValue.z;"
        "  float t1=1.0-uHdrPowerValue.z;"
        "  vec4 d2=inColor*inColor;"
        "  vec4 d3=d2*inColor;"
        "  return d3*t3+d2*t2+inColor*t1;"
        "}"
        "vec3 v4MapHdrPq(vec3 pq){"
        "  vec3 linear=st2084(pq);"
        "  float lum=max(max(linear.r,linear.g),linear.b);"
        "  float scale=hable(lum)/max(lum,1e-6);"
        "  vec3 tm=linear*scale*vec3(100.0,93.0,100.0)*0.70;"
        "  vec4 rv=pow(vec4(tm,1.0),vec4(uHdrPowerValue.y,uHdrPowerValue.y,uHdrPowerValue.y,1.0));"
        "  vec4 inColor=clamp(vec4(v4Rgb2Sat(rv.rgb),1.0)*uHdrColorMat,vec4(0.0),vec4(1.0));"
        "  return v4SmoothColor(inColor).rgb;"
        "}"
        "vec3 v4MapDolbyPq(vec3 pq){"
        "  vec3 linear=st2084(pq);"
        "  if(uDolbyColorEnabled) linear=linear*uDolbyColorMatrix;"
        "  linear=clamp(linear,vec3(0.0),vec3(1.0));"
        "  float lum=max(max(linear.r,linear.g),linear.b);"
        "  float scale=hable(lum)/max(lum,1e-6);"
        "  vec3 tm=linear*scale*vec3(100.0,93.0,100.0)*0.70;"
        "  vec4 rv=pow(vec4(tm,1.0),vec4(uHdrPowerValue.y,uHdrPowerValue.y,uHdrPowerValue.y,1.0));"
        "  vec4 inColor=clamp(vec4(v4Rgb2Sat(rv.rgb),1.0)*uHdrColorMat,vec4(0.0),vec4(1.0));"
        "  return v4SmoothColor(inColor).rgb;"
        "}"
        "vec3 v4RgbToBt2020Ycc(vec3 rgb){"
        " float y=dot(rgb,vec3(0.2627,0.6780,0.0593));"
        " return vec3(y,(rgb.b-y)/1.8814+0.5,(rgb.r-y)/1.4746+0.5);"
        "}"
        "vec3 v4Bt2020YccToRgb(vec3 ycc){"
        " float u=ycc.g-0.5; float v=ycc.b-0.5; float y=ycc.r;"
        " return vec3(y+1.4746*v,y-0.16455*u-0.57135*v,y+1.8814*u);"
        "}"
        "vec3 v4RgbToDolbyCode(vec3 rgb){"
        " vec3 ycc=v4RgbToBt2020Ycc(rgb);"
        " if(uDolbyBlFullRange) return ycc;"
        " return vec3(ycc.r*(219.0/255.0)+(16.0/255.0),(ycc.g-0.5)*(224.0/255.0)+(128.0/255.0),(ycc.b-0.5)*(224.0/255.0)+(128.0/255.0));"
        "}"
        "vec3 v4DolbyCodeToRgb(vec3 ycc){"
        " if(uDolbyColorEnabled) return (ycc-uDolbyYccOffset)*uDolbyYccToRgb;"
        " if(!uDolbyBlFullRange){"
        "  ycc=vec3((ycc.r-(64.0/1023.0))/(876.0/1023.0),(ycc.g-0.5)/(896.0/1023.0)+0.5,(ycc.b-0.5)/(896.0/1023.0)+0.5);"
        " }"
        " return v4Bt2020YccToRgb(ycc);"
        "}"
        "vec3 v4ApplyDolbyMapping(vec3 rgb){"
        " if(!uDolbyMappingEnabled) return rgb;"
        " vec3 ycc=clamp(v4RgbToDolbyCode(rgb),0.0,1.0);"
        " vec3 mapped;"
        " mapped.r=(uDolbyMappingKind.x==2)?texture(uDolbyI3D,ycc).r:texture(uDolbyI2D,vec2(ycc.r,0.5)).r;"
        " mapped.g=(uDolbyMappingKind.y==2)?texture(uDolbyCt3D,ycc).r:texture(uDolbyCt2D,vec2(ycc.g,0.5)).r;"
        " mapped.b=(uDolbyMappingKind.z==2)?texture(uDolbyCp3D,ycc).r:texture(uDolbyCp2D,vec2(ycc.b,0.5)).r;"
        " return v4DolbyCodeToRgb(mapped);"
        "}"
        "vec3 v4HlgSat(vec3 c){"
        "  bool strong=(uDolbyProfile==8);"
        "  float fix=strong?3.0:1.11;"
        "  vec3 result=c*vec3(1.036*fix,1.000*fix,1.036*fix);"
        "  float fmax=max(result.r,max(result.g,result.b));"
        "  float recovery=strong?0.6:0.3;"
        "  result+=max(result-vec3(fmax),vec3(-0.1))*vec3(recovery);"
        "  return max(result,vec3(0.0));"
        "}"
        "vec3 v4MapHlg(vec3 rgb){"
        "  vec3 c=clamp(rgb,0.0,1.0);"
        "  vec3 lo=c*c/3.0;"
        "  vec3 hi=(vec3(0.28466892)+exp((c-vec3(0.55991073))/vec3(0.17883277)))/12.0;"
        "  vec3 linear=mix(lo,hi,step(vec3(0.5),c));"
        "  float hlgGamma=(uDolbyProfile==8)?0.85:0.45;"
        "  vec4 rv=pow(vec4(linear,1.0),vec4(hlgGamma,hlgGamma,hlgGamma,1.0));"
        "  vec4 inColor=clamp(vec4(v4HlgSat(rv.rgb),1.0)*uHdrColorMat,vec4(0.0),vec4(1.0));"
        "  const float smoothAlpha=0.32;"
        "  vec4 d2=inColor*inColor;"
        "  vec4 d3=d2*inColor;"
        "  return (d3*(-2.0*smoothAlpha)+d2*(3.0*smoothAlpha)+inColor*(1.0-smoothAlpha)).rgb;"
        "}"
        "void main(){"
        "  if(!uHasVideo){ fragColor=vec4(uFallbackColor,1.0); return; }"
        "  vec2 local=vec2(vTexCoord.x, 1.0-vTexCoord.y);"
        "  vec2 mapped=vec2(local.x*uUvRect.z+uUvRect.x, local.y*uUvRect.w+uUvRect.y);"
        "  vec2 uv=(uTexMatrix*vec4(mapped,0.0,1.0)).xy;"
        "  vec4 sampleColor=texture(uTexture,uv);"
        "  vec3 color=v4ApplyDolbyMapping(sampleColor.rgb);"
        "  if(uTransfer==2){"
        "    color=(uDolbyProfile>0 && uDolbyColorEnabled)?v4MapDolbyPq(color):v4MapHdrPq(color);"
        "  } else if(uTransfer==3){"
        "    color=v4MapHlg(color);"
        "  } else {"
        "    color=max(color*uColorParams.x,vec3(0.0));"
        "    color=(color-vec3(0.5))*uColorParams.y+vec3(0.5);"
        "    float luma=dot(color,vec3(0.2126,0.7152,0.0722));"
        "    color=mix(vec3(luma),color,uColorParams.z);"
        "    color=pow(max(color,vec3(0.0)),vec3(uColorParams.w));"
        "  }"
        "  fragColor=vec4(clamp(color,0.0,1.0), sampleColor.a);"
        "}";
    GLuint v=compileShader(GL_VERTEX_SHADER,vs), f=compileShader(GL_FRAGMENT_SHADER,fs);
    program_ = glCreateProgram(); glAttachShader(program_,v); glAttachShader(program_,f); glLinkProgram(program_);
    glDeleteShader(v); glDeleteShader(f);
    GLint linked=0; glGetProgramiv(program_,GL_LINK_STATUS,&linked); if(!linked){char log[512]; glGetProgramInfoLog(program_,512,nullptr,log); XR_LOGE("DDDVR/OpenXRRenderer","program link error: %s",log); return false;}
    mvpLoc_=glGetUniformLocation(program_,"uMvp");
    texMatrixLoc_=glGetUniformLocation(program_,"uTexMatrix");
    textureLoc_=glGetUniformLocation(program_,"uTexture");
    uvRectLoc_=glGetUniformLocation(program_,"uUvRect");
    hasVideoLoc_=glGetUniformLocation(program_,"uHasVideo");
    fallbackColorLoc_=glGetUniformLocation(program_,"uFallbackColor");
    colorParamsLoc_=glGetUniformLocation(program_,"uColorParams");
    hdrTransferLoc_=glGetUniformLocation(program_,"uTransfer");
    dolbyProfileLoc_=glGetUniformLocation(program_,"uDolbyProfile");
    hdrPowerValueLoc_=glGetUniformLocation(program_,"uHdrPowerValue");
    hdrColorMatrixLoc_=glGetUniformLocation(program_,"uHdrColorMat");
    dolbyMappingEnabledLoc_=glGetUniformLocation(program_,"uDolbyMappingEnabled");
    dolbyMappingKindLoc_=glGetUniformLocation(program_,"uDolbyMappingKind");
    dolbySampler2DLoc_[0]=glGetUniformLocation(program_,"uDolbyI2D");
    dolbySampler2DLoc_[1]=glGetUniformLocation(program_,"uDolbyCt2D");
    dolbySampler2DLoc_[2]=glGetUniformLocation(program_,"uDolbyCp2D");
    dolbySampler3DLoc_[0]=glGetUniformLocation(program_,"uDolbyI3D");
    dolbySampler3DLoc_[1]=glGetUniformLocation(program_,"uDolbyCt3D");
    dolbySampler3DLoc_[2]=glGetUniformLocation(program_,"uDolbyCp3D");
    dolbyColorEnabledLoc_=glGetUniformLocation(program_,"uDolbyColorEnabled");
    dolbyYccToRgbLoc_=glGetUniformLocation(program_,"uDolbyYccToRgb");
    dolbyYccOffsetLoc_=glGetUniformLocation(program_,"uDolbyYccOffset");
    dolbyColorMatrixLoc_=glGetUniformLocation(program_,"uDolbyColorMatrix");
    dolbyBlFullRangeLoc_=glGetUniformLocation(program_,"uDolbyBlFullRange");

    const char* ffmpegFs =
        "#version 300 es\n"
        "precision highp float;"
        "precision highp int;"
        "precision highp usampler2D;"
        "in vec2 vTexCoord;"
        "uniform usampler2D uPlane0;"
        "uniform usampler2D uPlane1;"
        "uniform usampler2D uPlane2;"
        "uniform vec4 uUvRect;"
        "uniform bool uHasVideo;"
        "uniform vec3 uFallbackColor;"
        "uniform vec4 uColorParams;"
        "uniform int uPixelFormat;"
        "uniform int uTransfer;"
        "uniform int uPrimaries;"
        "uniform int uRange;"
        "uniform int uDolbyVision;"
        "uniform int uDolbyProfile;"
        "uniform bool uDolbyMappingEnabled;"
        "uniform ivec3 uDolbyMappingKind;"
        "uniform sampler2D uDolbyI2D;"
        "uniform sampler2D uDolbyCt2D;"
        "uniform sampler2D uDolbyCp2D;"
        "uniform sampler3D uDolbyI3D;"
        "uniform sampler3D uDolbyCt3D;"
        "uniform sampler3D uDolbyCp3D;"
        "uniform bool uDolbyColorEnabled;"
        "uniform mat3 uDolbyYccToRgb;"
        "uniform vec3 uDolbyYccOffset;"
        "uniform mat3 uDolbyColorMatrix;"
        "uniform bool uDolbyBlFullRange;"
        "uniform vec4 uHdrPowerValue;"
        "uniform mat4 uHdrColorMat;"
        "out vec4 fragColor;"
        "const float ST_M1=1.0/0.1593017578125;"
        "const float ST_M2=1.0/78.84375;"
        "const float ST_C1=0.8359375;"
        "const float ST_C2=18.8515625;"
        "const float ST_C3=18.6875;"
        "float hable(float x){"
        " const float A=0.15; const float B=0.50; const float C=0.10; const float D=0.20; const float E=0.02; const float F=0.30;"
        " return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;"
        "}"
        "vec3 st2084(vec3 x){"
        " x=clamp(x,vec3(0.0),vec3(1.2));"
        " vec3 xp=pow(x,vec3(ST_M2));"
        " return pow(max(xp-vec3(ST_C1),vec3(0.0))/(vec3(ST_C2)-vec3(ST_C3)*xp),vec3(ST_M1));"
        "}"
        "vec3 rgb2sat(vec3 c){"
        " float m=max(c.r,max(c.g,c.b));"
        " vec3 d=max(c-vec3(m),vec3(-0.1));"
        " return max(c+d*vec3(uHdrPowerValue.x),vec3(0.0));"
        "}"
        "vec4 smoothColor(vec4 c){"
        " vec4 d2=c*c; vec4 d3=d2*c;"
        " return d3*(-2.0*uHdrPowerValue.z)+d2*(3.0*uHdrPowerValue.z)+c*(1.0-uHdrPowerValue.z);"
        "}"
        "vec3 toneMap(vec3 linear){"
        " float lum=max(max(linear.r,linear.g),linear.b);"
        " float scale=hable(lum)/max(lum,1e-6);"
        " return linear*scale*vec3(100.0,93.0,100.0)*0.70;"
        "}"
        "vec3 mapHdrPq(vec3 rgb){"
        " vec3 linear=st2084(rgb);"
        " vec3 tm=toneMap(linear);"
        " vec4 rv=pow(vec4(tm,1.0),vec4(uHdrPowerValue.y,uHdrPowerValue.y,uHdrPowerValue.y,1.0));"
        " vec4 inColor=clamp(vec4(rgb2sat(rv.rgb),1.0)*uHdrColorMat,vec4(0.0),vec4(1.0));"
        " return smoothColor(inColor).rgb;"
        "}"
        "vec3 mapDolbyPq(vec3 rgb){"
        " vec3 linear=st2084(rgb);"
        " if(uDolbyColorEnabled) linear=linear*uDolbyColorMatrix;"
        " linear=clamp(linear,vec3(0.0),vec3(1.0));"
        " vec3 tm=toneMap(linear);"
        " vec4 rv=pow(vec4(tm,1.0),vec4(uHdrPowerValue.y,uHdrPowerValue.y,uHdrPowerValue.y,1.0));"
        " vec4 inColor=clamp(vec4(rgb2sat(rv.rgb),1.0)*uHdrColorMat,vec4(0.0),vec4(1.0));"
        " return smoothColor(inColor).rgb;"
        "}"
        "vec3 applyDolbyMapping(vec3 ycc){"
        " if(!uDolbyMappingEnabled) return ycc;"
        " ycc=clamp(ycc,0.0,1.0);"
        " vec3 mapped;"
        " mapped.r=(uDolbyMappingKind.x==2)?texture(uDolbyI3D,ycc).r:texture(uDolbyI2D,vec2(ycc.r,0.5)).r;"
        " mapped.g=(uDolbyMappingKind.y==2)?texture(uDolbyCt3D,ycc).r:texture(uDolbyCt2D,vec2(ycc.g,0.5)).r;"
        " mapped.b=(uDolbyMappingKind.z==2)?texture(uDolbyCp3D,ycc).r:texture(uDolbyCp2D,vec2(ycc.b,0.5)).r;"
        " return mapped;"
        "}"
        "vec3 dolbyYccToRgb(vec3 ycc){"
        " if(uDolbyColorEnabled) return (ycc-uDolbyYccOffset)*uDolbyYccToRgb;"
        " vec3 code=ycc;"
        " if(!uDolbyBlFullRange){"
        "  code=vec3((code.r-(64.0/1023.0))/(876.0/1023.0),(code.g-0.5)/(896.0/1023.0)+0.5,(code.b-0.5)/(896.0/1023.0)+0.5);"
        " }"
        " float u=code.g-0.5; float v=code.b-0.5; float y=code.r;"
        " return vec3(y+1.4746*v,y-0.16455*u-0.57135*v,y+1.8814*u);"
        "}"
        "vec3 hlgSat(vec3 c){"
        " bool strong=(uDolbyProfile==8);"
        " float fix=strong?3.0:1.11;"
        " vec3 result=c*vec3(1.036*fix,1.000*fix,1.036*fix);"
        " float fmax=max(result.r,max(result.g,result.b));"
        " float recovery=strong?0.6:0.3;"
        " result+=max(result-vec3(fmax),vec3(-0.1))*vec3(recovery);"
        " return max(result,vec3(0.0));"
        "}"
        "vec3 mapHlg(vec3 rgb){"
        " vec3 c=clamp(rgb,0.0,1.0);"
        " vec3 lo=c*c/3.0;"
        " vec3 hi=(vec3(0.28466892)+exp((c-vec3(0.55991073))/vec3(0.17883277)))/12.0;"
        " vec3 linear=mix(lo,hi,step(vec3(0.5),c));"
        " float hlgGamma=(uDolbyProfile==8)?0.85:0.45;"
        " vec4 rv=pow(vec4(linear,1.0),vec4(hlgGamma,hlgGamma,hlgGamma,1.0));"
        " vec4 inColor=clamp(vec4(hlgSat(rv.rgb),1.0)*uHdrColorMat,vec4(0.0),vec4(1.0));"
        " const float smoothAlpha=0.32;"
        " vec4 d2=inColor*inColor; vec4 d3=d2*inColor;"
        " return (d3*(-2.0*smoothAlpha)+d2*(3.0*smoothAlpha)+inColor*(1.0-smoothAlpha)).rgb;"
        "}"
        "vec3 applyControls(vec3 color){"
        " color=max(color*uColorParams.x,vec3(0.0));"
        " color=(color-vec3(0.5))*uColorParams.y+vec3(0.5);"
        " float luma=dot(color,vec3(0.2126,0.7152,0.0722));"
        " color=mix(vec3(luma),color,uColorParams.z);"
        " return pow(max(color,vec3(0.0)),vec3(uColorParams.w));"
        "}"
        "float yNorm(uint raw){"
        " if(uPixelFormat==2) return float(raw)/255.0;"
        " if(uPixelFormat==3) return float(raw)/65535.0;"
        " return float(raw)/1023.0;"
        "}"
        "vec3 readYuvCode(vec2 uv){"
        " float y=yNorm(texture(uPlane0,uv).r);"
        " float u=0.5; float v=0.5;"
        " if(uPixelFormat==3){"
        "   uvec2 uvp=texture(uPlane1,uv).rg;"
        "   u=float(uvp.r)/65535.0;"
        "   v=float(uvp.g)/65535.0;"
        " } else {"
        "   u=yNorm(texture(uPlane1,uv).r);"
        "   v=yNorm(texture(uPlane2,uv).r);"
        " }"
        " return vec3(y,u,v);"
        "}"
        "vec3 standardYuvToRgb(vec3 code){"
        " float y=code.r; float u=code.g; float v=code.b;"
        " bool fullRange=(uRange==2);"
        " if(fullRange){"
        "   u-=0.5; v-=0.5;"
        " } else if(uPixelFormat==2){"
        "   y=clamp((y-(16.0/255.0))/(219.0/255.0),0.0,1.0);"
        "   u=(u-(128.0/255.0))/(224.0/255.0);"
        "   v=(v-(128.0/255.0))/(224.0/255.0);"
        " } else {"
        "   y=clamp((y-(64.0/1023.0))/(876.0/1023.0),0.0,1.0);"
        "   u=(u-(512.0/1023.0))/(896.0/1023.0);"
        "   v=(v-(512.0/1023.0))/(896.0/1023.0);"
        " }"
        " if(uPrimaries==2){"
        "   return vec3(y+1.4746*v, y-0.16455*u-0.57135*v, y+1.8814*u);"
        " }"
        " return vec3(y+1.5748*v, y-0.1873*u-0.4681*v, y+1.8556*u);"
        "}"
        "void main(){"
        " if(!uHasVideo){ fragColor=vec4(uFallbackColor,1.0); return; }"
        " vec2 local=vec2(vTexCoord.x,vTexCoord.y);"
        " vec2 uv=vec2(local.x*uUvRect.z+uUvRect.x, local.y*uUvRect.w+uUvRect.y);"
        " vec3 ycc=readYuvCode(uv);"
        " vec3 rgb;"
        " if(uDolbyMappingEnabled){ rgb=dolbyYccToRgb(applyDolbyMapping(ycc)); }"
        " else { rgb=standardYuvToRgb(ycc); }"
        " if(uTransfer==2 || uDolbyVision==1){ rgb=(uDolbyVision==1 && uDolbyColorEnabled)?mapDolbyPq(rgb):mapHdrPq(rgb); }"
        " else if(uTransfer==3){ rgb=mapHlg(rgb); }"
        " else { rgb=applyControls(rgb); }"
        " fragColor=vec4(clamp(rgb,0.0,1.0),1.0);"
        "}";
    GLuint fv=compileShader(GL_VERTEX_SHADER,vs), ff=compileShader(GL_FRAGMENT_SHADER,ffmpegFs);
    ffmpegProgram_ = glCreateProgram();
    glAttachShader(ffmpegProgram_,fv);
    glAttachShader(ffmpegProgram_,ff);
    glLinkProgram(ffmpegProgram_);
    glDeleteShader(fv);
    glDeleteShader(ff);
    GLint ffmpegLinked=0;
    glGetProgramiv(ffmpegProgram_,GL_LINK_STATUS,&ffmpegLinked);
    if(!ffmpegLinked){
        char log[512];
        glGetProgramInfoLog(ffmpegProgram_,512,nullptr,log);
        XR_LOGE("DDDVR/OpenXRRenderer","ffmpeg program link error: %s",log);
        return false;
    }
    ffmpegMvpLoc_=glGetUniformLocation(ffmpegProgram_,"uMvp");
    ffmpegUvRectLoc_=glGetUniformLocation(ffmpegProgram_,"uUvRect");
    ffmpegHasVideoLoc_=glGetUniformLocation(ffmpegProgram_,"uHasVideo");
    ffmpegFallbackColorLoc_=glGetUniformLocation(ffmpegProgram_,"uFallbackColor");
    ffmpegColorParamsLoc_=glGetUniformLocation(ffmpegProgram_,"uColorParams");
    ffmpegPlane0Loc_=glGetUniformLocation(ffmpegProgram_,"uPlane0");
    ffmpegPlane1Loc_=glGetUniformLocation(ffmpegProgram_,"uPlane1");
    ffmpegPlane2Loc_=glGetUniformLocation(ffmpegProgram_,"uPlane2");
    ffmpegFormatLoc_=glGetUniformLocation(ffmpegProgram_,"uPixelFormat");
    ffmpegTransferLoc_=glGetUniformLocation(ffmpegProgram_,"uTransfer");
    ffmpegPrimariesLoc_=glGetUniformLocation(ffmpegProgram_,"uPrimaries");
    ffmpegRangeLoc_=glGetUniformLocation(ffmpegProgram_,"uRange");
    ffmpegDolbyLoc_=glGetUniformLocation(ffmpegProgram_,"uDolbyVision");
    ffmpegDolbyProfileLoc_=glGetUniformLocation(ffmpegProgram_,"uDolbyProfile");
    ffmpegHdrPowerValueLoc_=glGetUniformLocation(ffmpegProgram_,"uHdrPowerValue");
    ffmpegHdrColorMatrixLoc_=glGetUniformLocation(ffmpegProgram_,"uHdrColorMat");
    ffmpegDolbyMappingEnabledLoc_=glGetUniformLocation(ffmpegProgram_,"uDolbyMappingEnabled");
    ffmpegDolbyMappingKindLoc_=glGetUniformLocation(ffmpegProgram_,"uDolbyMappingKind");
    ffmpegDolbySampler2DLoc_[0]=glGetUniformLocation(ffmpegProgram_,"uDolbyI2D");
    ffmpegDolbySampler2DLoc_[1]=glGetUniformLocation(ffmpegProgram_,"uDolbyCt2D");
    ffmpegDolbySampler2DLoc_[2]=glGetUniformLocation(ffmpegProgram_,"uDolbyCp2D");
    ffmpegDolbySampler3DLoc_[0]=glGetUniformLocation(ffmpegProgram_,"uDolbyI3D");
    ffmpegDolbySampler3DLoc_[1]=glGetUniformLocation(ffmpegProgram_,"uDolbyCt3D");
    ffmpegDolbySampler3DLoc_[2]=glGetUniformLocation(ffmpegProgram_,"uDolbyCp3D");
    ffmpegDolbyColorEnabledLoc_=glGetUniformLocation(ffmpegProgram_,"uDolbyColorEnabled");
    ffmpegDolbyYccToRgbLoc_=glGetUniformLocation(ffmpegProgram_,"uDolbyYccToRgb");
    ffmpegDolbyYccOffsetLoc_=glGetUniformLocation(ffmpegProgram_,"uDolbyYccOffset");
    ffmpegDolbyColorMatrixLoc_=glGetUniformLocation(ffmpegProgram_,"uDolbyColorMatrix");
    ffmpegDolbyBlFullRangeLoc_=glGetUniformLocation(ffmpegProgram_,"uDolbyBlFullRange");

    halfWidthMeters_ = screenWidthMeters * 0.5f;
    halfHeightMeters_ = screenWidthMeters * (9.f / 16.f) * 0.5f;
    centerX_ = 0.f;
    centerY_ = 0.f;
    centerZ_ = -screenDistanceMeters;
    curveRadians_ = curveRadians;
    glGenBuffers(1,&videoVbo_);
    rebuildVideoMesh();
    glGenBuffers(1,&overlayVbo_);
    return true;
}

void CinemaScreenRenderer::setPlacement(float yawRadians, float centerX, float centerY, float centerZ, float curveRadians) {
    yawRadians_ = yawRadians;
    centerX_ = centerX;
    centerY_ = centerY;
    centerZ_ = centerZ;
    curveRadians_ = curveRadians;
    rebuildVideoMesh();
}

void CinemaScreenRenderer::setColorControls(float brightness, float contrast, float saturation, float gamma) {
    brightness_ = std::clamp(brightness, 0.45f, 1.45f);
    contrast_ = std::clamp(contrast, 0.75f, 1.45f);
    saturation_ = std::clamp(saturation, 0.0f, 1.65f);
    gamma_ = std::clamp(gamma, 0.65f, 1.35f);
}

void CinemaScreenRenderer::setHdrLookEnabled(bool enabled) {
    if (hdrLookEnabled_ != enabled) {
        XR_LOGI("DDDVR/OpenXRColor", "XR_HDR_LOOK enabled=%d", enabled ? 1 : 0);
    }
    hdrLookEnabled_ = enabled;
}

void CinemaScreenRenderer::rebuildVideoMesh() {
    std::vector<GLfloat> vertices;
    vertices.reserve(kScreenSegments * 6 * 5);
    const float width = halfWidthMeters_ * 2.f;
    for (int segment = 0; segment < kScreenSegments; ++segment) {
        const float t0 = static_cast<float>(segment) / static_cast<float>(kScreenSegments);
        const float t1 = static_cast<float>(segment + 1) / static_cast<float>(kScreenSegments);
        const float x0 = -halfWidthMeters_ + width * t0;
        const float x1 = -halfWidthMeters_ + width * t1;
        appendVertex(vertices, x0, -halfHeightMeters_, t0, 1.f);
        appendVertex(vertices, x1, -halfHeightMeters_, t1, 1.f);
        appendVertex(vertices, x0,  halfHeightMeters_, t0, 0.f);
        appendVertex(vertices, x1, -halfHeightMeters_, t1, 1.f);
        appendVertex(vertices, x1,  halfHeightMeters_, t1, 0.f);
        appendVertex(vertices, x0,  halfHeightMeters_, t0, 0.f);
    }
    videoVertexCount_ = static_cast<GLsizei>(vertices.size() / 5);
    glBindBuffer(GL_ARRAY_BUFFER, videoVbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GLfloat), vertices.data(), GL_STATIC_DRAW);
}

void CinemaScreenRenderer::appendVertex(
    std::vector<GLfloat>& vertices,
    float localX,
    float localY,
    float u,
    float v
) const {
    float x = localX;
    float z = 0.f;
    const float curve = curveRadians_ < 0.f ? 0.f : curveRadians_;
    if (curve > 0.001f && halfWidthMeters_ > 0.001f) {
        const float radius = (halfWidthMeters_ * 2.f) / curve;
        const float angle = (localX / halfWidthMeters_) * (curve * 0.5f);
        x = std::sin(angle) * radius;
        z = radius * (1.f - std::cos(angle));
    }

    const float c = std::cos(yawRadians_);
    const float s = std::sin(yawRadians_);
    const float worldX = centerX_ + c * x - s * z;
    const float worldZ = centerZ_ + s * x + c * z;
    vertices.push_back(worldX);
    vertices.push_back(localY + centerY_);
    vertices.push_back(worldZ);
    vertices.push_back(u);
    vertices.push_back(v);
}

void CinemaScreenRenderer::appendFlatVertex(
    std::vector<GLfloat>& vertices,
    float localX,
    float localY,
    float localZ,
    float u,
    float v
) const {
    const float c = std::cos(yawRadians_);
    const float s = std::sin(yawRadians_);
    const float worldX = centerX_ + c * localX - s * localZ;
    const float worldZ = centerZ_ + s * localX + c * localZ;
    vertices.push_back(worldX);
    vertices.push_back(localY + centerY_);
    vertices.push_back(worldZ);
    vertices.push_back(u);
    vertices.push_back(v);
}

void CinemaScreenRenderer::renderVideo(
    GLuint videoTexture,
    const float* mvp,
    const float* texMatrix,
    const CinemaUvRect& uvRect,
    bool hasVideo,
    float fallbackR,
    float fallbackG,
    float fallbackB,
    const FfmpegVideoTextureSet* colorMetadata
){
    FfmpegVideoColorTransfer transfer = FfmpegVideoColorTransfer::Sdr;
    bool dolbyVision = false;
    int dolbyProfile = 0;
    int primaries = static_cast<int>(FfmpegVideoColorPrimaries::Unknown);
    int range = static_cast<int>(FfmpegVideoColorRange::Unknown);
    if (colorMetadata != nullptr) {
        transfer = colorMetadata->transfer;
        dolbyProfile = colorMetadata->dolbyProfile;
        dolbyVision = colorMetadata->dolbyVision || dolbyProfile > 0;
        primaries = static_cast<int>(colorMetadata->primaries);
        range = static_cast<int>(colorMetadata->range);
        if (transfer == FfmpegVideoColorTransfer::Unknown) {
            transfer = dolbyVision ? FfmpegVideoColorTransfer::St2084 : FfmpegVideoColorTransfer::Sdr;
        }
    } else if (hdrLookEnabled_) {
        transfer = FfmpegVideoColorTransfer::St2084;
    }
    const FourXvrHdrProfile hdrProfile = makeFourXvrHdrProfile(dolbyVision);
    dolbyMappingTexture_.update(
        colorMetadata != nullptr ? colorMetadata->dolbyMetadata : nullptr
    );

    glUseProgram(program_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(texMatrixLoc_, 1, GL_FALSE, texMatrix);
    glUniform4f(uvRectLoc_, uvRect.uOffset, uvRect.vOffset, uvRect.uScale, uvRect.vScale);
    glUniform1i(hasVideoLoc_, hasVideo ? 1 : 0);
    glUniform3f(fallbackColorLoc_, fallbackR, fallbackG, fallbackB);
    glUniform4f(colorParamsLoc_, brightness_, contrast_, saturation_, gamma_);
    glUniform1i(hdrTransferLoc_, static_cast<int>(transfer));
    glUniform1i(dolbyProfileLoc_, dolbyProfile);
    glUniform4f(
        hdrPowerValueLoc_,
        hdrProfile.saturationPower,
        hdrProfile.gammaPower,
        hdrProfile.smoothAmount,
        hdrProfile.brightness
    );
    glUniformMatrix4fv(hdrColorMatrixLoc_, 1, GL_FALSE, kIdentityColorMatrix);
    dolbyMappingTexture_.bind(
        dolbyMappingEnabledLoc_,
        dolbyMappingKindLoc_,
        dolbySampler2DLoc_,
        dolbySampler3DLoc_,
        dolbyColorEnabledLoc_,
        dolbyYccToRgbLoc_,
        dolbyYccOffsetLoc_,
        dolbyColorMatrixLoc_,
        dolbyBlFullRangeLoc_
    );

    const int hdrLogKey = static_cast<int>(transfer)
        | (dolbyVision ? 1 << 8 : 0)
        | (primaries << 12)
        | (range << 16)
        | (dolbyProfile << 20);
    if (externalHdrLogKey_ != hdrLogKey) {
        externalHdrLogKey_ = hdrLogKey;
        XR_LOGI(
            "DDDVR/OpenXRColor",
            "XR_HDR_PROFILE source=4xvr-1.10.2 path=external transfer=%d primaries=%d range=%d dolby=%d doviProfile=%d hdrBrightness=%.3f power=(%.7f,%.7f,%.7f,%.7f) hlgProfile=%s matrix=identity xrColorSpace=UNMANAGED",
            static_cast<int>(transfer),
            primaries,
            range,
            dolbyVision ? 1 : 0,
            dolbyProfile,
            kFourXvrHdrBrightness,
            hdrProfile.saturationPower,
            hdrProfile.gammaPower,
            hdrProfile.smoothAmount,
            hdrProfile.brightness,
            dolbyProfile == 8 ? "strong" : "soft"
        );
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, videoTexture);
    glUniform1i(textureLoc_, 0);
    glBindBuffer(GL_ARRAY_BUFFER,videoVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5 * sizeof(GLfloat),(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5 * sizeof(GLfloat),(void*)(3 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLES,0,videoVertexCount_);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glActiveTexture(GL_TEXTURE0);
}

void CinemaScreenRenderer::renderFfmpegVideo(
    const FfmpegVideoTextureSet& textures,
    const float* mvp,
    const CinemaUvRect& uvRect,
    bool hasVideo,
    float fallbackR,
    float fallbackG,
    float fallbackB
) {
    dolbyMappingTexture_.update(textures.dolbyMetadata);
    glUseProgram(ffmpegProgram_);
    glUniformMatrix4fv(ffmpegMvpLoc_, 1, GL_FALSE, mvp);
    glUniform4f(ffmpegUvRectLoc_, uvRect.uOffset, uvRect.vOffset, uvRect.uScale, uvRect.vScale);
    glUniform1i(ffmpegHasVideoLoc_, hasVideo && textures.valid ? 1 : 0);
    glUniform3f(ffmpegFallbackColorLoc_, fallbackR, fallbackG, fallbackB);
    glUniform4f(ffmpegColorParamsLoc_, brightness_, contrast_, saturation_, gamma_);
    glUniform1i(ffmpegFormatLoc_, static_cast<int>(textures.pixelFormat));
    glUniform1i(ffmpegTransferLoc_, static_cast<int>(textures.transfer));
    glUniform1i(ffmpegPrimariesLoc_, static_cast<int>(textures.primaries));
    glUniform1i(ffmpegRangeLoc_, static_cast<int>(textures.range));
    glUniform1i(ffmpegDolbyLoc_, textures.dolbyVision ? 1 : 0);
    glUniform1i(ffmpegDolbyProfileLoc_, textures.dolbyProfile);
    const FourXvrHdrProfile hdrProfile = makeFourXvrHdrProfile(textures.dolbyVision);
    glUniform4f(
        ffmpegHdrPowerValueLoc_,
        hdrProfile.saturationPower,
        hdrProfile.gammaPower,
        hdrProfile.smoothAmount,
        hdrProfile.brightness
    );
    glUniformMatrix4fv(ffmpegHdrColorMatrixLoc_, 1, GL_FALSE, kIdentityColorMatrix);
    dolbyMappingTexture_.bind(
        ffmpegDolbyMappingEnabledLoc_,
        ffmpegDolbyMappingKindLoc_,
        ffmpegDolbySampler2DLoc_,
        ffmpegDolbySampler3DLoc_,
        ffmpegDolbyColorEnabledLoc_,
        ffmpegDolbyYccToRgbLoc_,
        ffmpegDolbyYccOffsetLoc_,
        ffmpegDolbyColorMatrixLoc_,
        ffmpegDolbyBlFullRangeLoc_
    );

    const int hdrLogKey = static_cast<int>(textures.transfer)
        | (textures.dolbyVision ? 1 << 8 : 0)
        | (static_cast<int>(textures.primaries) << 12)
        | (static_cast<int>(textures.range) << 16)
        | (textures.dolbyProfile << 20);
    if (planarHdrLogKey_ != hdrLogKey) {
        planarHdrLogKey_ = hdrLogKey;
        XR_LOGI(
            "DDDVR/OpenXRColor",
            "XR_HDR_PROFILE source=4xvr-1.10.2 path=planar transfer=%d primaries=%d range=%d dolby=%d doviProfile=%d hdrBrightness=%.3f power=(%.7f,%.7f,%.7f,%.7f) hlgProfile=%s matrix=identity xrColorSpace=UNMANAGED",
            static_cast<int>(textures.transfer),
            static_cast<int>(textures.primaries),
            static_cast<int>(textures.range),
            textures.dolbyVision ? 1 : 0,
            textures.dolbyProfile,
            kFourXvrHdrBrightness,
            hdrProfile.saturationPower,
            hdrProfile.gammaPower,
            hdrProfile.smoothAmount,
            hdrProfile.brightness,
            textures.dolbyProfile == 8 ? "strong" : "soft"
        );
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures.planes[0]);
    glUniform1i(ffmpegPlane0Loc_, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures.planes[1]);
    glUniform1i(ffmpegPlane1Loc_, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, textures.planes[2]);
    glUniform1i(ffmpegPlane2Loc_, 2);

    glBindBuffer(GL_ARRAY_BUFFER,videoVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5 * sizeof(GLfloat),(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5 * sizeof(GLfloat),(void*)(3 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLES,0,videoVertexCount_);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glActiveTexture(GL_TEXTURE0);
}

void CinemaScreenRenderer::renderUiOverlay(
    const float* mvp,
    int progressPermille,
    bool playing,
    CinemaUiHoverTarget hoverTarget
){
    const float safeProgress = progressPermille <= 0 ? 0.f :
        (progressPermille >= 1000 ? 1.f : progressPermille / 1000.f);
    const float panelY = -halfHeightMeters_ + kUiPanelYOffsetMeters;
    const float panelW = halfWidthMeters_ * kUiPanelWidthScale;
    const float panelH = kUiPanelHeightMeters;
    const float progressY = panelY + kUiProgressYOffsetMeters;
    const float progressTrackW = panelW * kUiProgressWidthScale;
    const float progressH = 0.026f;
    const float playY = panelY + kUiPlayButtonYOffsetMeters;
    const bool hoverProgress = hoverTarget == CinemaUiHoverTarget::Progress;
    const bool hoverPlay = hoverTarget == CinemaUiHoverTarget::PlayPause;
    const bool hoverPanel = hoverTarget == CinemaUiHoverTarget::Panel || hoverTarget == CinemaUiHoverTarget::Video;

    if (hoverPanel || hoverProgress || hoverPlay) {
        renderSolidRect(mvp, 0.f, panelY, panelW + 0.045f, panelH + 0.045f, 0.025f, 0.050f, 0.075f, kUiPlaneOffsetMeters - 0.004f);
    }
    renderSolidRect(mvp, 0.f, panelY, panelW, panelH, 0.014f, 0.015f, 0.018f, kUiPlaneOffsetMeters);
    renderSolidRect(mvp, -panelW * 0.435f, panelY, panelW * 0.11f, panelH * 0.92f, 0.06f, 0.12f, 0.22f, kUiPlaneOffsetMeters + 0.004f);

    if (hoverPlay) {
        renderSolidRect(mvp, 0.f, playY, kUiPlayButtonWidthMeters, kUiPlayButtonHeightMeters, 0.08f, 0.20f, 0.36f, kUiPlaneOffsetMeters + 0.006f);
    }

    if (hoverProgress) {
        renderSolidRect(mvp, 0.f, progressY, progressTrackW + 0.10f, progressH + 0.055f, 0.055f, 0.15f, 0.26f, kUiPlaneOffsetMeters + 0.005f);
    }
    renderSolidRect(mvp, 0.f, progressY, progressTrackW, progressH, 0.075f, 0.080f, 0.095f, kUiPlaneOffsetMeters + 0.006f);
    if (safeProgress > 0.f) {
        const float progressW = progressTrackW * safeProgress;
        const float progressX = -progressTrackW * 0.5f + progressW * 0.5f;
        renderSolidRect(mvp, progressX, progressY, progressW, progressH, 0.30f, 0.52f, 1.0f, kUiPlaneOffsetMeters + 0.010f);
        const float knobX = -progressTrackW * 0.5f + progressTrackW * safeProgress;
        const float knobSize = hoverProgress ? 0.066f : 0.050f;
        renderSolidRect(mvp, knobX, progressY, knobSize, knobSize, 0.86f, 0.92f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
    }

    if (playing) {
        renderSolidRect(mvp, -0.025f, playY, 0.026f, 0.090f, 0.88f, 0.92f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
        renderSolidRect(mvp, 0.025f, playY, 0.026f, 0.090f, 0.88f, 0.92f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
    } else {
        renderSolidRect(mvp, -0.015f, playY, 0.030f, 0.100f, 0.36f, 0.64f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
        renderSolidRect(mvp, 0.025f, playY, 0.030f, 0.075f, 0.36f, 0.64f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
        renderSolidRect(mvp, 0.055f, playY, 0.030f, 0.050f, 0.36f, 0.64f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
    }

    renderSolidRect(mvp, -panelW * 0.23f, playY, 0.020f, 0.070f, 0.58f, 0.64f, 0.72f, kUiPlaneOffsetMeters + 0.014f);
    renderSolidRect(mvp, panelW * 0.23f, playY, 0.020f, 0.070f, 0.58f, 0.64f, 0.72f, kUiPlaneOffsetMeters + 0.014f);
    renderSolidRect(mvp, panelW * 0.39f, playY, 0.018f, 0.018f, 0.62f, 0.66f, 0.73f, kUiPlaneOffsetMeters + 0.014f);
    renderSolidRect(mvp, panelW * 0.43f, playY, 0.018f, 0.018f, 0.62f, 0.66f, 0.73f, kUiPlaneOffsetMeters + 0.014f);
    renderSolidRect(mvp, panelW * 0.47f, playY, 0.018f, 0.018f, 0.62f, 0.66f, 0.73f, kUiPlaneOffsetMeters + 0.014f);
}

void CinemaScreenRenderer::renderGrabHighlight(const float* mvp) {
    const float panelY = -halfHeightMeters_ + kUiPanelYOffsetMeters;
    const float panelW = halfWidthMeters_ * kUiPanelWidthScale;
    const float topEdgeY = panelY + kUiPanelHeightMeters * 0.5f - 0.018f;
    renderSolidRect(mvp, 0.f, topEdgeY, panelW * 0.84f, 0.018f, 0.10f, 0.46f, 1.0f, kUiPlaneOffsetMeters + 0.018f);
}

void CinemaScreenRenderer::renderSolidRect(
    const float* mvp,
    float centerX,
    float centerY,
    float width,
    float height,
    float r,
    float g,
    float b,
    float localZ
){
    const float left = centerX - width * 0.5f;
    const float right = centerX + width * 0.5f;
    const float bottom = centerY - height * 0.5f;
    const float top = centerY + height * 0.5f;
    std::vector<GLfloat> quad;
    quad.reserve(30);
    appendFlatVertex(quad, left,  bottom, localZ, 0.f, 0.f);
    appendFlatVertex(quad, right, bottom, localZ, 1.f, 0.f);
    appendFlatVertex(quad, left,  top,    localZ, 0.f, 1.f);
    appendFlatVertex(quad, right, bottom, localZ, 1.f, 0.f);
    appendFlatVertex(quad, right, top,    localZ, 1.f, 1.f);
    appendFlatVertex(quad, left,  top,    localZ, 0.f, 1.f);
    static const float identity[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    glUseProgram(program_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(texMatrixLoc_, 1, GL_FALSE, identity);
    glUniform4f(uvRectLoc_, 0.f, 0.f, 1.f, 1.f);
    glUniform1i(hasVideoLoc_, 0);
    glUniform3f(fallbackColorLoc_, r, g, b);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
    glBufferData(GL_ARRAY_BUFFER, quad.size() * sizeof(GLfloat), quad.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5 * sizeof(GLfloat),(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5 * sizeof(GLfloat),(void*)(3 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLES,0,6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

void CinemaScreenRenderer::renderRay(
    const float* mvp,
    const float start[3],
    const float end[3],
    float r,
    float g,
    float b
) {
    const GLfloat line[] = {
        start[0], start[1], start[2], 0.f, 0.f,
        end[0], end[1], end[2], 1.f, 1.f,
    };
    static const float identity[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    glUseProgram(program_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(texMatrixLoc_, 1, GL_FALSE, identity);
    glUniform4f(uvRectLoc_, 0.f, 0.f, 1.f, 1.f);
    glUniform1i(hasVideoLoc_, 0);
    glUniform3f(fallbackColorLoc_, r, g, b);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(line), line, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
    glLineWidth(3.f);
    glDrawArrays(GL_LINES, 0, 2);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

void CinemaScreenRenderer::renderCursorDot(
    const float* mvp,
    const float center[3],
    const float right[3],
    const float up[3],
    float radiusMeters,
    bool active
) {
    auto drawQuad = [&](float radius, float r, float g, float b) {
        const float lx[3] = {
            right[0] * radius,
            right[1] * radius,
            right[2] * radius
        };
        const float ly[3] = {
            up[0] * radius,
            up[1] * radius,
            up[2] * radius
        };
        const GLfloat quad[] = {
            center[0] - lx[0] - ly[0], center[1] - lx[1] - ly[1], center[2] - lx[2] - ly[2], 0.f, 0.f,
            center[0] + lx[0] - ly[0], center[1] + lx[1] - ly[1], center[2] + lx[2] - ly[2], 1.f, 0.f,
            center[0] - lx[0] + ly[0], center[1] - lx[1] + ly[1], center[2] - lx[2] + ly[2], 0.f, 1.f,
            center[0] + lx[0] - ly[0], center[1] + lx[1] - ly[1], center[2] + lx[2] - ly[2], 1.f, 0.f,
            center[0] + lx[0] + ly[0], center[1] + lx[1] + ly[1], center[2] + lx[2] + ly[2], 1.f, 1.f,
            center[0] - lx[0] + ly[0], center[1] - lx[1] + ly[1], center[2] - lx[2] + ly[2], 0.f, 1.f,
        };
        static const float identity[16] = {
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f
        };
        glUseProgram(program_);
        glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, mvp);
        glUniformMatrix4fv(texMatrixLoc_, 1, GL_FALSE, identity);
        glUniform4f(uvRectLoc_, 0.f, 0.f, 1.f, 1.f);
        glUniform1i(hasVideoLoc_, 0);
        glUniform3f(fallbackColorLoc_, r, g, b);
        glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
    };

    const float outer = active ? radiusMeters * 1.55f : radiusMeters * 1.25f;
    drawQuad(outer, 0.10f, 0.85f, 1.0f);
    drawQuad(radiusMeters, 0.92f, 0.97f, 1.0f);
}
