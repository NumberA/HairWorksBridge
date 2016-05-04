// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#if !PLATFORM_ANDROIDGL4 && !PLATFORM_ANDROIDES31

#include "OpenGLDrvPrivate.h"
#include "OpenGLES2.h"
#include "AndroidWindow.h"
#include "AndroidOpenGLPrivate.h"

PFNEGLGETSYSTEMTIMENVPROC eglGetSystemTimeNV;
PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR = NULL;
PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR = NULL;
PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR = NULL;

// Occlusion Queries
PFNGLGENQUERIESEXTPROC 					glGenQueriesEXT = NULL;
PFNGLDELETEQUERIESEXTPROC 				glDeleteQueriesEXT = NULL;
PFNGLISQUERYEXTPROC 					glIsQueryEXT = NULL;
PFNGLBEGINQUERYEXTPROC 					glBeginQueryEXT = NULL;
PFNGLENDQUERYEXTPROC 					glEndQueryEXT = NULL;
PFNGLGETQUERYIVEXTPROC 					glGetQueryivEXT = NULL;  
PFNGLGETQUERYOBJECTIVEXTPROC 			glGetQueryObjectivEXT = NULL;
PFNGLGETQUERYOBJECTUIVEXTPROC 			glGetQueryObjectuivEXT = NULL;

PFNGLQUERYCOUNTEREXTPROC				glQueryCounterEXT = NULL;
PFNGLGETQUERYOBJECTUI64VEXTPROC			glGetQueryObjectui64vEXT = NULL;

// Offscreen MSAA rendering
PFNBLITFRAMEBUFFERNVPROC				glBlitFramebufferNV = NULL;
PFNGLDISCARDFRAMEBUFFEREXTPROC			glDiscardFramebufferEXT = NULL;
PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC	glFramebufferTexture2DMultisampleEXT = NULL;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC	glRenderbufferStorageMultisampleEXT = NULL;

PFNGLPUSHGROUPMARKEREXTPROC				glPushGroupMarkerEXT = NULL;
PFNGLPOPGROUPMARKEREXTPROC				glPopGroupMarkerEXT = NULL;
PFNGLLABELOBJECTEXTPROC					glLabelObjectEXT = NULL;
PFNGLGETOBJECTLABELEXTPROC				glGetObjectLabelEXT = NULL;

PFNGLMAPBUFFEROESPROC					glMapBufferOES = NULL;
PFNGLUNMAPBUFFEROESPROC					glUnmapBufferOES = NULL;

PFNGLTEXSTORAGE2DPROC					glTexStorage2D = NULL;

// KHR_debug
PFNGLDEBUGMESSAGECONTROLKHRPROC			glDebugMessageControlKHR = NULL;
PFNGLDEBUGMESSAGEINSERTKHRPROC			glDebugMessageInsertKHR = NULL;
PFNGLDEBUGMESSAGECALLBACKKHRPROC		glDebugMessageCallbackKHR = NULL;
PFNGLGETDEBUGMESSAGELOGKHRPROC			glDebugMessageLogKHR = NULL;
PFNGLGETPOINTERVKHRPROC					glGetPointervKHR = NULL;
PFNGLPUSHDEBUGGROUPKHRPROC				glPushDebugGroupKHR = NULL;
PFNGLPOPDEBUGGROUPKHRPROC				glPopDebugGroupKHR = NULL;
PFNGLOBJECTLABELKHRPROC					glObjectLabelKHR = NULL;
PFNGLGETOBJECTLABELKHRPROC				glGetObjectLabelKHR = NULL;
PFNGLOBJECTPTRLABELKHRPROC				glObjectPtrLabelKHR = NULL;
PFNGLGETOBJECTPTRLABELKHRPROC			glGetObjectPtrLabelKHR = NULL;

PFNGLDRAWELEMENTSINSTANCEDPROC			glDrawElementsInstanced = NULL;
PFNGLDRAWARRAYSINSTANCEDPROC			glDrawArraysInstanced = NULL;
PFNGLVERTEXATTRIBDIVISORPROC			glVertexAttribDivisor = NULL;

