// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "SlateBasics.h"
#include "AudioDevice.h"
#include "Scalability.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/GameEngine.h"

extern EWindowMode::Type GetWindowModeType(EWindowMode::Type WindowMode);

enum EGameUserSettingsVersion
{
	/** Version for user game settings. All settings will be wiped if the serialized version differs. */
	UE_GAMEUSERSETTINGS_VERSION = 5
};


UGameUserSettings::UGameUserSettings(const FObjectInitializer& ObjectInitializer)
:	Super(ObjectInitializer)
{
	SetToDefaults();
}

FIntPoint UGameUserSettings::GetScreenResolution() const
{
	return FIntPoint(ResolutionSizeX, ResolutionSizeY);
}

FIntPoint UGameUserSettings::GetLastConfirmedScreenResolution() const
{
	return FIntPoint(LastUserConfirmedResolutionSizeX, LastUserConfirmedResolutionSizeY);
}

void UGameUserSettings::SetScreenResolution( FIntPoint Resolution )
{
	ResolutionSizeX = Resolution.X;
	ResolutionSizeY = Resolution.Y;
}

EWindowMode::Type UGameUserSettings::GetFullscreenMode() const
{
	return EWindowMode::ConvertIntToWindowMode(FullscreenMode);
}

EWindowMode::Type UGameUserSettings::GetLastConfirmedFullscreenMode() const
{
	return EWindowMode::ConvertIntToWindowMode(LastConfirmedFullscreenMode);
}

void UGameUserSettings::SetFullscreenMode( EWindowMode::Type InFullscreenMode )
{
	switch ( InFullscreenMode )
	{
		case EWindowMode::Fullscreen:
			FullscreenMode = 0;
			break;
		case EWindowMode::WindowedFullscreen:
			FullscreenMode = 1;
			break;
		case EWindowMode::WindowedMirror:
			FullscreenMode = 3;
			break;
		case EWindowMode::Windowed:
		default:
			FullscreenMode = 2;
			break;
	}
}

void UGameUserSettings::SetVSyncEnabled( bool bEnable )
{
	bUseVSync = bEnable;
}

bool UGameUserSettings::IsVSyncEnabled() const
{
	return bUseVSync;
}

bool UGameUserSettings::IsScreenResolutionDirty() const
{
	bool bIsDirty = false;
	if ( GEngine && GEngine->GameViewport && GEngine->GameViewport->ViewportFrame )
	{
		bIsDirty = (ResolutionSizeX != GSystemResolution.ResX || ResolutionSizeY != GSystemResolution.ResY) ? true : false;
	}
	return bIsDirty;
}

bool UGameUserSettings::IsFullscreenModeDirty() const
{
	bool bIsDirty = false;
	if ( GEngine && GEngine->GameViewport && GEngine->GameViewport->ViewportFrame )
	{
		EWindowMode::Type WindowMode = GEngine->GameViewport->IsFullScreenViewport() ? EWindowMode::Fullscreen : EWindowMode::Windowed;
		EWindowMode::Type CurrentFullscreenMode = GetWindowModeType(WindowMode);
		EWindowMode::Type NewFullscreenMode = GetFullscreenMode();
		bIsDirty = (CurrentFullscreenMode != NewFullscreenMode) ? true : false;
	}
	return bIsDirty;
}

bool UGameUserSettings::IsVSyncDirty() const
{
	bool bIsDirty = false;
	if ( GEngine && GEngine->GameViewport && GEngine->GameViewport->ViewportFrame )
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VSync"));
		bIsDirty = (bUseVSync != (CVar->GetValueOnGameThread() != 0));
	}
	return bIsDirty;
}

bool UGameUserSettings::IsDirty() const
{
	return IsScreenResolutionDirty() || IsFullscreenModeDirty() || IsVSyncDirty();
}

void UGameUserSettings::ConfirmVideoMode()
{
	LastConfirmedFullscreenMode = FullscreenMode;
	LastUserConfirmedResolutionSizeX = ResolutionSizeX;
	LastUserConfirmedResolutionSizeY = ResolutionSizeY;
}

void UGameUserSettings::RevertVideoMode()
{
	FullscreenMode = LastConfirmedFullscreenMode;
	ResolutionSizeX = LastUserConfirmedResolutionSizeX;
	ResolutionSizeY = LastUserConfirmedResolutionSizeY;
}

