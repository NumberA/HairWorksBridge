// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "HMDPrivatePCH.h"
#include "GearVR.h"
#include "EngineAnalytics.h"
#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"
#include "RHIStaticStates.h"
#include "SceneViewport.h"
//#include "Android/AndroidEGL.h"

#if GEARVR_SUPPORTED_PLATFORMS
#include "VrApi_Helpers.h"
#endif

#include <android_native_app_glue.h>

#define DEFAULT_PREDICTION_IN_SECONDS 0.035

#if PLATFORM_ANDROID
// call out to JNI to see if the application was packaged for GearVR
bool AndroidThunkCpp_IsGearVRApplication()
{
	bool bIsGearVRApplication = false;
	if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
	{
		static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_IsGearVRApplication", "()Z", false);
		bIsGearVRApplication = FJavaWrapper::CallBooleanMethod(Env, FJavaWrapper::GameActivityThis, Method);
	}
	return bIsGearVRApplication;
}
#endif

//---------------------------------------------------
// GearVR Plugin Implementation
//---------------------------------------------------

#if GEARVR_SUPPORTED_PLATFORMS
static TAutoConsoleVariable<int32> CVarGearVREnableMSAA(TEXT("gearvr.EnableMSAA"), 1, TEXT("Enable MSAA when rendering on GearVR"));
#endif

class FGearVRPlugin : public IGearVRPlugin
{
	/** IHeadMountedDisplayModule implementation */
	virtual TSharedPtr< class IHeadMountedDisplay, ESPMode::ThreadSafe > CreateHeadMountedDisplay() override;

	// Pre-init the HMD module
	virtual void PreInit() override;

	FString GetModulePriorityKeyName() const
	{
		return FString(TEXT("GearVR"));
	}

	virtual void StartOVRGlobalMenu() const ;

	virtual void StartOVRQuitMenu() const ;

	virtual void SetCPUAndGPULevels(int32 CPULevel, int32 GPULevel) const ;

	virtual bool IsPowerLevelStateMinimum() const;

	virtual bool IsPowerLevelStateThrottled() const;
	
	virtual float GetTemperatureInCelsius() const;

	virtual float GetBatteryLevel() const;

	virtual bool AreHeadPhonesPluggedIn() const;

	virtual void SetLoadingIconTexture(FTextureRHIRef InTexture);

	virtual void SetLoadingIconMode(bool bActiveLoadingIcon);

	virtual void RenderLoadingIcon_RenderThread();

	virtual bool IsInLoadingIconMode() const;
};

IMPLEMENT_MODULE( FGearVRPlugin, GearVR )

TSharedPtr< class IHeadMountedDisplay, ESPMode::ThreadSafe > FGearVRPlugin::CreateHeadMountedDisplay()
{
#if GEARVR_SUPPORTED_PLATFORMS
	if (!AndroidThunkCpp_IsGearVRApplication())
	{
		// don't do anything if we aren't packaged for GearVR
		return NULL;
	}

	TSharedPtr< FGearVR, ESPMode::ThreadSafe > GearVR(new FGearVR());
	if (GearVR->IsInitialized())
	{
		return GearVR;
	}
#endif//GEARVR_SUPPORTED_PLATFORMS
	return NULL;
}

void FGearVRPlugin::PreInit()
{
#if GEARVR_SUPPORTED_PLATFORMS
	if (!AndroidThunkCpp_IsGearVRApplication())
	{
		// don't do anything if we aren't packaged for GearVR
		return;
	}
#endif//GEARVR_SUPPORTED_PLATFORMS
}


#if GEARVR_SUPPORTED_PLATFORMS
FSettings::FSettings()
	: RenderTargetSize(OVR_DEFAULT_EYE_RENDER_TARGET_WIDTH * 2, OVR_DEFAULT_EYE_RENDER_TARGET_HEIGHT)
	, MotionPredictionInSeconds(DEFAULT_PREDICTION_IN_SECONDS)
	, HeadModel(0.12f, 0.0f, 0.17f)
{
	CpuLevel = 2;
	GpuLevel = 3;
	HFOVInRadians = FMath::DegreesToRadians(90.f);
	VFOVInRadians = FMath::DegreesToRadians(90.f);
	HmdToEyeViewOffset[0] = HmdToEyeViewOffset[1] = OVR::Vector3f(0,0,0);
	IdealScreenPercentage = ScreenPercentage = SavedScrPerc = 100.f;
	InterpupillaryDistance = OVR_DEFAULT_IPD;

	Flags.bStereoEnabled = false; Flags.bHMDEnabled = true;
	Flags.bUpdateOnRT = Flags.bTimeWarp = true;
}

TSharedPtr<FHMDSettings, ESPMode::ThreadSafe> FSettings::Clone() const
{
	TSharedPtr<FSettings, ESPMode::ThreadSafe> NewSettings = MakeShareable(new FSettings(*this));
	return NewSettings;
}

//////////////////////////////////////////////////////////////////////////
FGameFrame::FGameFrame()
{
	FMemory::Memzero(CurEyeRenderPose);
	FMemory::Memzero(CurSensorState);
	FMemory::Memzero(EyeRenderPose);
	FMemory::Memzero(HeadPose);
	FMemory::Memzero(TanAngleMatrix);
	GameThreadId = 0;
}

TSharedPtr<FHMDGameFrame, ESPMode::ThreadSafe> FGameFrame::Clone() const
{
	TSharedPtr<FGameFrame, ESPMode::ThreadSafe> NewFrame = MakeShareable(new FGameFrame(*this));
	return NewFrame;
}


//---------------------------------------------------
// GearVR IHeadMountedDisplay Implementation
//---------------------------------------------------

TSharedPtr<FHMDGameFrame, ESPMode::ThreadSafe> FGearVR::CreateNewGameFrame() const
{
	TSharedPtr<FGameFrame, ESPMode::ThreadSafe> Result(MakeShareable(new FGameFrame()));
	return Result;
}