static TAutoConsoleVariable<int32> CVarAndroidDisableTextureFormatBGRA8888(
	TEXT("android.DisableTextureFormatBGRA8888"),
	0,
	TEXT("Whether to disable usage of GL_EXT_texture_format_BGRA8888 extension.\n")
	TEXT(" 0: Enable when extension is available (default)\n")
	TEXT(" 1: Always disabled"),
	ECVF_ReadOnly);

struct FPlatformOpenGLDevice
{

	void SetCurrentSharedContext();
	void SetCurrentRenderingContext();
	void SetCurrentNULLContext();

	FPlatformOpenGLDevice();
	~FPlatformOpenGLDevice();
	void Init();
	void LoadEXT();
	void Terminate();
	void ReInit();
};


FPlatformOpenGLDevice::~FPlatformOpenGLDevice()
{
	AndroidEGL::GetInstance()->DestroyBackBuffer();
	AndroidEGL::GetInstance()->Terminate();
}

FPlatformOpenGLDevice::FPlatformOpenGLDevice()
{
}

// call out to JNI to see if the application was packaged for GearVR
extern bool AndroidThunkCpp_IsGearVRApplication();

void FPlatformOpenGLDevice::Init()
{
	extern void InitDebugContext();

	FPlatformMisc::LowLevelOutputDebugString(TEXT("FPlatformOpenGLDevice:Init"));
	bool bCreateSurface = !AndroidThunkCpp_IsGearVRApplication();
	AndroidEGL::GetInstance()->InitSurface(false, bCreateSurface);
	PlatformRenderingContextSetup(this);

	LoadEXT();

	InitDefaultGLContextState();
	InitDebugContext();

	PlatformSharedContextSetup(this);
	InitDefaultGLContextState();
	InitDebugContext();

	AndroidEGL::GetInstance()->InitBackBuffer(); //can be done only after context is made current.
}

FPlatformOpenGLDevice* PlatformCreateOpenGLDevice()
{
	FPlatformOpenGLDevice* Device = new FPlatformOpenGLDevice();
	Device->Init();
	return Device;
}

void PlatformReleaseOpenGLContext(FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context)
{
}

void* PlatformGetWindow(FPlatformOpenGLContext* Context, void** AddParam)
{
	check(Context);

	return (void*)&Context->eglContext;
}

bool PlatformBlitToViewport( FPlatformOpenGLDevice* Device, const FOpenGLViewport& Viewport, uint32 BackbufferSizeX, uint32 BackbufferSizeY, bool bPresent,bool bLockToVsync, int32 SyncInterval )
{
	if (bPresent && Viewport.GetCustomPresent())
	{
		bPresent = Viewport.GetCustomPresent()->Present(SyncInterval);
	}
	if (bPresent)
	{
		AndroidEGL::GetInstance()->SwapBuffers();
	}
	return bPresent;
}

void PlatformRenderingContextSetup(FPlatformOpenGLDevice* Device)
{
	Device->SetCurrentRenderingContext();
}

void PlatformFlushIfNeeded()
{
}

void PlatformRebindResources(FPlatformOpenGLDevice* Device)
{
}

void PlatformSharedContextSetup(FPlatformOpenGLDevice* Device)
{
	Device->SetCurrentSharedContext();
}

void PlatformNULLContextSetup()
{
	AndroidEGL::GetInstance()->SetCurrentContext(EGL_NO_CONTEXT, EGL_NO_SURFACE);
}

EOpenGLCurrentContext PlatformOpenGLCurrentContext(FPlatformOpenGLDevice* Device)
{
	return (EOpenGLCurrentContext)AndroidEGL::GetInstance()->GetCurrentContextType();
}

void PlatformRestoreDesktopDisplayMode()
{
}

bool PlatformInitOpenGL()
{
	return true;
}

bool PlatformOpenGLContextValid()
{
	return AndroidEGL::GetInstance()->IsCurrentContextValid();
}

void PlatformGetBackbufferDimensions( uint32& OutWidth, uint32& OutHeight )
{
	AndroidEGL::GetInstance()->GetDimensions(OutWidth, OutHeight);
}

