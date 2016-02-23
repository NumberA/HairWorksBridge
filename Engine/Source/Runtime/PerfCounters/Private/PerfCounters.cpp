// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "PerfCounters.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Http.h"

#define JSON_ARRAY_NAME					TEXT("PerfCounters")
#define JSON_PERFCOUNTER_NAME			TEXT("Name")
#define JSON_PERFCOUNTER_SIZE_IN_BYTES	TEXT("SizeInBytes")

FPerfCounters::FPerfCounters(const FString& InUniqueInstanceId)
: UniqueInstanceId(InUniqueInstanceId)
, Socket(nullptr)
{
}

FPerfCounters::~FPerfCounters()
{
	if (Socket)
	{
		ISocketSubsystem* SocketSystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSystem)
		{
			SocketSystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
}

bool FPerfCounters::Initialize()
{
	// get the requested port from the command line (if specified)
	int32 StatsPort = -1;
	FParse::Value(FCommandLine::Get(), TEXT("statsPort="), StatsPort);
	if (StatsPort < 0)
	{
		UE_LOG(LogPerfCounters, Log, TEXT("FPerfCounters JSON socket disabled."));
		return true;
	}

	// get the socket subsystem
	ISocketSubsystem* SocketSystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSystem == nullptr)
	{
		UE_LOG(LogPerfCounters, Error, TEXT("FPerfCounters unable to get socket subsystem"));
		return false;
	}

	// make our listen socket
	Socket = SocketSystem->CreateSocket(NAME_Stream, TEXT("FPerfCounters"));
	if (Socket == nullptr)
	{
		UE_LOG(LogPerfCounters, Error, TEXT("FPerfCounters unable to allocate stream socket"));
		return false;
	}

	// make us non blocking
	Socket->SetNonBlocking(true);

	// create a localhost binding for the requested port
	TSharedRef<FInternetAddr> LocalhostAddr = SocketSystem->CreateInternetAddr(0x7f000001 /* 127.0.0.1 */, StatsPort);
	if (!Socket->Bind(*LocalhostAddr))
	{
		UE_LOG(LogPerfCounters, Error, TEXT("FPerfCounters unable to bind to %s"), *LocalhostAddr->ToString(true));
		return false;
	}
	StatsPort = Socket->GetPortNo();

	// log the port
	UE_LOG(LogPerfCounters, Display, TEXT("FPerfCounters listening on port %d"), StatsPort);

	// for now, jack this up so we can send in one go
	int32 NewSize;
	Socket->SetSendBufferSize(512 * 1024, NewSize); // best effort 512k buffer to avoid not being able to send in one go

	// listen on the port
	if (!Socket->Listen(16))
	{
		UE_LOG(LogPerfCounters, Error, TEXT("FPerfCounters unable to listen on socket"));
		return false;
	}

	return true;
}

FString FPerfCounters::GetAllCountersAsJson()
{
	FString JsonStr;
	TSharedRef< TJsonWriter<> > Json = TJsonWriterFactory<>::Create(&JsonStr);
	Json->WriteObjectStart();
	for (const auto& It : PerfCounterMap)
	{
		const FJsonVariant& JsonValue = It.Value;
		switch (JsonValue.Format)
		{
		case FJsonVariant::String:
			Json->WriteValue(It.Key, JsonValue.StringValue);
			break;
		case FJsonVariant::Number:
			Json->WriteValue(It.Key, JsonValue.NumberValue);
			break;
		case FJsonVariant::Callback:
			if (JsonValue.CallbackValue.IsBound())
			{
				Json->WriteIdentifierPrefix(It.Key);
				JsonValue.CallbackValue.Execute(Json);
			}
			else
			{
				// write an explicit null since the callback is unbound and the implication is this would have been an object
				Json->WriteNull(It.Key);
			}
			break;
		case FJsonVariant::Null:
		default:
			// don't write anything since wash may expect a scalar
			break;
		}
	}
	Json->WriteObjectEnd();
	Json->Close();
	return JsonStr;
}