TSharedPtr<FHMDSettings, ESPMode::ThreadSafe> FGearVR::CreateNewSettings() const
{
	TSharedPtr<FSettings, ESPMode::ThreadSafe> Result(MakeShareable(new FSettings()));
	return Result;
}

bool FGearVR::OnStartGameFrame( FWorldContext& WorldContext )
{
	// Temp fix to a bug in ovr_DeviceIsDocked() that can't return
	// actual state of docking. We are switching to stereo at the start
	// (missing the first frame to let it render at least once; otherwise
	// a blurry image may appear on Note4 with Adreno 420).
	if (GFrameNumber > 2 && !Settings->Flags.bStereoEnforced)
	{
		EnableStereo(true);
	}

#if 0 // temporarily out of order. Until ovr_DeviceIsDocked returns the actual state.
	if (ovr_DeviceIsDocked() != Settings->IsStereoEnabled())
	{
		if (!Settings->IsStereoEnabled() || !Settings->Flags.bStereoEnforced)
		{
			UE_LOG(LogHMD, Log, TEXT("Device is docked/undocked, changing stereo mode to %s"), (ovr_DeviceIsDocked()) ? TEXT("ON") : TEXT("OFF"));
			EnableStereo(ovr_DeviceIsDocked());
		}
	}
#endif // if 0

	bool rv = FHeadMountedDisplay::OnStartGameFrame(WorldContext);
	if (!rv)
	{
		return false;
	}

	FGameFrame* CurrentFrame = GetFrame();

	// need to make a copy of settings here, since settings could change.
	CurrentFrame->Settings = Settings->Clone();
	FSettings* CurrentSettings = CurrentFrame->GetSettings();

	if (OCFlags.bResumed && CurrentSettings->IsStereoEnabled() && pGearVRBridge && pGearVRBridge->IsTextureSetCreated())
	{
		if (!HasValidOvrMobile())
		{
			// re-enter VR mode if necessary
			EnterVRMode();
		}
	}
	CurrentFrame->GameThreadId = gettid();

	rv = GetEyePoses(*CurrentFrame, CurrentFrame->CurEyeRenderPose, CurrentFrame->CurSensorState);

#if !UE_BUILD_SHIPPING
	{ // used for debugging, do not remove
		FQuat CurHmdOrientation;
		FVector CurHmdPosition;
		GetCurrentPose(CurHmdOrientation, CurHmdPosition, false, false);
		//UE_LOG(LogHMD, Log, TEXT("BFPOSE: Pos %.3f %.3f %.3f, fr: %d"), CurHmdPosition.X, CurHmdPosition.Y, CurHmdPosition.Y,(int)CurrentFrame->FrameNumber);
		//UE_LOG(LogHMD, Log, TEXT("BFPOSE: Yaw %.3f Pitch %.3f Roll %.3f, fr: %d"), CurHmdOrientation.Rotator().Yaw, CurHmdOrientation.Rotator().Pitch, CurHmdOrientation.Rotator().Roll, (int)CurrentFrame->FrameNumber);
	}
#endif
	return rv;
}

FGameFrame* FGearVR::GetFrame() const
{
	return static_cast<FGameFrame*>(GetCurrentFrame());
}

EHMDDeviceType::Type FGearVR::GetHMDDeviceType() const
{
	return EHMDDeviceType::DT_GearVR;
}

bool FGearVR::GetHMDMonitorInfo(MonitorInfo& MonitorDesc)
{
	if (!GetSettings()->IsStereoEnabled())
	{
		return false;
	}
	MonitorDesc.MonitorName = "";
	MonitorDesc.MonitorId = 0;
	MonitorDesc.DesktopX = MonitorDesc.DesktopY = 0;
	MonitorDesc.ResolutionX = GetSettings()->RenderTargetSize.X;
	MonitorDesc.ResolutionY = GetSettings()->RenderTargetSize.Y;
	return true;
}

bool FGearVR::IsHMDConnected()
{
	//? @todo
	return true;
}

bool FGearVR::IsInLowPersistenceMode() const
{
	return true;
}

bool FGearVR::GetEyePoses(const FGameFrame& InFrame, ovrPosef outEyePoses[2], ovrTracking& outTracking)
{
	FOvrMobileSynced OvrMobile = GetMobileSynced();

	if (!OvrMobile.IsValid())
	{
		FMemory::Memzero(outTracking);
		ovrQuatf identityQ;
		FMemory::Memzero(identityQ);
		identityQ.w = 1;
		outTracking.HeadPose.Pose.Orientation = identityQ;
		const OVR::Vector3f OvrHeadModel = ToOVRVector<OVR::Vector3f>(InFrame.GetSettings()->HeadModel); // HeadModel is already in meters here
		const OVR::Vector3f HmdToEyeViewOffset0 = (OVR::Vector3f)InFrame.GetSettings()->HmdToEyeViewOffset[0];
		const OVR::Vector3f HmdToEyeViewOffset1 = (OVR::Vector3f)InFrame.GetSettings()->HmdToEyeViewOffset[1];
		const OVR::Vector3f transl0 = OvrHeadModel + HmdToEyeViewOffset0;
		const OVR::Vector3f transl1 = OvrHeadModel + HmdToEyeViewOffset1;
		outEyePoses[0].Orientation = outEyePoses[1].Orientation = outTracking.HeadPose.Pose.Orientation;
		outEyePoses[0].Position = transl0;
		outEyePoses[1].Position = transl1;
		return false;
	}

	double predictedTime = 0.0;
	const double now = vrapi_GetTimeInSeconds();
	if (IsInGameThread())
	{
		if (OCFlags.NeedResetOrientationAndPosition)
		{
			ResetOrientationAndPosition(ResetToYaw);
		}

		// Get the latest head tracking state, predicted ahead to the midpoint of the time
		// it will be displayed.  It will always be corrected to the real values by
		// time warp, but the closer we get, the less black will be pulled in at the edges.
		static double prev;
		const double rawDelta = now - prev;
		prev = now;
		const double clampedPrediction = FMath::Min( 0.1, rawDelta * 2 );
		predictedTime = now + clampedPrediction;

		//UE_LOG(LogHMD, Log, TEXT("GT Frame %d, predicted time: %.6f, delta %.6f"), InFrame.FrameNumber, (float)(clampedPrediction), float(rawDelta));
	}
	else if (IsInRenderingThread())
	{
		predictedTime = vrapi_GetPredictedDisplayTime(*OvrMobile, InFrame.FrameNumber);
		//UE_LOG(LogHMD, Log, TEXT("RT Frame %d, predicted time: %.6f"), InFrame.FrameNumber, (float)(predictedTime - now));
	}
	outTracking = vrapi_GetPredictedTracking(*OvrMobile, predictedTime);

	const OVR::Posef hmdPose = (OVR::Posef)outTracking.HeadPose.Pose;
	const OVR::Vector3f OvrHeadModel = ToOVRVector<OVR::Vector3f>(InFrame.GetSettings()->HeadModel); // HeadModel is already in meters here
	const OVR::Vector3f HmdToEyeViewOffset0 = (OVR::Vector3f)InFrame.GetSettings()->HmdToEyeViewOffset[0];
	const OVR::Vector3f HmdToEyeViewOffset1 = (OVR::Vector3f)InFrame.GetSettings()->HmdToEyeViewOffset[1];
	const OVR::Vector3f transl0 = hmdPose.Orientation.Rotate(OvrHeadModel + HmdToEyeViewOffset0);
	const OVR::Vector3f transl1 = hmdPose.Orientation.Rotate(OvrHeadModel + HmdToEyeViewOffset1);

	// Currently HmdToEyeViewOffset is only a 3D vector
	// (Negate HmdToEyeViewOffset because offset is a view matrix offset and not a camera offset)
	outEyePoses[0].Orientation = outEyePoses[1].Orientation = outTracking.HeadPose.Pose.Orientation;
	outEyePoses[0].Position = transl0;
	outEyePoses[1].Position = transl1;
	return true;
}