void UGameUserSettings::SetToDefaults()
{
	ResolutionSizeX = LastUserConfirmedResolutionSizeX = GetDefaultResolution().X;
	ResolutionSizeY = LastUserConfirmedResolutionSizeY = GetDefaultResolution().Y;
	WindowPosX = GetDefaultWindowPosition().X;
	WindowPosY = GetDefaultWindowPosition().Y;
	FullscreenMode = GetDefaultWindowMode();
	FrameRateLimit = 0.0f;

	ScalabilityQuality.SetDefaults();
}

bool UGameUserSettings::IsVersionValid()
{
	return (Version == UE_GAMEUSERSETTINGS_VERSION);
}

void UGameUserSettings::UpdateVersion()
{
	Version = UE_GAMEUSERSETTINGS_VERSION;
}

void UGameUserSettings::ValidateSettings()
{
	// Should we wipe all user settings?
	if ( !IsVersionValid() )
	{
		// First try loading the settings, if they haven't been loaded before.
		LoadSettings(true);

		// If it still an old version, delete the user settings file and reload defaults.
		if ( !IsVersionValid() )
		{
			// Force reset if there aren't any default .ini settings.
			SetToDefaults();
			static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VSync"));
			SetVSyncEnabled( CVar->GetValueOnGameThread() != 0 );

			IFileManager::Get().Delete( *GGameUserSettingsIni );
			LoadSettings(true);
		}
	}

	if ( ResolutionSizeX <= 0 || ResolutionSizeY <= 0 )
	{
		SetScreenResolution(FIntPoint(GSystemResolution.ResX, GSystemResolution.ResY));

		// Set last confirmed video settings
		LastConfirmedFullscreenMode = FullscreenMode;
		LastUserConfirmedResolutionSizeX = ResolutionSizeX;
		LastUserConfirmedResolutionSizeY = ResolutionSizeY;
	}

	// The user settings have now been validated for the current version.
	UpdateVersion();
}

void UGameUserSettings::ApplyNonResolutionSettings()
{
	ValidateSettings();

	// Update vsync cvar
	{
		FString ConfigSection = TEXT("SystemSettings");
#if WITH_EDITOR
		if (GIsEditor)
		{
			ConfigSection = TEXT("SystemSettingsEditor");
		}
#endif
		int32 VSyncValue = 0;
		if (GConfig->GetInt(*ConfigSection, TEXT("r.Vsync"), VSyncValue, GEngineIni))
		{
			// VSync was already set by system settings. We are capable of setting it here.
		}
		else
		{
			static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
			CVar->Set(IsVSyncEnabled(), ECVF_SetByGameSetting);
		}
	}

	if (!IsRunningDedicatedServer())
	{
		// Update MaxFPS cvar
		static IConsoleVariable* MaxFPSCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
		if (ensure(MaxFPSCVar) && (FrameRateLimit >= 0.0f))
		{
			MaxFPSCVar->Set(FrameRateLimit, ECVF_SetByGameSetting);
		}
	}

	// in init those are loaded earlier, after that we apply consolevariables.ini
	if(GEngine->IsInitialized())
	{
		Scalability::SetQualityLevels(ScalabilityQuality);
	}

	IConsoleManager::Get().CallAllConsoleVariableSinks();
}

void UGameUserSettings::ApplyResolutionSettings(bool bCheckForCommandLineOverrides)
{
#if UE_SERVER
	return;
#endif

	ValidateSettings();

	EWindowMode::Type NewFullscreenMode = GetFullscreenMode();

	// Request a resolution change
	RequestResolutionChange(ResolutionSizeX, ResolutionSizeY, NewFullscreenMode, bCheckForCommandLineOverrides);
	IConsoleManager::Get().CallAllConsoleVariableSinks();
}

void UGameUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
	ApplyResolutionSettings(bCheckForCommandLineOverrides);
	ApplyNonResolutionSettings();

	SaveSettings();
	UE_LOG(LogConsoleResponse, Display, TEXT(""));
}

void UGameUserSettings::LoadSettings( bool bForceReload/*=false*/ )
{
	if ( bForceReload )
	{
		LoadConfigIni( bForceReload );
	}
	LoadConfig(GetClass(), *GGameUserSettingsIni);


	// Note: Scalability::LoadState() should not be needed as we already loaded the settings earlier (needed so the engine can startup with that before the game is initialized)
	ScalabilityQuality = Scalability::GetQualityLevels();

	// Allow override using command-line settings
	bool bDetectingResolution = ResolutionSizeX == 0 || ResolutionSizeY == 0;

	if (bDetectingResolution)
	{
		ConfirmVideoMode();
	}
}