// =============================================================

void PlatformGetNewOcclusionQuery( GLuint* OutQuery, uint64* OutQueryContext )
{
}

bool PlatformContextIsCurrent( uint64 QueryContext )
{
	return true;
}

void FPlatformOpenGLDevice::LoadEXT()
{
	eglGetSystemTimeNV = (PFNEGLGETSYSTEMTIMENVPROC)((void*)eglGetProcAddress("eglGetSystemTimeNV"));
	eglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC)((void*)eglGetProcAddress("eglCreateSyncKHR"));
	eglDestroySyncKHR = (PFNEGLDESTROYSYNCKHRPROC)((void*)eglGetProcAddress("eglDestroySyncKHR"));
	eglClientWaitSyncKHR = (PFNEGLCLIENTWAITSYNCKHRPROC)((void*)eglGetProcAddress("eglClientWaitSyncKHR"));

	glDebugMessageControlKHR = (PFNGLDEBUGMESSAGECONTROLKHRPROC)((void*)eglGetProcAddress("glDebugMessageControlKHR"));

	// Some PowerVR drivers (Rogue Han and Intel-based devices) are crashing using glDebugMessageControlKHR (causes signal 11 crash)
	if (glDebugMessageControlKHR != NULL && FAndroidMisc::GetGPUFamily().Contains(TEXT("PowerVR")))
	{
		glDebugMessageControlKHR = NULL;
	}

	glDebugMessageInsertKHR = (PFNGLDEBUGMESSAGEINSERTKHRPROC)((void*)eglGetProcAddress("glDebugMessageInsertKHR"));
	glDebugMessageCallbackKHR = (PFNGLDEBUGMESSAGECALLBACKKHRPROC)((void*)eglGetProcAddress("glDebugMessageCallbackKHR"));
	glDebugMessageLogKHR = (PFNGLGETDEBUGMESSAGELOGKHRPROC)((void*)eglGetProcAddress("glDebugMessageLogKHR"));
	glGetPointervKHR = (PFNGLGETPOINTERVKHRPROC)((void*)eglGetProcAddress("glGetPointervKHR"));
	glPushDebugGroupKHR = (PFNGLPUSHDEBUGGROUPKHRPROC)((void*)eglGetProcAddress("glPushDebugGroupKHR"));
	glPopDebugGroupKHR = (PFNGLPOPDEBUGGROUPKHRPROC)((void*)eglGetProcAddress("glPopDebugGroupKHR"));
	glObjectLabelKHR = (PFNGLOBJECTLABELKHRPROC)((void*)eglGetProcAddress("glObjectLabelKHR"));
	glGetObjectLabelKHR = (PFNGLGETOBJECTLABELKHRPROC)((void*)eglGetProcAddress("glGetObjectLabelKHR"));
	glObjectPtrLabelKHR = (PFNGLOBJECTPTRLABELKHRPROC)((void*)eglGetProcAddress("glObjectPtrLabelKHR"));
	glGetObjectPtrLabelKHR = (PFNGLGETOBJECTPTRLABELKHRPROC)((void*)eglGetProcAddress("glGetObjectPtrLabelKHR"));
}

FPlatformOpenGLContext* PlatformCreateOpenGLContext(FPlatformOpenGLDevice* Device, void* InWindowHandle)
{
	//Assumes Device is already initialized and context already created.
	return AndroidEGL::GetInstance()->GetRenderingContext();
}

void PlatformDestroyOpenGLContext(FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context)
{
	delete Device; //created here, destroyed here, but held by RHI.
}

FRHITexture* PlatformCreateBuiltinBackBuffer(FOpenGLDynamicRHI* OpenGLRHI, uint32 SizeX, uint32 SizeY)
{
	uint32 Flags = TexCreate_RenderTargetable;
	FOpenGLTexture2D* Texture2D = new FOpenGLTexture2D(OpenGLRHI, AndroidEGL::GetInstance()->GetOnScreenColorRenderBuffer(), GL_RENDERBUFFER, GL_COLOR_ATTACHMENT0, SizeX, SizeY, 0, 1, 1, 1, PF_B8G8R8A8, false, false, Flags, nullptr, FClearValueBinding::Transparent);
	OpenGLTextureAllocated(Texture2D, Flags);

	return Texture2D;
}