void FGameFrame::PoseToOrientationAndPosition(const ovrPosef& InPose, FQuat& OutOrientation, FVector& OutPosition) const
{
	OutOrientation = ToFQuat(InPose.Orientation);

	check(WorldToMetersScale >= 0);
	// correct position according to BaseOrientation and BaseOffset. 
	const FVector Pos = (ToFVector_M2U(OVR::Vector3f(InPose.Position), WorldToMetersScale) - (Settings->BaseOffset * WorldToMetersScale)) * CameraScale3D;
	OutPosition = Settings->BaseOrientation.Inverse().RotateVector(Pos);

	// apply base orientation correction to OutOrientation
	OutOrientation = Settings->BaseOrientation.Inverse() * OutOrientation;
	OutOrientation.Normalize();
}

void FGearVR::GetCurrentPose(FQuat& CurrentHmdOrientation, FVector& CurrentHmdPosition, bool bUseOrienationForPlayerCamera, bool bUsePositionForPlayerCamera)
{
	check(IsInGameThread());

	auto frame = GetFrame();
	check(frame);

	if (bUseOrienationForPlayerCamera || bUsePositionForPlayerCamera)
	{
		// if this pose is going to be used for camera update then save it.
		// This matters only if bUpdateOnRT is OFF.
		frame->EyeRenderPose[0] = frame->CurEyeRenderPose[0];
		frame->EyeRenderPose[1] = frame->CurEyeRenderPose[1];
		frame->HeadPose = frame->CurSensorState.HeadPose;
	}

	frame->PoseToOrientationAndPosition(frame->CurSensorState.HeadPose.Pose, CurrentHmdOrientation, CurrentHmdPosition);
	//UE_LOG(LogHMD, Log, TEXT("CRPOSE: Pos %.3f %.3f %.3f"), CurrentHmdPosition.X, CurrentHmdPosition.Y, CurrentHmdPosition.Z);
	//UE_LOG(LogHMD, Log, TEXT("CRPOSE: Yaw %.3f Pitch %.3f Roll %.3f"), CurrentHmdOrientation.Rotator().Yaw, CurrentHmdOrientation.Rotator().Pitch, CurrentHmdOrientation.Rotator().Roll);
}

TSharedPtr<class ISceneViewExtension, ESPMode::ThreadSafe> FGearVR::GetViewExtension()
{
	TSharedPtr<FViewExtension, ESPMode::ThreadSafe> Result(MakeShareable(new FViewExtension(this)));
	return Result;
}

void FGearVR::ResetStereoRenderingParams()
{
	FHeadMountedDisplay::ResetStereoRenderingParams();
	Settings->InterpupillaryDistance = OVR_DEFAULT_IPD;
}

bool FGearVR::Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar )
{
	if (FHeadMountedDisplay::Exec(InWorld, Cmd, Ar))
	{
		return true;
	}
	else if (FParse::Command(&Cmd, TEXT("OVRGLOBALMENU")))
	{
		// fire off the global menu from the render thread
		ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(OVRGlobalMenu,
			FGearVR*, Plugin, this,
			{
				Plugin->StartOVRGlobalMenu();
			});
		return true;
	}
	else if (FParse::Command(&Cmd, TEXT("OVRQUITMENU")))
	{
		// fire off the global menu from the render thread
		ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(OVRQuitMenu,
			FGearVR*, Plugin, this,
			{
				Plugin->StartOVRQuitMenu();
			});
		return true;
	}
#if !UE_BUILD_SHIPPING
	else if (FParse::Command(&Cmd, TEXT("OVRLD")))
	{
		SetLoadingIconMode(!IsInLoadingIconMode());
		return true;
	}
	else if (FParse::Command(&Cmd, TEXT("OVRLDI")))
	{
		if (!IsInLoadingIconMode())
		{
			const TCHAR* iconPath = TEXT("/Game/Loading/LoadingIconTexture.LoadingIconTexture");
			UE_LOG(LogHMD, Log, TEXT("Loading texture for loading icon %s..."), iconPath);
			UTexture2D* LoadingTexture = LoadObject<UTexture2D>(NULL, iconPath, NULL, LOAD_None, NULL);
			UE_LOG(LogHMD, Log, TEXT("...EEE"));
			if (LoadingTexture != nullptr)
			{
				ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
				SetRenderLoadingTex,
				FGearVR*, pGearVR, this,
				UTexture2D*, LoadingTexture, LoadingTexture,
				{
					UE_LOG(LogHMD, Log, TEXT("...Success. Loading icon format %d"), int(LoadingTexture->Resource->TextureRHI->GetFormat()));
					pGearVR->SetLoadingIconTexture(LoadingTexture->Resource->TextureRHI);
				});
				FlushRenderingCommands();
			}
			else
			{
				UE_LOG(LogHMD, Warning, TEXT("Can't load texture %s for loading icon"), iconPath);
			}
			return true;
		}
		else
		{
			SetLoadingIconTexture(nullptr);
		}
	}
