// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "OnlineSubsystemTypes.h"
#include "OnlineDelegateMacros.h"

/**
 * Delegate called when the external UI is opened or closed
 *
 * @param bIsOpening state of the external UI
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnExternalUIChange, bool);
typedef FOnExternalUIChange::FDelegate FOnExternalUIChangeDelegate;

/**
 * Delegate executed when the external login UI has been closed.
 *
 * @param UniqueId The unique id of the user who signed in. Null if no user signed in.
 * @param ControllerIndex The controller index of the controller that activated the login UI.
 */
DECLARE_DELEGATE_TwoParams(FOnLoginUIClosedDelegate, TSharedPtr<const FUniqueNetId>, const int);

/**
 * Delegate executed when the web url UI has been closed
 *
 * @param FinalUrl the url that was used as the final redirect before closing
 */
DECLARE_DELEGATE_OneParam(FOnShowWebUrlClosedDelegate, const FString& /*FinalUrl*/);

/** 
 * Delegate executed when the user profile UI has been closed. 
 */
DECLARE_DELEGATE(FOnProfileUIClosedDelegate);

/** Parameters used to show a web UI */
struct FShowWebUrlParams
{
	/** presented without a frame if embedded enabled */
	bool bEmbedded;
	/** Show the built in close button */
	bool bShowCloseButton;
	/** Show the built in background */
	bool bShowBackground;
	/** x offset in pixels from top left */
	int32 OffsetX;
	/** y offset in pixels from top left */
	int32 OffsetY;
	/** x size in pixels */
	int32 SizeX;
	/** y size in pixels */
	int32 SizeY;
	/** if specified then restricted to only navigate within these domains */
	TArray<FString> AllowedDomains;
	/** portion of url for detecting callback.  Eg. "&code=", "redirect=", etc */
	FString CallbackPath;

	/**
	 * Constructor
	 */
	FShowWebUrlParams(bool InbEmbedded, int32 InOffsetX, int32 InOffsetY, int32 InSizeX, int32 InSizeY)
		: bEmbedded(InbEmbedded)
		, bShowCloseButton(false)
		, bShowBackground(false)
		, OffsetX(InOffsetX)
		, OffsetY(InOffsetY)
		, SizeX(InSizeX)
		, SizeY(InSizeY)
	{}

	/**
	 * Default Constructor
	 */
	FShowWebUrlParams()
		: bEmbedded(false)
		, bShowCloseButton(false)
		, bShowBackground(false)
		, OffsetX(0)
		, OffsetY(0)
		, SizeX(0)
		, SizeY(0)
	{}
};

/** 
 * Interface definition for the online services external UIs
 * Any online service that provides extra UI overlays will implement the relevant functions
 */
class IOnlineExternalUI
{
protected:
	IOnlineExternalUI() {};

public:
	virtual ~IOnlineExternalUI() {};

	/**
	 * Displays the UI that prompts the user for their login credentials. Each
	 * platform handles the authentication of the user's data.
	 *
	 * @param bShowOnlineOnly whether to only display online enabled profiles or not
	 * @param ControllerIndex The controller that prompted showing the login UI. If the platform supports it,
	 * it will pair the signed-in user with this controller.
	 * @param Delegate The delegate to execute when the user closes the login UI.
 	 *
	 * @return true if it was able to show the UI, false if it failed
	 */
	virtual bool ShowLoginUI(const int ControllerIndex, bool bShowOnlineOnly, const FOnLoginUIClosedDelegate& Delegate = FOnLoginUIClosedDelegate()) = 0;

	/**
	 * Displays the UI that shows a user's list of friends
	 *
	 * @param LocalUserNum the controller number of the associated user
	 *
	 * @return true if it was able to show the UI, false if it failed
	 */
	virtual bool ShowFriendsUI(int32 LocalUserNum) = 0;

	/**
	 *	Displays the UI that shows a user's list of friends to invite
	 *
	 * @param LocalUserNum the controller number of the associated user
	 *
	 * @return true if it was able to show the UI, false if it failed
	 */
	virtual bool ShowInviteUI(int32 LocalUserNum, FName SessionMame = GameSessionName) = 0;

	/**
	 *	Displays the UI that shows a user's list of achievements
	 *
	 * @param LocalUserNum the controller number of the associated user
	 *
	 * @return true if it was able to show the UI, false if it failed
	 */
	virtual bool ShowAchievementsUI(int32 LocalUserNum) = 0;

	/**
	 *	Displays the UI that shows a specific leaderboard
	 *
	 * @param LeaderboardName the name of the leaderboard to show
	 *
	 * @return true if it was able to show the UI, false if it failed
	 */
	virtual bool ShowLeaderboardUI(const FString& LeaderboardName) = 0;

	/**
	 * Displays a web page in the external UI
	 *
	 * @param WebURL fully formed web address (http://www.google.com)
	 *
	 * @return true if it was able to show the UI, false if it failed
	 */
	virtual bool ShowWebURL(const FString& Url, const FShowWebUrlParams& ShowParams, const FOnShowWebUrlClosedDelegate& Delegate = FOnShowWebUrlClosedDelegate()) = 0;

	/**
	 * Closes the currently active web external UI
	 *
	 * @return true if it was able to show the UI, false if it failed
	 */
	virtual bool CloseWebURL() = 0;

	/**
	 * Displays a user's profile card.
	 *
	 * @param Requestor The user requesting the profile.
	 * @param Requestee The user for whom to show the profile.
	 *
	 * @return true if it was able to show the UI, false if it failed
	 */
	virtual bool ShowProfileUI(const FUniqueNetId& Requestor, const FUniqueNetId& Requestee, const FOnProfileUIClosedDelegate& Delegate = FOnProfileUIClosedDelegate()) = 0;

	/**
	* Displays a system dialog to purchase user account upgrades.  e.g. PlaystationPlus, XboxLive GOLD, etc.
	*
	* @param UniqueID of the user to show the dialog for
	*
	* @return true if it was able to show the UI, false if it failed
	*/
	virtual bool ShowAccountUpgradeUI(const FUniqueNetId& UniqueId) = 0;

	/**
	 * Delegate called when the external UI is opened or closed
	 *
	 * @param bIsOpening state of the external UI
	 */
	DEFINE_ONLINE_DELEGATE_ONE_PARAM(OnExternalUIChange, bool);
};

typedef TSharedPtr<IOnlineExternalUI, ESPMode::ThreadSafe> IOnlineExternalUIPtr;