void PlatformResizeGLContext( FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context, uint32 SizeX, uint32 SizeY, bool bFullscreen, bool bWasFullscreen, GLenum BackBufferTarget, GLuint BackBufferResource)
{
	check(Context);

	glViewport(0, 0, SizeX, SizeY);
	VERIFY_GL(glViewport);
}

void PlatformGetSupportedResolution(uint32 &Width, uint32 &Height)
{
}

bool PlatformGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate)
{
	return true;
}

int32 PlatformGlGetError()
{
	return glGetError();
}

// =============================================================

void PlatformReleaseOcclusionQuery( GLuint Query, uint64 QueryContext )
{
}

void FPlatformOpenGLDevice::SetCurrentSharedContext()
{
	AndroidEGL::GetInstance()->SetCurrentSharedContext();
}

void PlatformDestroyOpenGLDevice(FPlatformOpenGLDevice* Device)
{
	delete Device;
}

void FPlatformOpenGLDevice::SetCurrentRenderingContext()
{
	AndroidEGL::GetInstance()->SetCurrentRenderingContext();
}

void PlatformLabelObjects()
{
	// @todo: Check that there is a valid id (non-zero) as LabelObject will fail otherwise
	GLuint RenderBuffer = AndroidEGL::GetInstance()->GetOnScreenColorRenderBuffer();
	if (RenderBuffer != 0)
	{
		FOpenGL::LabelObject(GL_RENDERBUFFER, RenderBuffer, "OnScreenColorRB");
	}

	GLuint FrameBuffer = AndroidEGL::GetInstance()->GetResolveFrameBuffer();
	if (FrameBuffer != 0)
	{
		FOpenGL::LabelObject(GL_FRAMEBUFFER, FrameBuffer, "ResolveFB");
	}
}

//--------------------------------

void PlatformGetNewRenderQuery( GLuint* OutQuery, uint64* OutQueryContext )
{
	GLuint NewQuery = 0;
	FOpenGL::GenQueries( 1, &NewQuery );
	*OutQuery = NewQuery;
	*OutQueryContext = 0;
}

void PlatformReleaseRenderQuery( GLuint Query, uint64 QueryContext )
{
	FOpenGL::DeleteQueries(1, &Query );
}


bool FAndroidOpenGL::bUseHalfFloatTexStorage = false;
bool FAndroidOpenGL::bUseES30ShadingLanguage = false;
bool FAndroidOpenGL::bES30Support = false;
bool FAndroidOpenGL::bSupportsInstancing = false;