#endif
	return false;
}

FString FGearVR::GetVersionString() const
{
	FString VerStr = ANSI_TO_TCHAR(vrapi_GetVersionString());
	FString s = FString::Printf(TEXT("%s, VrLib: %s, built %s, %s"), *FEngineVersion::Current().ToString(), *VerStr,
		UTF8_TO_TCHAR(__DATE__), UTF8_TO_TCHAR(__TIME__));
	return s;
}

void FGearVR::OnScreenModeChange(EWindowMode::Type WindowMode)
{
	//EnableStereo(WindowMode != EWindowMode::Windowed);
	//UpdateStereoRenderingParams();
}

bool FGearVR::IsPositionalTrackingEnabled() const
{
	return false;
}

bool FGearVR::EnablePositionalTracking(bool enable)
{
	return false;
}

static class FSceneViewport* FindSceneViewport()
{
	UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
	return GameEngine->SceneViewport.Get();
}

bool FGearVR::EnableStereo(bool bStereo)
{
	Settings->Flags.bStereoEnforced = false;
	if (bStereo)
	{
		Flags.bNeedEnableStereo = true;
	}
	else
	{
		Flags.bNeedDisableStereo = true;
	}
	return Settings->Flags.bStereoEnabled;
}

bool FGearVR::DoEnableStereo(bool bStereo, bool bApplyToHmd)
{
	check(IsInGameThread());

	FSceneViewport* SceneVP = FindSceneViewport();
	if (bStereo && (!SceneVP || !SceneVP->IsStereoRenderingAllowed()))
	{
		return false;
	}

	// Uncap fps to enable FPS higher than 62
	GEngine->bForceDisableFrameRateSmoothing = bStereo;

	bool stereoToBeEnabled = (Settings->Flags.bHMDEnabled) ? bStereo : false;

	if ((Settings->Flags.bStereoEnabled && stereoToBeEnabled) || (!Settings->Flags.bStereoEnabled && !stereoToBeEnabled))
	{
		// already in the desired mode
		return Settings->Flags.bStereoEnabled;
	}

 	TSharedPtr<SWindow> Window;
 	if (SceneVP)
 	{
 		Window = SceneVP->FindWindow();
 	}

	Settings->Flags.bStereoEnabled = stereoToBeEnabled;

	if (!stereoToBeEnabled)
	{
		LeaveVRMode();
	}
	return Settings->Flags.bStereoEnabled;
}

void FGearVR::ApplySystemOverridesOnStereo(bool bForce)
{
	if (Settings->Flags.bStereoEnabled || bForce)
	{
		// Set the current VSync state
		if (Settings->Flags.bOverrideVSync)
		{
			static IConsoleVariable* CVSyncVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
			CVSyncVar->Set(Settings->Flags.bVSync);
		}
		else
		{
			static IConsoleVariable* CVSyncVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
			Settings->Flags.bVSync = CVSyncVar->GetInt() != 0;
		}

		static IConsoleVariable* CFinishFrameVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FinishCurrentFrame"));
		CFinishFrameVar->Set(Settings->Flags.bAllowFinishCurrentFrame);
	}
}

void FGearVR::SaveSystemValues()
{
	static IConsoleVariable* CVSyncVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
	Settings->Flags.bSavedVSync = CVSyncVar->GetInt() != 0;

	static IConsoleVariable* CScrPercVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	Settings->SavedScrPerc = CScrPercVar->GetFloat();
}

void FGearVR::RestoreSystemValues()
{
	static IConsoleVariable* CVSyncVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
	CVSyncVar->Set(Settings->Flags.bSavedVSync);

	static IConsoleVariable* CScrPercVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	CScrPercVar->Set(Settings->SavedScrPerc);

	static IConsoleVariable* CFinishFrameVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FinishCurrentFrame"));
	CFinishFrameVar->Set(false);
}

