// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GameSession.cpp: GameSession code.
=============================================================================*/

#include "EnginePrivate.h"
#include "Net/UnrealNetwork.h"
#include "OnlineSubsystemUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/GameMode.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameSession, Log, All);

static TAutoConsoleVariable<int32> CVarMaxPlayersOverride( TEXT( "net.MaxPlayersOverride" ), 0, TEXT( "If greater than 0, will override the standard max players count. Useful for testing full servers." ) );

/** 
 * Returns the player controller associated with this net id
 * @param PlayerNetId the id to search for
 * @return the player controller if found, otherwise NULL
 */
APlayerController* GetPlayerControllerFromNetId(UWorld* World, const FUniqueNetId& PlayerNetId)
{
	if (PlayerNetId.IsValid())
	{
		// Iterate through the controller list looking for the net id
		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			APlayerController* PlayerController = *Iterator;
			// Determine if this is a player with replication
			if (PlayerController->PlayerState != NULL && PlayerController->PlayerState->UniqueId.IsValid())
			{
				// If the ids match, then this is the right player.
				if (*PlayerController->PlayerState->UniqueId == PlayerNetId)
				{
					return PlayerController;
				}
			}
		}
	}

	return nullptr;
}

AGameSession::AGameSession(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	MaxPartySize(INDEX_NONE)
{
}

void AGameSession::HandleMatchIsWaitingToStart()
{
}

void AGameSession::HandleMatchHasStarted()
{
	UWorld* World = GetWorld();
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid() && SessionInt->GetNamedSession(SessionName) != nullptr)
	{
		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			APlayerController* PlayerController = *Iterator;
			if (!PlayerController->IsLocalController())
			{
				PlayerController->ClientStartOnlineSession();
			}
		}

		StartSessionCompleteHandle = SessionInt->AddOnStartSessionCompleteDelegate_Handle(FOnStartSessionCompleteDelegate::CreateUObject(this, &AGameSession::OnStartSessionComplete));
		SessionInt->StartSession(SessionName);
	}

	if (STATS && !UE_BUILD_SHIPPING)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("MatchAutoStatCapture")))
		{
			UE_LOG(LogGameSession, Log, TEXT("Match has started - begin automatic stat capture"));
			GEngine->Exec(GetWorld(), TEXT("stat startfile"));
		}
	}
}

void AGameSession::OnStartSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	UE_LOG(LogGameSession, Verbose, TEXT("OnStartSessionComplete %s bSuccess: %d"), *InSessionName.ToString(), bWasSuccessful);
	UWorld* World = GetWorld();
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid())
	{
		SessionInt->ClearOnStartSessionCompleteDelegate_Handle(StartSessionCompleteHandle);
	}
}

void AGameSession::HandleMatchHasEnded()
{
	if (STATS && !UE_BUILD_SHIPPING)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("MatchAutoStatCapture")))
		{
			UE_LOG(LogGameSession, Log, TEXT("Match has ended - end automatic stat capture"));
			GEngine->Exec(GetWorld(), TEXT("stat stopfile"));
		}
	}

	UWorld* World = GetWorld();
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid())
	{
		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			APlayerController* PlayerController = *Iterator;
			if (!PlayerController->IsLocalController())
			{
				PlayerController->ClientEndOnlineSession();
			}
		}

		EndSessionCompleteHandle = SessionInt->AddOnEndSessionCompleteDelegate_Handle(FOnEndSessionCompleteDelegate::CreateUObject(this, &AGameSession::OnEndSessionComplete));
		SessionInt->EndSession(SessionName);
	}
}

void AGameSession::OnEndSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	UE_LOG(LogGameSession, Verbose, TEXT("OnEndSessionComplete %s bSuccess: %d"), *InSessionName.ToString(), bWasSuccessful);
	UWorld* World = GetWorld();
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid())
	{
		SessionInt->ClearOnEndSessionCompleteDelegate_Handle(EndSessionCompleteHandle);
	}
}

bool AGameSession::HandleStartMatchRequest()
{
	return false;
}

void AGameSession::InitOptions( const FString& Options )
{
	UWorld* const World = GetWorld();
	check(World);
	AGameMode* const GameMode = World ? World->GetAuthGameMode() : nullptr;

	MaxPlayers = UGameplayStatics::GetIntOption( Options, TEXT("MaxPlayers"), MaxPlayers );
	MaxSpectators = UGameplayStatics::GetIntOption( Options, TEXT("MaxSpectators"), MaxSpectators );
	
	if (GameMode)
	{
		APlayerState const* const DefaultPlayerState = GetDefault<APlayerState>(GameMode->PlayerStateClass);
		if (DefaultPlayerState)
		{
			SessionName = DefaultPlayerState->SessionName;
		}
		else
		{
			UE_LOG(LogGameSession, Error, TEXT("Player State class is invalid for game mode: %s!"), *GameMode->GetName());
		}
	}
}