void UGameUserSettings::RequestResolutionChange(int32 InResolutionX, int32 InResolutionY, EWindowMode::Type InWindowMode, bool bInDoOverrides /* = true */)
{
	if (bInDoOverrides)
	{
		UGameEngine::ConditionallyOverrideSettings(InResolutionX, InResolutionY, InWindowMode);
	}

	FSystemResolution::RequestResolutionChange(InResolutionX, InResolutionY, InWindowMode);
}

void UGameUserSettings::SaveSettings()
{
	// Save the Scalability state to the same ini file as it was loaded from in FEngineLoop::Preinit
	Scalability::SaveState(GIsEditor ? GEditorSettingsIni : GGameUserSettingsIni);
	SaveConfig(CPF_Config, *GGameUserSettingsIni);
}

void UGameUserSettings::LoadConfigIni( bool bForceReload/*=false*/ )
{
	// Load .ini, allowing merging
	FConfigCacheIni::LoadGlobalIniFile(GGameUserSettingsIni, TEXT("GameUserSettings"), NULL, bForceReload);
}

void UGameUserSettings::PreloadResolutionSettings()
{
	// Note: This preloads resolution settings without loading the user settings object.  
	// When changing this code care must be taken to ensure the window starts at the same resolution as the in game resolution
	LoadConfigIni();

	FString ScriptEngineCategory = TEXT("/Script/Engine.Engine");
	FString GameUserSettingsCategory = TEXT("/Script/Engine.GameUserSettings");

	GConfig->GetString(*ScriptEngineCategory, TEXT("GameUserSettingsClassName"), GameUserSettingsCategory, GEngineIni);

	int32 ResolutionX = GetDefaultResolution().X; 
	int32 ResolutionY = GetDefaultResolution().Y;
	EWindowMode::Type WindowMode = GetDefaultWindowMode();
	bool bUseDesktopResolution = false;

	int32 Version=0;
	if( GConfig->GetInt(*GameUserSettingsCategory, TEXT("Version"), Version, GGameUserSettingsIni ) && Version == UE_GAMEUSERSETTINGS_VERSION )
	{
		GConfig->GetBool(*GameUserSettingsCategory, TEXT("bUseDesktopResolution"), bUseDesktopResolution, GGameUserSettingsIni );

		int32 WindowModeInt = (int32)WindowMode;
		GConfig->GetInt(*GameUserSettingsCategory, TEXT("FullscreenMode"), WindowModeInt, GGameUserSettingsIni);
		WindowMode = EWindowMode::ConvertIntToWindowMode(WindowModeInt);

		GConfig->GetInt(*GameUserSettingsCategory, TEXT("ResolutionSizeX"), ResolutionX, GGameUserSettingsIni);
		GConfig->GetInt(*GameUserSettingsCategory, TEXT("ResolutionSizeY"), ResolutionY, GGameUserSettingsIni);

#if PLATFORM_DESKTOP
		if (bUseDesktopResolution && ResolutionX == 0 && ResolutionY == 0 && WindowMode != EWindowMode::Windowed)
		{
			// Grab display metrics so we can get the primary display output size.
			FDisplayMetrics DisplayMetrics;
			FDisplayMetrics::GetDisplayMetrics(DisplayMetrics);

			ResolutionX = DisplayMetrics.PrimaryDisplayWidth;
			ResolutionY = DisplayMetrics.PrimaryDisplayHeight;
		}
#endif
	}

	RequestResolutionChange(ResolutionX, ResolutionY, WindowMode);

	IConsoleManager::Get().CallAllConsoleVariableSinks();
}

FIntPoint UGameUserSettings::GetDefaultResolution()
{
	return FIntPoint::ZeroValue;
}

FIntPoint UGameUserSettings::GetDefaultWindowPosition()
{
	return FIntPoint(-1,-1);
}

EWindowMode::Type UGameUserSettings::GetDefaultWindowMode()
{
	return EWindowMode::Windowed;
}

void UGameUserSettings::ResetToCurrentSettings()
{
	if ( GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow().IsValid() )
	{
		//handle the fullscreen setting
		SetFullscreenMode(GetWindowModeType(GEngine->GameViewport->GetWindow()->GetWindowMode()));

		//set the current resolution
		SetScreenResolution(FIntPoint(GSystemResolution.ResX, GSystemResolution.ResY));

		// Set the current VSync state
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VSync"));
		SetVSyncEnabled( CVar->GetValueOnGameThread() != 0 );

		// Reset to confirmed settings
		FullscreenMode = LastConfirmedFullscreenMode;
		ResolutionSizeX = LastUserConfirmedResolutionSizeX;
		ResolutionSizeY = LastUserConfirmedResolutionSizeY;

		// Reset the quality settings to the current levels
		ScalabilityQuality = Scalability::GetQualityLevels();
	}
}