void FGearVR::CalculateStereoViewOffset(const EStereoscopicPass StereoPassType, const FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation)
{
	check(WorldToMeters != 0.f);

	const int idx = (StereoPassType == eSSP_LEFT_EYE) ? 0 : 1;

	if (IsInGameThread())
	{
		const auto frame = GetFrame();
		if (!frame)
		{
			return;
		}

		// This method is called from GetProjectionData on a game thread.
		// The modified ViewLocation is used ONLY for ViewMatrix composition, it is not
		// stored modified in the ViewInfo. ViewInfo.ViewLocation remains unmodified.

		if (StereoPassType != eSSP_FULL || frame->Settings->Flags.bHeadTrackingEnforced)
		{
			if (!frame->Flags.bOrientationChanged)
			{
				UE_LOG(LogHMD, Log, TEXT("Orientation wasn't applied to a camera in frame %d"), int(GFrameCounter));
			}

			FVector CurEyePosition;
			FQuat CurEyeOrient;
			frame->PoseToOrientationAndPosition(frame->EyeRenderPose[idx], CurEyeOrient, CurEyePosition);

			FVector HeadPosition = FVector::ZeroVector;
			// If we use PlayerController->bFollowHmd then we must apply full EyePosition (HeadPosition == 0).
			// Otherwise, we will apply only a difference between EyePosition and HeadPosition, since
			// HeadPosition is supposedly already applied.
			if (!frame->Flags.bPlayerControllerFollowsHmd)
			{
				FQuat HeadOrient;
				frame->PoseToOrientationAndPosition(frame->HeadPose.Pose, HeadOrient, HeadPosition);
			}

			// apply stereo disparity to ViewLocation. Note, ViewLocation already contains HeadPose.Position, thus
			// we just need to apply delta between EyeRenderPose.Position and the HeadPose.Position. 
			// EyeRenderPose and HeadPose are captured by the same call to GetEyePoses.
			const FVector HmdToEyeOffset = CurEyePosition - HeadPosition;

			// Calculate the difference between the final ViewRotation and EyeOrientation:
			// we need to rotate the HmdToEyeOffset by this differential quaternion.
			// When bPlayerControllerFollowsHmd == true, the DeltaControlOrientation already contains
			// the proper value (see ApplyHmdRotation)
			//FRotator r = ViewRotation - CurEyeOrient.Rotator();
			const FQuat ViewOrient = ViewRotation.Quaternion();
			const FQuat DeltaControlOrientation =  ViewOrient * CurEyeOrient.Inverse();

			//UE_LOG(LogHMD, Log, TEXT("EYEROT: Yaw %.3f Pitch %.3f Roll %.3f"), CurEyeOrient.Rotator().Yaw, CurEyeOrient.Rotator().Pitch, CurEyeOrient.Rotator().Roll);
			//UE_LOG(LogHMD, Log, TEXT("VIEROT: Yaw %.3f Pitch %.3f Roll %.3f"), ViewRotation.Yaw, ViewRotation.Pitch, ViewRotation.Roll);
			//UE_LOG(LogHMD, Log, TEXT("DLTROT: Yaw %.3f Pitch %.3f Roll %.3f"), DeltaControlOrientation.Rotator().Yaw, DeltaControlOrientation.Rotator().Pitch, DeltaControlOrientation.Rotator().Roll);

			// The HMDPosition already has HMD orientation applied.
			// Apply rotational difference between HMD orientation and ViewRotation
			// to HMDPosition vector. 
			const FVector vEyePosition = DeltaControlOrientation.RotateVector(HmdToEyeOffset);
			ViewLocation += vEyePosition;

			//UE_LOG(LogHMD, Log, TEXT("DLTPOS: %.3f %.3f %.3f"), vEyePosition.X, vEyePosition.Y, vEyePosition.Z);
		}
	}
}

void FGearVR::ResetOrientationAndPosition(float yaw)
{
	check (IsInGameThread());

	auto frame = GetFrame();
	if (!frame)
	{
		OCFlags.NeedResetOrientationAndPosition = true;
		ResetToYaw = yaw;
		return;
	}

	const ovrPosef& pose = frame->CurSensorState.HeadPose.Pose;
	const OVR::Quatf orientation = OVR::Quatf(pose.Orientation);

	// Reset position
	Settings->BaseOffset = FVector::ZeroVector;

	FRotator ViewRotation;
	ViewRotation = FRotator(ToFQuat(orientation));
	ViewRotation.Pitch = 0;
	ViewRotation.Roll = 0;

	if (yaw != 0.f)
	{
		// apply optional yaw offset
		ViewRotation.Yaw -= yaw;
		ViewRotation.Normalize();
	}

	Settings->BaseOrientation = ViewRotation.Quaternion();
	OCFlags.NeedResetOrientationAndPosition = false;
}

void FGearVR::RebaseObjectOrientationAndPosition(FVector& OutPosition, FQuat& OutOrientation) const
{
}

FMatrix FGearVR::GetStereoProjectionMatrix(enum EStereoscopicPass StereoPassType, const float FOV) const
{
	auto frame = GetFrame();
	check(frame);
	check(IsStereoEnabled());

	const FSettings* FrameSettings = frame->GetSettings();

	const float ProjectionCenterOffset = 0.0f;
	const float PassProjectionOffset = (StereoPassType == eSSP_LEFT_EYE) ? ProjectionCenterOffset : -ProjectionCenterOffset;

	const float HalfFov = FrameSettings->HFOVInRadians / 2.0f;
	const float InWidth = FrameSettings->RenderTargetSize.X / 2.0f;
	const float InHeight = FrameSettings->RenderTargetSize.Y;
	const float XS = 1.0f / tan(HalfFov);
	const float YS = InWidth / tan(HalfFov) / InHeight;

	// correct far and near planes for reversed-Z projection matrix
	const float InNearZ = (FrameSettings->NearClippingPlane) ? FrameSettings->NearClippingPlane : GNearClippingPlane;
	const float InFarZ = (FrameSettings->FarClippingPlane) ? FrameSettings->FarClippingPlane : GNearClippingPlane;

	const float M_2_2 = (InNearZ == InFarZ) ? 0.0f    : InNearZ / (InNearZ - InFarZ);
	const float M_3_2 = (InNearZ == InFarZ) ? InNearZ : -InFarZ * InNearZ / (InNearZ - InFarZ);

	FMatrix proj = FMatrix(
		FPlane(XS,                      0.0f,								    0.0f,							0.0f),
		FPlane(0.0f,					YS,	                                    0.0f,							0.0f),
		FPlane(0.0f,	                0.0f,								    M_2_2,							1.0f),
		FPlane(0.0f,					0.0f,								    M_3_2,							0.0f))

		* FTranslationMatrix(FVector(PassProjectionOffset,0,0));
	
	ovrMatrix4f tanAngleMatrix = ToMatrix4f(proj);
	frame->TanAngleMatrix = ovrMatrix4f_TanAngleMatrixFromProjection(&tanAngleMatrix);
	return proj;
}

void FGearVR::InitCanvasFromView(FSceneView* InView, UCanvas* Canvas)
{
	// This is used for placing small HUDs (with names)
	// over other players (for example, in Capture Flag).
	// HmdOrientation should be initialized by GetCurrentOrientation (or
	// user's own value).
}