bool AGameSession::ProcessAutoLogin()
{
	UWorld* World = GetWorld();
	IOnlineIdentityPtr IdentityInt = Online::GetIdentityInterface(World);
	if (IdentityInt.IsValid())
	{
		OnLoginCompleteDelegateHandle = IdentityInt->AddOnLoginCompleteDelegate_Handle(0, FOnLoginCompleteDelegate::CreateUObject(this, &AGameSession::OnLoginComplete));
		if (!IdentityInt->AutoLogin(0))
		{
			// Not waiting for async login
			return false;
		}

		return true;
	}

	// Not waiting for async login
	return false;
}

void AGameSession::OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
	UWorld* World = GetWorld();
	IOnlineIdentityPtr IdentityInt = Online::GetIdentityInterface(World);
	if (IdentityInt.IsValid())
	{
		IdentityInt->ClearOnLoginCompleteDelegate_Handle(0, OnLoginCompleteDelegateHandle);
		if (IdentityInt->GetLoginStatus(0) == ELoginStatus::LoggedIn)
		{
			RegisterServer();
		}
		else
		{
			RegisterServerFailed();
		}
	}
}

void AGameSession::RegisterServer()
{
}

void AGameSession::RegisterServerFailed()
{
	UE_LOG(LogGameSession, Warning, TEXT("Autologin attempt failed, unable to register server!"));
}

FString AGameSession::ApproveLogin(const FString& Options)
{
	UWorld* const World = GetWorld();
	check(World);

	AGameMode* const GameMode = World->GetAuthGameMode();
	check(GameMode);

	int32 SpectatorOnly = 0;
	SpectatorOnly = UGameplayStatics::GetIntOption(Options, TEXT("SpectatorOnly"), SpectatorOnly);

	if (AtCapacity(SpectatorOnly == 1))
	{
		return TEXT( "Server full." );
	}

	int32 SplitscreenCount = 0;
	SplitscreenCount = UGameplayStatics::GetIntOption(Options, TEXT("SplitscreenCount"), SplitscreenCount);

	if (SplitscreenCount > MaxSplitscreensPerConnection)
	{
		UE_LOG(LogGameSession, Warning, TEXT("ApproveLogin: A maximum of %i splitscreen players are allowed"), MaxSplitscreensPerConnection);
		return TEXT("Maximum splitscreen players");
	}

	return TEXT("");
}

void AGameSession::PostLogin(APlayerController* NewPlayer)
{
}

int32 AGameSession::GetNextPlayerID()
{
	// Start at 256, because 255 is special (means all team for some UT Emote stuff)
	static int32 NextPlayerID = 256;
	return NextPlayerID++;
}

void AGameSession::RegisterPlayer(APlayerController* NewPlayer, const TSharedPtr<const FUniqueNetId>& UniqueId, bool bWasFromInvite)
{
	if (NewPlayer != NULL)
	{
		// Set the player's ID.
		check(NewPlayer->PlayerState);
		NewPlayer->PlayerState->PlayerId = GetNextPlayerID();
		NewPlayer->PlayerState->SetUniqueId(UniqueId);
		NewPlayer->PlayerState->RegisterPlayerWithSession(bWasFromInvite);
	}
}

void AGameSession::UnregisterPlayer(FName InSessionName, const FUniqueNetIdRepl& UniqueId)
{
	UWorld* World = GetWorld();
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid())
	{
		if (GetNetMode() != NM_Standalone &&
			UniqueId.IsValid() &&
			UniqueId->IsValid())
		{
			// Remove the player from the session
			SessionInt->UnregisterPlayer(InSessionName, *UniqueId);
		}
	}
}

void AGameSession::UnregisterPlayer(const APlayerController* ExitingPlayer)
{
	if (GetNetMode() != NM_Standalone &&
		ExitingPlayer != NULL &&
		ExitingPlayer->PlayerState &&
		ExitingPlayer->PlayerState->UniqueId.IsValid() &&
		ExitingPlayer->PlayerState->UniqueId->IsValid())
	{
		UnregisterPlayer(ExitingPlayer->PlayerState->SessionName, ExitingPlayer->PlayerState->UniqueId);
	}
}

bool AGameSession::AtCapacity(bool bSpectator)
{
	if ( GetNetMode() == NM_Standalone )
	{
		return false;
	}

	if ( bSpectator )
	{
		return ( (GetWorld()->GetAuthGameMode()->NumSpectators >= MaxSpectators)
		&& ((GetNetMode() != NM_ListenServer) || (GetWorld()->GetAuthGameMode()->NumPlayers > 0)) );
	}
	else
	{
		const int32 MaxPlayersToUse = CVarMaxPlayersOverride.GetValueOnGameThread() > 0 ? CVarMaxPlayersOverride.GetValueOnGameThread() : MaxPlayers;

		return ( (MaxPlayersToUse>0) && (GetWorld()->GetAuthGameMode()->GetNumPlayers() >= MaxPlayersToUse) );
	}
}

void AGameSession::NotifyLogout(FName InSessionName, const FUniqueNetIdRepl& UniqueId)
{
	// Unregister the player from the online layer
	UnregisterPlayer(InSessionName, UniqueId);
}

void AGameSession::NotifyLogout(const APlayerController* PC)
{
	// Unregister the player from the online layer
	UnregisterPlayer(PC);
}

void AGameSession::AddAdmin(APlayerController* AdminPlayer)
{
}