void FAndroidOpenGL::ProcessExtensions(const FString& ExtensionsString)
{
	FOpenGLES2::ProcessExtensions(ExtensionsString);

	FString VersionString = FString(ANSI_TO_TCHAR((const ANSICHAR*)glGetString(GL_VERSION)));
	
	bES30Support = VersionString.Contains(TEXT("OpenGL ES 3."));

	// Get procedures
	if (bSupportsOcclusionQueries || bSupportsDisjointTimeQueries)
	{
		glGenQueriesEXT        = (PFNGLGENQUERIESEXTPROC)       ((void*)eglGetProcAddress("glGenQueriesEXT"));
		glDeleteQueriesEXT     = (PFNGLDELETEQUERIESEXTPROC)    ((void*)eglGetProcAddress("glDeleteQueriesEXT"));
		glIsQueryEXT           = (PFNGLISQUERYEXTPROC)          ((void*)eglGetProcAddress("glIsQueryEXT"));
		glBeginQueryEXT        = (PFNGLBEGINQUERYEXTPROC)       ((void*)eglGetProcAddress("glBeginQueryEXT"));
		glEndQueryEXT          = (PFNGLENDQUERYEXTPROC)         ((void*)eglGetProcAddress("glEndQueryEXT"));
		glGetQueryivEXT        = (PFNGLGETQUERYIVEXTPROC)       ((void*)eglGetProcAddress("glGetQueryivEXT"));
		glGetQueryObjectivEXT  = (PFNGLGETQUERYOBJECTIVEXTPROC) ((void*)eglGetProcAddress("glGetQueryObjectivEXT"));
		glGetQueryObjectuivEXT = (PFNGLGETQUERYOBJECTUIVEXTPROC)((void*)eglGetProcAddress("glGetQueryObjectuivEXT"));
	}

	if (bSupportsDisjointTimeQueries)
	{
		glQueryCounterEXT			= (PFNGLQUERYCOUNTEREXTPROC)		((void*)eglGetProcAddress("glQueryCounterEXT"));
		glGetQueryObjectui64vEXT	= (PFNGLGETQUERYOBJECTUI64VEXTPROC)	((void*)eglGetProcAddress("glGetQueryObjectui64vEXT"));

		// If EXT_disjoint_timer_query wasn't found, NV_timer_query might be available
		if (glQueryCounterEXT == NULL)
		{
			glQueryCounterEXT = (PFNGLQUERYCOUNTEREXTPROC)eglGetProcAddress("glQueryCounterNV");
		}
		if (glGetQueryObjectui64vEXT == NULL)
		{
			glGetQueryObjectui64vEXT = (PFNGLGETQUERYOBJECTUI64VEXTPROC)eglGetProcAddress("glGetQueryObjectui64vNV");
		}
	}

	glDiscardFramebufferEXT = (PFNGLDISCARDFRAMEBUFFEREXTPROC)((void*)eglGetProcAddress("glDiscardFramebufferEXT"));
	glFramebufferTexture2DMultisampleEXT = (PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)((void*)eglGetProcAddress("glFramebufferTexture2DMultisampleEXT"));
	glRenderbufferStorageMultisampleEXT = (PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)((void*)eglGetProcAddress("glRenderbufferStorageMultisampleEXT"));
	glPushGroupMarkerEXT = (PFNGLPUSHGROUPMARKEREXTPROC)((void*)eglGetProcAddress("glPushGroupMarkerEXT"));
	glPopGroupMarkerEXT = (PFNGLPOPGROUPMARKEREXTPROC)((void*)eglGetProcAddress("glPopGroupMarkerEXT"));
	glLabelObjectEXT = (PFNGLLABELOBJECTEXTPROC)((void*)eglGetProcAddress("glLabelObjectEXT"));
	glGetObjectLabelEXT = (PFNGLGETOBJECTLABELEXTPROC)((void*)eglGetProcAddress("glGetObjectLabelEXT"));

	bSupportsETC2 = bES30Support;
	bUseES30ShadingLanguage = bES30Support;

	FString RendererString = FString(ANSI_TO_TCHAR((const ANSICHAR*)glGetString(GL_RENDERER)));

	if (RendererString.Contains(TEXT("SGX 540")))
	{
		UE_LOG(LogRHI, Warning, TEXT("Disabling support for GL_OES_packed_depth_stencil on SGX 540"));
		bSupportsPackedDepthStencil = false;
		bRequiresTexture2DPrecisionHack = true;
	}

	const bool bIsAdrenoBased = RendererString.Contains(TEXT("Adreno"));
	if (bIsAdrenoBased)
	{
		// This is to avoid a bug in Adreno drivers that define GL_EXT_shader_framebuffer_fetch even when device does not support this extension
		// OpenGL ES 3.1 V@127.0 (GIT@I1af360237c)
		bRequiresShaderFramebufferFetchUndef = !bSupportsShaderFramebufferFetch;
		bRequiresARMShaderFramebufferFetchDepthStencilUndef = !bSupportsShaderDepthStencilFetch;

		// Adreno 2xx doesn't work with packed depth stencil enabled
		if (RendererString.Contains(TEXT("Adreno (TM) 2")))
		{
			UE_LOG(LogRHI, Warning, TEXT("Disabling support for GL_OES_packed_depth_stencil on Adreno 2xx"));
			bSupportsPackedDepthStencil = false;
		}
	}

	if (bES30Support)
	{
		glDrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC)((void*)eglGetProcAddress("glDrawElementsInstanced"));
		glDrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)((void*)eglGetProcAddress("glDrawArraysInstanced"));
		glVertexAttribDivisor = (PFNGLVERTEXATTRIBDIVISORPROC)((void*)eglGetProcAddress("glVertexAttribDivisor"));

		bSupportsInstancing = true;
	}

	if (bES30Support || bIsAdrenoBased)
	{
		// Attempt to find ES 3.0 glTexStorage2D if we're on an ES 3.0 device
		glTexStorage2D = (PFNGLTEXSTORAGE2DPROC)((void*)eglGetProcAddress("glTexStorage2D"));
		if( glTexStorage2D != NULL )
		{
			bUseHalfFloatTexStorage = true;
		}
		else
		{
			// need to disable GL_EXT_color_buffer_half_float support because we have no way to allocate the storage and the driver doesn't work without it.
			UE_LOG(LogRHI,Warning,TEXT("Disabling support for GL_EXT_color_buffer_half_float as we cannot bind glTexStorage2D"));
			bSupportsColorBufferHalfFloat = false;
		}
	}

	//@todo android: need GMSAAAllowed	 ?
	if (bSupportsNVFrameBufferBlit)
	{
		glBlitFramebufferNV = (PFNBLITFRAMEBUFFERNVPROC)((void*)eglGetProcAddress("glBlitFramebufferNV"));
	}

	glMapBufferOES = (PFNGLMAPBUFFEROESPROC)((void*)eglGetProcAddress("glMapBufferOES"));
	glUnmapBufferOES = (PFNGLUNMAPBUFFEROESPROC)((void*)eglGetProcAddress("glUnmapBufferOES"));

	//On Android, there are problems compiling shaders with textureCubeLodEXT calls in the glsl code,
	// so we set this to false to modify the glsl manually at compile-time.
	bSupportsTextureCubeLodEXT = false;

	// On some Android devices with Mali GPUs textureCubeLod is not available.
	if (RendererString.Contains(TEXT("Mali-400")))
	{
		bSupportsShaderTextureCubeLod = false;
	}
	
	// Nexus 5 (Android 4.4.2) doesn't like glVertexAttribDivisor(index, 0) called when not using a glDrawElementsInstanced
	if (bIsAdrenoBased && VersionString.Contains(TEXT("OpenGL ES 3.0 V@66.0 AU@  (CL@)")))
	{
		UE_LOG(LogRHI, Warning, TEXT("Disabling support for hardware instancing on Adreno 330 OpenGL ES 3.0 V@66.0 AU@  (CL@)"));
		bSupportsInstancing = false;
	}

	if (bSupportsBGRA8888 && CVarAndroidDisableTextureFormatBGRA8888.GetValueOnAnyThread() == 1)
	{
		UE_LOG(LogRHI, Warning, TEXT("Disabling support for GL_EXT_texture_format_BGRA8888"));
		bSupportsBGRA8888 = false;
	}
}


FString FAndroidMisc::GetGPUFamily()
{
	return FAndroidGPUInfo::Get().GPUFamily;
}

FString FAndroidMisc::GetGLVersion()
{
	return FAndroidGPUInfo::Get().GLVersion;
}

bool FAndroidMisc::SupportsFloatingPointRenderTargets()
{
	return FAndroidGPUInfo::Get().bSupportsFloatingPointRenderTargets && FAndroidGPUInfo::Get().bSupportsTextureHalfFloat;
}

bool FAndroidMisc::SupportsShaderFramebufferFetch()
{
	return FAndroidGPUInfo::Get().bSupportsFrameBufferFetch;
}

void FAndroidMisc::GetValidTargetPlatforms(TArray<FString>& TargetPlatformNames)
{
	TargetPlatformNames = FAndroidGPUInfo::Get().TargetPlatformNames;
}

void FAndroidAppEntry::PlatformInit()
{
	AndroidEGL::GetInstance()->Init(AndroidEGL::AV_OpenGLES, 2, 0, false);
}

#endif