//---------------------------------------------------
// GearVR ISceneViewExtension Implementation
//---------------------------------------------------

void FGearVR::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	InViewFamily.EngineShowFlags.MotionBlur = 0;
	InViewFamily.EngineShowFlags.HMDDistortion = false;
	InViewFamily.EngineShowFlags.ScreenPercentage = false;
	InViewFamily.EngineShowFlags.StereoRendering = IsStereoEnabled();
}

void FGearVR::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	auto frame = GetFrame();
	check(frame);

	InView.BaseHmdOrientation = frame->LastHmdOrientation;
	InView.BaseHmdLocation = frame->LastHmdPosition;

	InViewFamily.bUseSeparateRenderTarget = ShouldUseSeparateRenderTarget();

	const int eyeIdx = (InView.StereoPass == eSSP_LEFT_EYE) ? 0 : 1;

	InView.ViewRect = frame->GetSettings()->EyeRenderViewport[eyeIdx];

	frame->CachedViewRotation[eyeIdx] = InView.ViewRotation;
}

FGearVR::FGearVR()
	: DeltaControlRotation(FRotator::ZeroRotator)
{
	OCFlags.Raw = 0;
	DeltaControlRotation = FRotator::ZeroRotator;
	ResetToYaw = 0.f;

	Settings = MakeShareable(new FSettings);

	Startup();
}

FGearVR::~FGearVR()
{
	Shutdown();
}

void FGearVR::Startup()
{
//	Flags.bNeedEnableStereo = true;

	// grab the clock settings out of the ini
	const TCHAR* GearVRSettings = TEXT("GearVR.Settings");
	int CpuLevel = 2;
	int GpuLevel = 3;
	int MinimumVsyncs = 1;
	float HeadModelScale = 1.0f;
	GConfig->GetInt(GearVRSettings, TEXT("CpuLevel"), CpuLevel, GEngineIni);
	GConfig->GetInt(GearVRSettings, TEXT("GpuLevel"), GpuLevel, GEngineIni);
	GConfig->GetInt(GearVRSettings, TEXT("MinimumVsyncs"), MinimumVsyncs, GEngineIni);
	GConfig->GetFloat(GearVRSettings, TEXT("HeadModelScale"), HeadModelScale, GEngineIni);

	UE_LOG(LogHMD, Log, TEXT("GearVR starting with CPU: %d GPU: %d MinimumVsyncs: %d"), CpuLevel, GpuLevel, MinimumVsyncs);

	JavaGT.Vm = GJavaVM;
	JavaGT.Env = FAndroidApplication::GetJavaEnv();
	extern struct android_app* GNativeAndroidApp;
	JavaGT.ActivityObject = GNativeAndroidApp->activity->clazz;

	HmdInfo = vrapi_GetHmdInfo(&JavaGT);

	const ovrInitParms initParms = vrapi_DefaultInitParms(&JavaGT);
	vrapi_Initialize(&initParms);

	GetSettings()->HeadModel *= HeadModelScale;
	GetSettings()->MinimumVsyncs = MinimumVsyncs;
	GetSettings()->CpuLevel = CpuLevel;
	GetSettings()->GpuLevel = GpuLevel;

	FPlatformMisc::MemoryBarrier();

	if (!IsRunningGame() || (Settings->Flags.InitStatus & FSettings::eStartupExecuted) != 0)
	{
		// do not initialize plugin for server or if it was already initialized
		return;
	}
	Settings->Flags.InitStatus |= FSettings::eStartupExecuted;

	// register our application lifetime delegates
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FGearVR::ApplicationPauseDelegate);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FGearVR::ApplicationResumeDelegate);

	Settings->Flags.InitStatus |= FSettings::eInitialized;

	UpdateHmdRenderInfo();
	UpdateStereoRenderingParams();

#if !OVR_DEBUG_DRAW
	pGearVRBridge = new FGearVRCustomPresent(GNativeAndroidApp->activity->clazz, MinimumVsyncs);
#endif

	LoadFromIni();
	SaveSystemValues();

	if(CVarGearVREnableMSAA.GetValueOnAnyThread())
	{
		static IConsoleVariable* CVarMobileOnChipMSAA = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MobileOnChipMSAA"));
		if (CVarMobileOnChipMSAA)
		{
			UE_LOG(LogHMD, Log, TEXT("Enabling r.MobileOnChipMSAA, previous value %d"), CVarMobileOnChipMSAA->GetInt());
			CVarMobileOnChipMSAA->Set(1);
		}
	}

	UE_LOG(LogHMD, Log, TEXT("GearVR has started"));
}

void FGearVR::Shutdown()
{
	if (!(Settings->Flags.InitStatus & FSettings::eStartupExecuted))
	{
		return;
	}

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(ShutdownRen,
	FGearVR*, Plugin, this,
	{
		Plugin->ShutdownRendering();
		if (Plugin->pGearVRBridge)
		{
			Plugin->pGearVRBridge->Shutdown();
			Plugin->pGearVRBridge = nullptr;
		}

	});

	// Wait for all resources to be released
	FlushRenderingCommands();

	Settings->Flags.InitStatus = 0;

	vrapi_Shutdown();

	UE_LOG(LogHMD, Log, TEXT("GearVR shutdown."));
}

void FGearVR::ApplicationPauseDelegate()
{
	FPlatformMisc::LowLevelOutputDebugString(TEXT("+++++++ GEARVR APP PAUSE ++++++"));
	OCFlags.bResumed = false;

	LeaveVRMode();
}

void FGearVR::ApplicationResumeDelegate()
{
	FPlatformMisc::LowLevelOutputDebugString(TEXT("+++++++ GEARVR APP RESUME ++++++"));
	OCFlags.bResumed = true;
}

void FGearVR::UpdateHmdRenderInfo()
{
}