void FPerfCounters::ResetStatsForNextPeriod()
{
	UE_LOG(LogPerfCounters, Verbose, TEXT("Clearing perf counters."));
	for (TMap<FString, FJsonVariant>::TIterator It(PerfCounterMap); It; ++It)
	{
		if (It.Value().Flags & IPerfCounters::Flags::Transient)
		{
			UE_LOG(LogPerfCounters, Verbose, TEXT("  Removed '%s'"), *It.Key());
			It.RemoveCurrent();
		}
	}
};


static bool SendAsUtf8(FSocket* Conn, const FString& Message)
{
	FTCHARToUTF8 ConvertToUtf8(*Message);
	int32 BytesSent = 0;
	return Conn->Send(reinterpret_cast<const uint8*>(ConvertToUtf8.Get()), ConvertToUtf8.Length(), BytesSent) && BytesSent == ConvertToUtf8.Length();
}

bool FPerfCounters::Tick(float DeltaTime)
{
	// if we didn't get a socket, don't tick
	if (Socket == nullptr)
	{
		return false;
	}

	// accept any connections
	static const FString PerfCounterRequest = TEXT("FPerfCounters Request");
	FSocket* IncomingConnection = Socket->Accept(PerfCounterRequest);
	if (IncomingConnection)
	{
		if (0)
		{
			TSharedRef<FInternetAddr> FromAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
			IncomingConnection->GetPeerAddress(*FromAddr);
			UE_LOG(LogPerfCounters, Log, TEXT("New connection from %s"), *FromAddr->ToString(true));
		}

		// make sure this is non-blocking
		IncomingConnection->SetNonBlocking(true);

		new (Connections) FPerfConnection(IncomingConnection);
	}

	TArray<FPerfConnection> ConnectionsToClose;
	for (FPerfConnection& Connection : Connections)
	{
		FSocket* ExistingSocket = Connection.Connection;
		if (ExistingSocket && ExistingSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::Zero()))
		{
			// read any data that's ready
			// NOTE: this is not a full HTTP implementation, just enough to be usable by curl
			uint8 Buffer[2 * 1024] = { 0 };
			int32 DataLen = 0;
			if (ExistingSocket->Recv(Buffer, sizeof(Buffer) - 1, DataLen, ESocketReceiveFlags::None))
			{
				FResponse Response;
				if (ProcessRequest(Buffer, DataLen, Response))
				{
					if (SendAsUtf8(ExistingSocket, Response.Header))
					{
						if (!SendAsUtf8(ExistingSocket, Response.Body))
						{
							UE_LOG(LogPerfCounters, Warning, TEXT("Unable to send full HTTP response body"));
						}
					}
					else
					{
						UE_LOG(LogPerfCounters, Warning, TEXT("Unable to send HTTP response header: %s"), *Response.Header);
					}
				}
			}
			else
			{
				UE_LOG(LogPerfCounters, Warning, TEXT("Unable to immediately receive request header"));
			}

			ConnectionsToClose.Add(Connection);
		}
		else if (Connection.ElapsedTime > 5.0f)
		{
			ConnectionsToClose.Add(Connection);
		}

		Connection.ElapsedTime += DeltaTime;
	}

	for (FPerfConnection& Connection : ConnectionsToClose)
	{
		Connections.RemoveSingleSwap(Connection);

		FSocket* ClosingSocket = Connection.Connection;
		if (ClosingSocket)
		{
			// close the socket (whether we processed or not)
			ClosingSocket->Close();
			ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClosingSocket);
			
			if (0)
			{
				UE_LOG(LogPerfCounters, Log, TEXT("Closed connection."));
			}
		}
	}

	// keep ticking
	return true;
}

bool FPerfCounters::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	// ignore everything that doesn't start with PerfCounters
	if (!FParse::Command(&Cmd, TEXT("perfcounters")))
	{
		return false;
	}

	if (FParse::Command(&Cmd, TEXT("clear")))
	{
		ResetStatsForNextPeriod();
		return true;
	}

	return false;
}