void UGameUserSettings::SetWindowPosition(int32 WinX, int32 WinY)
{
	WindowPosX = WinX;
	WindowPosY = WinY;
}

FIntPoint UGameUserSettings::GetWindowPosition()
{
	return FIntPoint(WindowPosX, WindowPosY);
}

void UGameUserSettings::SetBenchmarkFallbackValues()
{
	ScalabilityQuality.SetBenchmarkFallback();
}

void UGameUserSettings::SetAudioQualityLevel(int32 QualityLevel)
{
	AudioQualityLevel = QualityLevel;
}

void UGameUserSettings::SetFrameRateLimit(float NewLimit)
{
	FrameRateLimit = NewLimit;
}

float UGameUserSettings::GetFrameRateLimit() const
{
	return FrameRateLimit;
}

void UGameUserSettings::SetOverallScalabilityLevel(int32 Value)
{
	Value = FMath::Clamp(Value, 0, 3);
	ScalabilityQuality.SetFromSingleQualityLevel(Value);
}

int32 UGameUserSettings::GetOverallScalabilityLevel() const
{
	return ScalabilityQuality.GetSingleQualityLevel();
}

void UGameUserSettings::GetResolutionScaleInformation(float& CurrentScaleNormalized, int32& CurrentScaleValue, int32& MinScaleValue, int32& MaxScaleValue) const
{
	CurrentScaleValue = ScalabilityQuality.ResolutionQuality;
	MinScaleValue = Scalability::MinResolutionScale;
	MaxScaleValue = Scalability::MaxResolutionScale;
	CurrentScaleNormalized = ((float)CurrentScaleValue - (float)MinScaleValue) / (float)(MaxScaleValue - MinScaleValue);
}

void UGameUserSettings::SetResolutionScaleValue(int32 NewScaleValue)
{
	ScalabilityQuality.ResolutionQuality = FMath::Clamp(NewScaleValue, Scalability::MinResolutionScale, Scalability::MaxResolutionScale);
}

void UGameUserSettings::SetResolutionScaleNormalized(float NewScaleNormalized)
{
	const int32 RemappedValue = (int32)FMath::Lerp((float)Scalability::MinResolutionScale, (float)Scalability::MaxResolutionScale, NewScaleNormalized);
	SetResolutionScaleValue(RemappedValue);
}

void UGameUserSettings::SetViewDistanceQuality(int32 Value)
{
	ScalabilityQuality.ViewDistanceQuality = FMath::Clamp(Value, 0, 3);
}

int32 UGameUserSettings::GetViewDistanceQuality() const
{
	return ScalabilityQuality.ViewDistanceQuality;
}

void UGameUserSettings::SetShadowQuality(int32 Value)
{
	ScalabilityQuality.ShadowQuality = FMath::Clamp(Value, 0, 3);
}

int32 UGameUserSettings::GetShadowQuality() const
{
	return ScalabilityQuality.ShadowQuality;
}

void UGameUserSettings::SetAntiAliasingQuality(int32 Value)
{
	ScalabilityQuality.AntiAliasingQuality = FMath::Clamp(Value, 0, 3);
}

int32 UGameUserSettings::GetAntiAliasingQuality() const
{
	return ScalabilityQuality.AntiAliasingQuality;
}

void UGameUserSettings::SetTextureQuality(int32 Value)
{
	ScalabilityQuality.TextureQuality = FMath::Clamp(Value, 0, 3);
}

int32 UGameUserSettings::GetTextureQuality() const
{
	return ScalabilityQuality.TextureQuality;
}

void UGameUserSettings::SetVisualEffectQuality(int32 Value)
{
	ScalabilityQuality.EffectsQuality = FMath::Clamp(Value, 0, 3);
}

int32 UGameUserSettings::GetVisualEffectQuality() const
{
	return ScalabilityQuality.EffectsQuality;
}

void UGameUserSettings::SetPostProcessingQuality(int32 Value)
{
	ScalabilityQuality.PostProcessQuality = FMath::Clamp(Value, 0, 3);
}

int32 UGameUserSettings::GetPostProcessingQuality() const
{
	return ScalabilityQuality.PostProcessQuality;
}

UGameUserSettings* UGameUserSettings::GetGameUserSettings()
{
	return GEngine->GetGameUserSettings();
}