void AGameSession::RemoveAdmin(APlayerController* AdminPlayer)
{
}

bool AGameSession::KickPlayer(APlayerController* KickedPlayer, const FText& KickReason)
{
	// Do not kick logged admins
	if (KickedPlayer != NULL && Cast<UNetConnection>(KickedPlayer->Player) != NULL)
	{
		if (KickedPlayer->GetPawn() != NULL)
		{
			KickedPlayer->GetPawn()->Destroy();
		}

		KickedPlayer->ClientWasKicked(KickReason);

		if (KickedPlayer != NULL)
		{
			KickedPlayer->Destroy();
		}

		return true;
	}
	return false;
}

bool AGameSession::BanPlayer(class APlayerController* BannedPlayer, const FText& BanReason)
{
	return KickPlayer(BannedPlayer, BanReason);
}

void AGameSession::ReturnToMainMenuHost()
{
	FString RemoteReturnReason = NSLOCTEXT("NetworkErrors", "HostHasLeft", "Host has left the game.").ToString();
	FString LocalReturnReason(TEXT(""));

	APlayerController* Controller = NULL;
	FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator();
	for(; Iterator; ++Iterator)
	{
		Controller = *Iterator;
		if (Controller && !Controller->IsLocalPlayerController() && Controller->IsPrimaryPlayer())
		{
			// Clients
			Controller->ClientReturnToMainMenu(RemoteReturnReason);
		}
	}

	Iterator.Reset();
	for(; Iterator; ++Iterator)
	{
		Controller = *Iterator;
		if (Controller && Controller->IsLocalPlayerController() && Controller->IsPrimaryPlayer())
		{
			Controller->ClientReturnToMainMenu(LocalReturnReason);
			break;
		}
	}
}

bool AGameSession::TravelToSession(int32 ControllerId, FName InSessionName)
{
	UWorld* World = GetWorld();
	IOnlineSubsystem* OnlineSub = Online::GetSubsystem(World);
	if (OnlineSub)
	{
		FString URL;
		IOnlineSessionPtr SessionInt = OnlineSub->GetSessionInterface();
		if (SessionInt.IsValid() && SessionInt->GetResolvedConnectString(InSessionName, URL))
		{
			APlayerController* PC = UGameplayStatics::GetPlayerController(World, ControllerId);
			if (PC)
			{
				PC->ClientTravel(URL, TRAVEL_Absolute);
				return true;
			}
		}
		else
		{
			UE_LOG(LogGameSession, Warning, TEXT("Failed to resolve session connect string for %s"), *InSessionName.ToString());
		}
	}

	return false;
}

void AGameSession::PostSeamlessTravel()
{
}

void AGameSession::DumpSessionState()
{
	UE_LOG(LogGameSession, Log, TEXT("  MaxPlayers: %i"), MaxPlayers);
	UE_LOG(LogGameSession, Log, TEXT("  MaxSpectators: %i"), MaxSpectators);

	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(GetWorld());
	if (SessionInt.IsValid())
	{
		SessionInt->DumpSessionState();
	}
}

bool AGameSession::CanRestartGame()
{
	return true;
}

bool AGameSession::GetSessionJoinability(FName InSessionName, FJoinabilitySettings& OutSettings)
{
	UWorld* const World = GetWorld();
	check(World);

	bool bValidData = false;

	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid())
	{
		FOnlineSessionSettings* SessionSettings = SessionInt->GetSessionSettings(InSessionName);
		if (SessionSettings)
		{
			OutSettings.SessionName = InSessionName;
			OutSettings.bPublicSearchable = SessionSettings->bShouldAdvertise;
			OutSettings.bAllowInvites = SessionSettings->bAllowInvites;
			OutSettings.bJoinViaPresence = SessionSettings->bAllowJoinViaPresence;
			OutSettings.bJoinViaPresenceFriendsOnly = SessionSettings->bAllowJoinViaPresenceFriendsOnly;

			OutSettings.MaxPlayers = MaxPlayers;
			OutSettings.MaxPartySize = MaxPartySize;

			bValidData = true;
		}
	}

	return bValidData;
}

void AGameSession::UpdateSessionJoinability(FName InSessionName, bool bPublicSearchable, bool bAllowInvites, bool bJoinViaPresence, bool bJoinViaPresenceFriendsOnly)
{
	if (GetNetMode() != NM_Standalone)
	{
		IOnlineSessionPtr SessionInt = Online::GetSessionInterface(GetWorld());
		if (SessionInt.IsValid())
		{
			FOnlineSessionSettings* GameSettings = SessionInt->GetSessionSettings(InSessionName);
			if (GameSettings != NULL)
			{
				GameSettings->bShouldAdvertise = bPublicSearchable;
				GameSettings->bAllowInvites = bAllowInvites;
				GameSettings->bAllowJoinViaPresence = bJoinViaPresence && !bJoinViaPresenceFriendsOnly;
				GameSettings->bAllowJoinViaPresenceFriendsOnly = bJoinViaPresenceFriendsOnly;
				SessionInt->UpdateSession(InSessionName, *GameSettings, true);
			}
		}
	}
}