bool FPerfCounters::ProcessRequest(uint8* Buffer, int32 BufferLen, FResponse& Response)
{
	bool bSuccess = false;

	// scan the buffer for a line
	FUTF8ToTCHAR WideBuffer(reinterpret_cast<const ANSICHAR*>(Buffer));
	const TCHAR* BufferEnd = FCString::Strstr(WideBuffer.Get(), TEXT("\r\n"));
	if (BufferEnd != nullptr)
	{
		// crack into pieces
		FString MainLine(BufferEnd - WideBuffer.Get(), WideBuffer.Get());
		TArray<FString> Tokens;
		MainLine.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() >= 2)
		{
			FString ContentType(TEXT("application/json"));
			Response.Code = 200;

			// handle the request
			if (Tokens[0] != TEXT("GET"))
			{
				Response.Body = FString::Printf(TEXT("{ \"error\": \"Method %s not allowed\" }"), *Tokens[0]);
				Response.Code = 405;
			}
			else if (Tokens[1].StartsWith(TEXT("/stats")))
			{
				Response.Body = GetAllCountersAsJson();

				// retrieving stats resets them by default, unless ?peek parameter is passed
				const int kStatsTokenLength = 6; // strlen("/stats");
				FString TokenRemainder = Tokens[1].Mid(kStatsTokenLength);
				if (TokenRemainder != TEXT("?peek"))
				{
					ResetStatsForNextPeriod();
				}
			}
			else if (Tokens[1].StartsWith(TEXT("/exec?c=")))
			{
				FString ExecCmd = Tokens[1].Mid(8);
				FString ExecCmdDecoded = FPlatformHttp::UrlDecode(ExecCmd);

				FStringOutputDevice StringOutDevice;
				StringOutDevice.SetAutoEmitLineTerminator(true);

				bool bResult = false;
				if (ExecCmdCallback.IsBound())
				{
					bResult = ExecCmdCallback.Execute(ExecCmdDecoded, StringOutDevice);
					Response.Body = StringOutDevice;
					ContentType = TEXT("text/text");
				}
				else
				{
					Response.Body = FString::Printf(TEXT("{ \"error\": \"exec handler not found\" }"));
				}

				Response.Code = bResult ? 200 : 404;
			}
			else
			{
				Response.Body = FString::Printf(TEXT("{ \"error\": \"%s not found\" }"), *Tokens[1]);
				Response.Code = 404;
			}

			// send the response headers
			Response.Header = FString::Printf(TEXT("HTTP/1.0 %d\r\nContent-Length: %d\r\nContent-Type: %s\r\n\r\n"), Response.Code, Response.Body.Len(), *ContentType);
			bSuccess = true;
		}
		else
		{
			UE_LOG(LogPerfCounters, Warning, TEXT("Unable to parse HTTP request header: %s"), *MainLine);
		}
	}
	else
	{
		UE_LOG(LogPerfCounters, Warning, TEXT("Unable to immediately receive full request header"));
	}

	return bSuccess;
}

double FPerfCounters::GetNumber(const FString& Name, double DefaultValue)
{
	FJsonVariant * JsonValue = PerfCounterMap.Find(Name);
	if (JsonValue == nullptr)
	{
		return DefaultValue;
	}

	if (JsonValue->Format != FJsonVariant::Number)
	{
		UE_LOG(LogPerfCounters, Warning, TEXT("Attempting to get PerfCounter '%s' as number, but it is not (Json format=%d). Default value %f will be returned"), 
			*Name, static_cast<int32>(JsonValue->Format), DefaultValue);

		return DefaultValue;
	}

	return JsonValue->NumberValue;
}

void FPerfCounters::SetNumber(const FString& Name, double Value, uint32 Flags)
{
	FJsonVariant& JsonValue = PerfCounterMap.FindOrAdd(Name);
	JsonValue.Format = FJsonVariant::Number;
	JsonValue.Flags = Flags;
	JsonValue.NumberValue = Value;
}

void FPerfCounters::SetString(const FString& Name, const FString& Value, uint32 Flags)
{
	FJsonVariant& JsonValue = PerfCounterMap.FindOrAdd(Name);
	JsonValue.Format = FJsonVariant::String;
	JsonValue.Flags = Flags;
	JsonValue.StringValue = Value;
}

void FPerfCounters::SetJson(const FString& Name, const FProduceJsonCounterValue& InCallback, uint32 Flags)
{
	FJsonVariant& JsonValue = PerfCounterMap.FindOrAdd(Name);
	JsonValue.Format = FJsonVariant::Callback;
	JsonValue.Flags = Flags;
	JsonValue.CallbackValue = InCallback;
}