void FGearVR::UpdateStereoRenderingParams()
{
	FSettings* CurrentSettings = GetSettings();

	if ((!CurrentSettings->IsStereoEnabled() && !CurrentSettings->Flags.bHeadTrackingEnforced))
	{
		return;
	}
	if (IsInitialized())
	{
		CurrentSettings->HmdToEyeViewOffset[0] = CurrentSettings->HmdToEyeViewOffset[1] = OVR::Vector3f(0,0,0);
		CurrentSettings->HmdToEyeViewOffset[0].x = -CurrentSettings->InterpupillaryDistance * 0.5f; // -X <=, +X => (OVR coord sys)
		CurrentSettings->HmdToEyeViewOffset[1].x = CurrentSettings->InterpupillaryDistance * 0.5f;  // -X <=, +X => (OVR coord sys)

		CurrentSettings->RenderTargetSize.X = HmdInfo.SuggestedEyeResolutionWidth * 2;
		CurrentSettings->RenderTargetSize.Y = HmdInfo.SuggestedEyeResolutionHeight;
		//FSceneRenderTargets::QuantizeBufferSize(CurrentSettings->RenderTargetSize.X, CurrentSettings->RenderTargetSize.Y);

		CurrentSettings->HFOVInRadians = FMath::DegreesToRadians(HmdInfo.SuggestedEyeFovDegreesX);
		CurrentSettings->VFOVInRadians = FMath::DegreesToRadians(HmdInfo.SuggestedEyeFovDegreesY);

		const int32 RTSizeX = CurrentSettings->RenderTargetSize.X;
		const int32 RTSizeY = CurrentSettings->RenderTargetSize.Y;
		CurrentSettings->EyeRenderViewport[0] = FIntRect(1, 1, RTSizeX / 2 - 1, RTSizeY - 1);
		CurrentSettings->EyeRenderViewport[1] = FIntRect(RTSizeX / 2 + 1, 1, RTSizeX-1, RTSizeY-1);
	}

	Flags.bNeedUpdateStereoRenderingParams = false;
}

void FGearVR::LoadFromIni()
{
	FSettings* CurrentSettings = GetSettings();
	const TCHAR* GearVRSettings = TEXT("GearVR.Settings");
	bool v;
	float f;
	if (GConfig->GetBool(GearVRSettings, TEXT("bChromaAbCorrectionEnabled"), v, GEngineIni))
	{
		CurrentSettings->Flags.bChromaAbCorrectionEnabled = v;
	}
	if (GConfig->GetBool(GearVRSettings, TEXT("bDevSettingsEnabled"), v, GEngineIni))
	{
		CurrentSettings->Flags.bDevSettingsEnabled = v;
	}
	if (GConfig->GetFloat(GearVRSettings, TEXT("MotionPrediction"), f, GEngineIni))
	{
		CurrentSettings->MotionPredictionInSeconds = f;
	}
	if (GConfig->GetBool(GearVRSettings, TEXT("bOverrideIPD"), v, GEngineIni))
	{
		CurrentSettings->Flags.bOverrideIPD = v;
		if (CurrentSettings->Flags.bOverrideIPD)
		{
			if (GConfig->GetFloat(GearVRSettings, TEXT("IPD"), f, GEngineIni))
			{
				SetInterpupillaryDistance(f);
			}
		}
	}
	if (GConfig->GetBool(GearVRSettings, TEXT("bOverrideStereo"), v, GEngineIni))
	{
		CurrentSettings->Flags.bOverrideStereo = v;
		if (CurrentSettings->Flags.bOverrideStereo)
		{
			if (GConfig->GetFloat(GearVRSettings, TEXT("HFOV"), f, GEngineIni))
			{
				CurrentSettings->HFOVInRadians = f;
			}
			if (GConfig->GetFloat(GearVRSettings, TEXT("VFOV"), f, GEngineIni))
			{
				CurrentSettings->VFOVInRadians = f;
			}
		}
	}
	if (GConfig->GetBool(GearVRSettings, TEXT("bOverrideVSync"), v, GEngineIni))
	{
		CurrentSettings->Flags.bOverrideVSync = v;
		if (GConfig->GetBool(GearVRSettings, TEXT("bVSync"), v, GEngineIni))
		{
			CurrentSettings->Flags.bVSync = v;
		}
	}
	if (GConfig->GetBool(GearVRSettings, TEXT("bOverrideScreenPercentage"), v, GEngineIni))
	{
		CurrentSettings->Flags.bOverrideScreenPercentage = v;
		if (GConfig->GetFloat(GearVRSettings, TEXT("ScreenPercentage"), f, GEngineIni))
		{
			CurrentSettings->ScreenPercentage = f;
		}
	}
	if (GConfig->GetBool(GearVRSettings, TEXT("bAllowFinishCurrentFrame"), v, GEngineIni))
	{
		CurrentSettings->Flags.bAllowFinishCurrentFrame = v;
	}
	if (GConfig->GetBool(GearVRSettings, TEXT("bUpdateOnRT"), v, GEngineIni))
	{
		CurrentSettings->Flags.bUpdateOnRT = v;
	}
	if (GConfig->GetFloat(GearVRSettings, TEXT("FarClippingPlane"), f, GEngineIni))
	{
		CurrentSettings->FarClippingPlane = f;
	}
	if (GConfig->GetFloat(GearVRSettings, TEXT("NearClippingPlane"), f, GEngineIni))
	{
		CurrentSettings->NearClippingPlane = f;
	}
}

void FGearVR::GetOrthoProjection(int32 RTWidth, int32 RTHeight, float OrthoDistance, FMatrix OrthoProjection[2]) const
{
	OrthoProjection[0] = OrthoProjection[1] = FMatrix::Identity;
	
	// note, this is not right way, this is hack. The proper orthoproj matrix should be used. @TODO!
	OrthoProjection[1] = FTranslationMatrix(FVector(OrthoProjection[1].M[0][3] * RTWidth * .25 + RTWidth * .5, 0 , 0));
}

void FGearVR::StartOVRGlobalMenu()
{
	check(IsInRenderingThread());

	if (pGearVRBridge)
	{
		ovr_StartSystemActivity(&pGearVRBridge->JavaRT, PUI_GLOBAL_MENU, NULL);
	}
}

