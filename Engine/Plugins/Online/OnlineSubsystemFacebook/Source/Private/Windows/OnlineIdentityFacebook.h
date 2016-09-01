// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
 
#include "OnlineIdentityInterface.h"
#include "OnlineSubsystemFacebookPackage.h"

/**
 * Info associated with an user account generated by this online service
 */
class FUserOnlineAccountFacebook : 
	public FUserOnlineAccount,
	public FOnlineJsonSerializable
{
public:

	// FOnlineUser
	
	virtual TSharedRef<const FUniqueNetId> GetUserId() const override;
	virtual FString GetRealName() const override;
	virtual FString GetDisplayName(const FString& Platform = FString()) const override;
	virtual bool GetUserAttribute(const FString& AttrName, FString& OutAttrValue) const override;
	virtual bool SetUserAttribute(const FString& AttrName, const FString& AttrValue) override;

	// FUserOnlineAccount

	virtual FString GetAccessToken() const override;
	virtual bool GetAuthAttribute(const FString& AttrName, FString& OutAttrValue) const override;

	// FUserOnlineAccountFacebook

	FUserOnlineAccountFacebook(const FString& InUserId=TEXT(""), const FString& InAuthTicket=TEXT("")) 
		: UserIdPtr(new FUniqueNetIdString(InUserId))
		, UserId(InUserId)
		, AuthTicket(InAuthTicket)
	{ }

	virtual ~FUserOnlineAccountFacebook()
	{
	}

	/** User Id represented as a FUniqueNetId */
	TSharedRef<const FUniqueNetId> UserIdPtr;
	/** Id associated with the user account provided by the online service during registration */
	FString UserId;
	/** Public user name */
	FString UserName;
	/** Real name */
	FString RealName;
	/** male or female */
	FString Gender;
	/** eg. en_US */
	FString Locale;
	/** Ticket which is provided to user once authenticated by the online service */
	FString AuthTicket;

	// FJsonSerializable

	BEGIN_ONLINE_JSON_SERIALIZER
		ONLINE_JSON_SERIALIZE("id", UserId);
		ONLINE_JSON_SERIALIZE("username", UserName);
		ONLINE_JSON_SERIALIZE("name", RealName);
		ONLINE_JSON_SERIALIZE("gender", Gender);
		ONLINE_JSON_SERIALIZE("locale", Locale);
	END_ONLINE_JSON_SERIALIZER
};
/** Mapping from user id to his internal online account info (only one per user) */
typedef TMap<FString, TSharedRef<FUserOnlineAccountFacebook> > FUserOnlineAccountFacebookMap;

/**
 * Facebook service implementation of the online identity interface
 */
class FOnlineIdentityFacebook :
	public IOnlineIdentity
{
	/** The endpoint at Facebook we are supposed to hit for auth */
	FString LoginUrl;
	/** The redirect url for Facebook to redirect to upon completion. Note: this is configured at Facebook too */
	FString LoginRedirectUrl;
	/** The client id given to us by Facebook */
	FString ClientId;

	/** Users that have been registered/authenticated */
	FUserOnlineAccountFacebookMap UserAccounts;
	/** Ids mapped to locally registered users */
	TMap<int32, TSharedPtr<const FUniqueNetId> > UserIds;

	/** The amount of elapsed time since the last check */
	float LastCheckElapsedTime;
	/** Used to determine if we've timed out waiting for the response */
	float TotalCheckElapsedTime;
	/** Config value used to set our timeout period */
	float MaxCheckElapsedTime;
	/** Whether we have a registration in flight or not */
	bool bHasLoginOutstanding;
	/** A value used to verify our response came from our server */
	FString State;
	/** index of local user being registered */
	int32 LocalUserNumPendingLogin;

public:
	// IOnlineIdentity
	
	virtual bool Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials) override;
	virtual bool Logout(int32 LocalUserNum) override;
	virtual bool AutoLogin(int32 LocalUserNum) override;
	virtual TSharedPtr<FUserOnlineAccount> GetUserAccount(const FUniqueNetId& UserId) const override;
	virtual TArray<TSharedPtr<FUserOnlineAccount> > GetAllUserAccounts() const override;
	virtual TSharedPtr<const FUniqueNetId> GetUniquePlayerId(int32 LocalUserNum) const override;
	virtual TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(uint8* Bytes, int32 Size) override;
	virtual TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(const FString& Str) override;
	virtual ELoginStatus::Type GetLoginStatus(int32 LocalUserNum) const override;
	virtual ELoginStatus::Type GetLoginStatus(const FUniqueNetId& UserId) const override;
	virtual FString GetPlayerNickname(int32 LocalUserNum) const override;
	virtual FString GetPlayerNickname(const FUniqueNetId& UserId) const override;
	virtual FString GetAuthToken(int32 LocalUserNum) const override;
	virtual void GetUserPrivilege(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, const FOnGetUserPrivilegeCompleteDelegate& Delegate) override;
	virtual FPlatformUserId GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) override;
	virtual FString GetAuthType() const override;

	// FOnlineIdentityFacebook

	FOnlineIdentityFacebook();

	/**
	 * Destructor
	 */
	virtual ~FOnlineIdentityFacebook()
	{
	}

	/**
	 * Used to do any time based processing of tasks
	 *
	 * @param DeltaTime the amount of time that has elapsed since the last tick
	 */
	void Tick(float DeltaTime);

private:

	/**
	 * Ticks the registration process handling timeouts, etc.
	 *
	 * @param DeltaTime the amount of time that has elapsed since last tick
	 */
	void TickLogin(float DeltaTime);

	/**
	 * Parses the results into a user account entry
	 *
	 * @param Results the string returned by the login process
	 * @param Account the account structure to fill in
	 *
	 * @return true if it parsed correctly, false otherwise
	 */
	bool ParseLoginResults(const FString& Results, FUserOnlineAccountFacebook& Account);

	/**
	 * Delegate called when a user /me request from facebook is complete
	 */
	void MeUser_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);

	/** Info used to send request to register a user */
	struct FPendingLoginUser
	{
		FPendingLoginUser(
			int32 InLocalUserNum=0, 
			const FString& InAccessToken=FString()
			) 
			: LocalUserNum(InLocalUserNum)
			, AccessToken(InAccessToken)
		{

		}
		/** local index of user being registered */
		int32 LocalUserNum;
		/** Access token being used to login to Facebook */
		FString AccessToken;
	};
	/** List of pending Http requests for user registration */
	TMap<class IHttpRequest*, FPendingLoginUser> LoginUserRequests;
};

typedef TSharedPtr<FOnlineIdentityFacebook, ESPMode::ThreadSafe> FOnlineIdentityFacebookPtr;