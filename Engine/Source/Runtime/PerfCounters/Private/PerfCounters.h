// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PerfCountersModule.h"

class FSocket;

DECLARE_LOG_CATEGORY_EXTERN(LogPerfCounters, Log, All);

class FPerfCounters 
	: public FTickerObjectBase
	, public FSelfRegisteringExec
	, public IPerfCounters
{
public:

	FPerfCounters(const FString& InUniqueInstanceId);
	virtual ~FPerfCounters();

	/** Initializes this instance from JSON config. */
	bool Initialize();

	/** FTickerObjectBase */
	virtual bool Tick(float DeltaTime) override;

	//~ Begin Exec Interface
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar);
	//~ End Exec Interface

	//~ Begin IPerfCounters Interface
	const FString& GetInstanceName() const override { return UniqueInstanceId; }
	virtual double GetNumber(const FString& Name, double DefaultValue = 0.0) override;
	virtual void SetNumber(const FString& Name, double Value, uint32 Flags) override;
	virtual void SetString(const FString& Name, const FString& Value, uint32 Flags) override;
	virtual void SetJson(const FString& Name, const FProduceJsonCounterValue& InCallback, uint32 Flags) override;
	virtual FPerfCounterExecCommandCallback& OnPerfCounterExecCommand() override { return ExecCmdCallback; }
	virtual const TMap<FString, FJsonVariant>& GetAllCounters() override { return PerfCounterMap; }
	virtual FString GetAllCountersAsJson() override;
	virtual void ResetStatsForNextPeriod() override;
	//~ Begin IPerfCounters Interface end

private:
	
	/**
	 * Simple response structure for returning output to requestor
	 */
	struct FResponse
	{
		/** http header */
		FString Header;
		/** http body */
		FString Body;
		/** http response code */
		int32 Code;

		FResponse() :
			Code(0)
		{}
	};
	
	/**
	 * Simple connection structure for keeping track of incoming/active connections
	 */
	struct FPerfConnection
	{
		/** accepted external socket */
		FSocket* Connection;
		/** time connection has existed */
		float ElapsedTime;

		FPerfConnection() :
			Connection(nullptr),
			ElapsedTime(0.0f)
		{}

		FPerfConnection(FSocket* InConnection) :
			Connection(InConnection),
			ElapsedTime(0.0f)
		{}

		bool operator==(const FPerfConnection& Other) const
		{
			return Connection == Other.Connection;
		}
	};

	/** all active connections */
	TArray<FPerfConnection> Connections;

	/**
	 * Process the incoming request from an active socket
	 *
	 * @param Buffer data sent from the requestor
	 * @param BufferLen size of the data sent
	 * @param Response [out] response to be given to the requestor
	 *
	 * @return true if the response is valid, false otherwise
	 */
	bool ProcessRequest(uint8* Buffer, int32 BufferLen, FResponse& Response);

	/** Unique name of this instance */
	FString UniqueInstanceId;

	/** Map of all known performance counters */
	TMap<FString, FJsonVariant>  PerfCounterMap;

	/** Bound callback for script command execution */
	FPerfCounterExecCommandCallback ExecCmdCallback;

	/* Listen socket for outputting JSON on request */
	FSocket* Socket;
};