void FGearVR::StartOVRQuitMenu()
{
	check(IsInRenderingThread());

	if (pGearVRBridge)
	{
		ovr_StartSystemActivity(&pGearVRBridge->JavaRT, PUI_CONFIRM_QUIT, NULL);
	}
}

void FGearVR::UpdateViewport(bool bUseSeparateRenderTarget, const FViewport& InViewport, SViewport* ViewportWidget)
{
	check(IsInGameThread());

	FRHIViewport* const ViewportRHI = InViewport.GetViewportRHI().GetReference();

	if (!IsStereoEnabled() || !pGearVRBridge)
	{
		if (!bUseSeparateRenderTarget || !pGearVRBridge)
		{
			ViewportRHI->SetCustomPresent(nullptr);
		}
		return;
	}

	check(pGearVRBridge);

	pGearVRBridge->UpdateViewport(InViewport, ViewportRHI);
}

void FGearVR::DrawDebug(UCanvas* Canvas)
{
#if !UE_BUILD_SHIPPING
	check(IsInGameThread());
	const auto frame = GetCurrentFrame();
	if (frame)
	{
		if (frame->Settings->Flags.bDrawTrackingCameraFrustum)
		{
			DrawDebugTrackingCameraFrustum(GWorld, Canvas->SceneView->ViewRotation, Canvas->SceneView->ViewLocation);
		}
		DrawSeaOfCubes(GWorld, Canvas->SceneView->ViewLocation);
	}

#endif // #if !UE_BUILD_SHIPPING
}

float FGearVR::GetBatteryLevel() const
{
	return FAndroidMisc::GetBatteryState().Level;
}

float FGearVR::GetTemperatureInCelsius() const
{
	return FAndroidMisc::GetBatteryState().Temperature;
}

bool FGearVR::AreHeadPhonesPluggedIn() const
{
	return FAndroidMisc::AreHeadPhonesPluggedIn();
}

bool FGearVR::IsPowerLevelStateThrottled() const
{
	return ovr_GetPowerLevelStateThrottled();
}

bool FGearVR::IsPowerLevelStateMinimum() const
{
	return ovr_GetPowerLevelStateMinimum();
}

void FGearVR::SetCPUAndGPULevels(int32 CPULevel, int32 GPULevel)
{
	check(IsInGameThread());
	UE_LOG(LogHMD, Log, TEXT("SetCPUAndGPULevels: Adjusting levels to CPU=%d - GPU=%d"), CPULevel, GPULevel);

	FSettings* CurrentSettings = GetSettings();
	CurrentSettings->CpuLevel = CPULevel;
	CurrentSettings->GpuLevel = GPULevel;
}

bool FGearVR::HasValidOvrMobile() const
{
	return pGearVRBridge->OvrMobile != nullptr;
}

//////////////////////////////////////////////////////////////////////////
FViewExtension::FViewExtension(FHeadMountedDisplay* InDelegate)
	: FHMDViewExtension(InDelegate)
	, ShowFlags(ESFIM_All0)
	, bFrameBegun(false)
{
	auto GearVRHMD = static_cast<FGearVR*>(InDelegate);
	pPresentBridge = GearVRHMD->pGearVRBridge;
}

//////////////////////////////////////////////////////////////////////////

#endif //GEARVR_SUPPORTED_PLATFORMS

void FGearVRPlugin::StartOVRGlobalMenu() const 
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInGameThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		OculusHMD->StartOVRGlobalMenu();
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
}

void FGearVRPlugin::StartOVRQuitMenu() const 
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInGameThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		OculusHMD->StartOVRQuitMenu();
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
}

void FGearVRPlugin::SetCPUAndGPULevels(int32 CPULevel, int32 GPULevel) const
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInGameThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		OculusHMD->SetCPUAndGPULevels(CPULevel, GPULevel);
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
}

bool FGearVRPlugin::IsPowerLevelStateMinimum() const
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInGameThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		return OculusHMD->IsPowerLevelStateMinimum();
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
	return false;
}

bool FGearVRPlugin::IsPowerLevelStateThrottled() const
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInGameThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		return OculusHMD->IsPowerLevelStateThrottled();
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
	return false;
}

float FGearVRPlugin::GetTemperatureInCelsius() const
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInGameThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		return OculusHMD->GetTemperatureInCelsius();
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
	return 0.f;
}

float FGearVRPlugin::GetBatteryLevel() const
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInGameThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		return OculusHMD->GetBatteryLevel();
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
	return 0.f;
}

bool FGearVRPlugin::AreHeadPhonesPluggedIn() const
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInGameThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		return OculusHMD->AreHeadPhonesPluggedIn();
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
	return false;
}

void FGearVRPlugin::SetLoadingIconTexture(FTextureRHIRef InTexture)
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInGameThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		OculusHMD->SetLoadingIconTexture(InTexture);
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
}

void FGearVRPlugin::SetLoadingIconMode(bool bActiveLoadingIcon)
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInGameThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		OculusHMD->SetLoadingIconMode(bActiveLoadingIcon);
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
}

void FGearVRPlugin::RenderLoadingIcon_RenderThread()
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInRenderingThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		OculusHMD->RenderLoadingIcon_RenderThread();
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
}

bool FGearVRPlugin::IsInLoadingIconMode() const
{
#if GEARVR_SUPPORTED_PLATFORMS
	check(IsInGameThread());
	IHeadMountedDisplay* HMD = GEngine->HMDDevice.Get();
	if (HMD && HMD->GetHMDDeviceType() == EHMDDeviceType::DT_GearVR)
	{
		FGearVR* OculusHMD = static_cast<FGearVR*>(HMD);

		return OculusHMD->IsInLoadingIconMode();
	}
#endif //GEARVR_SUPPORTED_PLATFORMS
	return false;
}

#if GEARVR_SUPPORTED_PLATFORMS

#include <HeadMountedDisplayCommon.cpp>

#endif //GEARVR_SUPPORTED_PLATFORMS

