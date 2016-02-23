// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	NetworkProfiler.cpp: server network profiling support.
=============================================================================*/

#include "EnginePrivate.h"

#if USE_NETWORK_PROFILER

#define SCOPE_LOCK_REF(X) FScopeLock ScopeLock(&X);


/**
 * Whether to track the raw network data or not.
 */
#define NETWORK_PROFILER_TRACK_RAW_NETWORK_DATA		0

#include "Net/UnrealNetwork.h"
#include "IPAddress.h"
#include "Net/NetworkProfiler.h"

/** Global network profiler instance. */
FNetworkProfiler GNetworkProfiler;

/** Magic value, determining that file is a network profiler file.				*/
#define NETWORK_PROFILER_MAGIC						0x1DBF348C
/** Version of memory profiler. Incremented on serialization changes.			*/
#define NETWORK_PROFILER_VERSION					9

static const FString UnknownName("UnknownName");

static const double MaxTempFileAgeSeconds = 60.0 * 60.0 * 24.0 * 5.0;

enum ENetworkProfilingPayloadType
{
	NPTYPE_FrameMarker			= 0,	// Frame marker, signaling beginning of frame.	
	NPTYPE_SocketSendTo,				// FSocket::SendTo
	NPTYPE_SendBunch,					// UChannel::SendBunch
	NPTYPE_SendRPC,						// Sending RPC
	NPTYPE_ReplicateActor,				// Replicated object	
	NPTYPE_ReplicateProperty,			// Property being replicated.
	NPTYPE_EndOfStreamMarker,			// End of stream marker		
	NPTYPE_Event,						// Event
	NPTYPE_RawSocketData,				// Raw socket data being sent
	NPTYPE_SendAck,						// Ack being sent
	NPTYPE_WritePropertyHeader,			// Property header being written
	NPTYPE_ExportBunch,					// Exported GUIDs
	NPTYPE_MustBeMappedGuids,			// Must be mapped GUIDs
	NPTYPE_BeginContentBlock,			// Content block headers
	NPTYPE_EndContentBlock,				// Content block footers
	NPTYPE_WritePropertyHandle,			// Property handles
	NPTYPE_ConnectionChanged			// Connection changed
};

/*=============================================================================
	FNetworkProfilerHeader implementation.
=============================================================================*/

FNetworkProfilerHeader::FNetworkProfilerHeader()
	: Magic(NETWORK_PROFILER_MAGIC)
	, Version(NETWORK_PROFILER_VERSION)
	, NameTableOffset(0)
	, NameTableEntries(0)
	, AddressTableOffset( 0 )
	, AddressTableEntries( 0 )
{
}

void FNetworkProfilerHeader::Reset(const FURL& InURL)
{
	NameTableOffset = 0;
	NameTableEntries = 0;

	AddressTableOffset = 0;
	AddressTableEntries = 0;
		
	FParse::Value( FCommandLine::Get(), TEXT( "NETWORKPROFILER=" ), Tag );
	GameName = FApp::GetGameName();
	URL = InURL.ToString();
}

FArchive& operator << ( FArchive& Ar, FNetworkProfilerHeader& Header )
{
	check( Ar.IsSaving() );
	Ar	<< Header.Magic
		<< Header.Version
		<< Header.NameTableOffset
		<< Header.NameTableEntries
		<< Header.AddressTableOffset
		<< Header.AddressTableEntries;
	Header.Tag.SerializeAsANSICharArray( Ar );
	Header.GameName.SerializeAsANSICharArray( Ar );
	Header.URL.SerializeAsANSICharArray( Ar );
	return Ar;
}

/*=============================================================================
	FNetworkProfiler implementation.
=============================================================================*/

/**
 * Constructor, initializing member variables.
 */
FNetworkProfiler::FNetworkProfiler()
:	FileWriter(NULL)
,	bHasNoticeableNetworkTrafficOccured(false)
,	bIsTrackingEnabled(false)
,	LastAddress( 0xFFFFFFFFFFFFFFFF )
{
}

/**
 * Returns index of passed in name into name array. If not found, adds it.
 *
 * @param	Name	Name to find index for
 * @return	Index of passed in name
 */
int32 FNetworkProfiler::GetNameTableIndex( const FString& Name )
{
	// Index of name in name table.
	int32 Index = INDEX_NONE;

	// Use index if found.
	int32* IndexPtr = NameToNameTableIndexMap.Find( Name );
	if( IndexPtr )
	{
		Index = *IndexPtr;
	}
	// Encountered new name, add to array and set index mapping.
	else
	{
		Index = NameArray.Num();
		new(NameArray)FString(Name);
		NameToNameTableIndexMap.Add(*Name,Index);
	}

	check(Index!=INDEX_NONE);
	return Index;
}

/**
* Returns index of passed in name into name array. If not found, adds it.
*
* @param	Name	Name to find index for
* @return	Index of passed in name
*/
int32 FNetworkProfiler::GetAddressTableIndex( uint64 Address )
{
	// Index of name in name table.
	int32 Index = INDEX_NONE;

	// Use index if found.
	int32* IndexPtr = AddressTableIndexMap.Find( Address );
	if ( IndexPtr )
	{
		Index = *IndexPtr;
	}
	// Encountered new name, add to array and set index mapping.
	else
	{
		Index = AddressArray.Num();
		AddressArray.Add( Address );
		AddressTableIndexMap.Add( Address, Index );
	}

	check( Index != INDEX_NONE );
	return Index;
}

/**
 * Enables/ disables tracking. Emits a session changes if disabled.
 *
 * @param	bShouldEnableTracking	Whether tracking should be enabled or not
 */
void FNetworkProfiler::EnableTracking( bool bShouldEnableTracking )
{
	if( bShouldEnableTracking )
	{
		UE_LOG(LogNet, Log, TEXT("Network Profiler: ENABLED"));
	}

	// Flush existing session in progress if we're disabling tracking and it was enabled.
	if( bIsTrackingEnabled && !bShouldEnableTracking )
	{
		TrackSessionChange(false,FURL());
	}
	// Important to not change bIsTrackingEnabled till after we flushed as it's used during flushing.
	bIsTrackingEnabled = bShouldEnableTracking;
}

/**
 * Marks the beginning of a frame.
 */
void FNetworkProfiler::TrackFrameBegin()
{
	if( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);
		uint8 Type = NPTYPE_FrameMarker;
		(*FileWriter) << Type;
		float RelativeTime=  (float)(FPlatformTime::Seconds() - GStartTime);
		(*FileWriter) << RelativeTime;
		LastAddress = 0xFFFFFFFFFFFFFFFF;
	}
}

/**
* Tracks when connection address changes, and will emit token if different from last connection
*
* @param	Connection				Destination address
*/
void FNetworkProfiler::SetCurrentConnection( UNetConnection* Connection )
{
	if ( bIsTrackingEnabled && Connection != nullptr )
	{
		const uint32 NetworkByteOrderIP = Connection->GetAddrAsInt();
		const uint32 Port				= Connection->GetAddrPort();

		const uint64 Address = ( ( ( uint64 )NetworkByteOrderIP ) << 32 ) | Port;

		if ( Address != LastAddress )
		{
			uint32 Index = GetAddressTableIndex( Address );

			uint8 Type = NPTYPE_ConnectionChanged;
			( *FileWriter ) << Type;
			( *FileWriter ).SerializeIntPacked( Index );

			LastAddress = Address;
		}
	}
}

/**
 * Tracks and RPC being sent.
 * 
 * @param	Actor		Actor RPC is being called on
 * @param	Function	Function being called
 * @param	NumBits		Number of bits serialized into bunch for this RPC
 */
void FNetworkProfiler::TrackSendRPC( const AActor* Actor, const UFunction* Function, uint16 NumHeaderBits, uint16 NumParameterBits, uint16 NumFooterBits, UNetConnection* Connection )
{
	if( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF( CriticalSection );

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_SendRPC;
		(*FileWriter) << Type;
		uint32 ActorNameTableIndex = GetNameTableIndex( Actor->GetName() );
		( *FileWriter ).SerializeIntPacked( ActorNameTableIndex );
		uint32 FunctionNameTableIndex = GetNameTableIndex( Function->GetName() );
		( *FileWriter ).SerializeIntPacked( FunctionNameTableIndex );
		( *FileWriter ) << NumHeaderBits;
		(*FileWriter) << NumParameterBits;
		(*FileWriter) << NumFooterBits;
	}
}

void FNetworkProfiler::TrackQueuedRPC( UNetConnection* Connection, UObject* TargetObject, const AActor* Actor, const UFunction* Function, uint16 NumHeaderBits, uint16 NumParameterBits, uint16 NumFooterBits  )
{
	if( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);
		
		FQueuedRPCInfo Info;
		Info.Connection = Connection;
		Info.TargetObject = TargetObject;
		Info.ActorNameIndex = GetNameTableIndex( Actor->GetName() );
		Info.FunctionNameIndex = GetNameTableIndex( Function->GetName() );
		Info.NumHeaderBits = NumHeaderBits;
		Info.NumParameterBits = NumParameterBits;
		Info.NumFooterBits = NumFooterBits;

		QueuedRPCs.Add(Info);
	}
}

void FNetworkProfiler::FlushQueuedRPCs( UNetConnection* Connection, UObject* TargetObject )
{
	if( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);

		SetCurrentConnection( Connection );

		for ( int i = QueuedRPCs.Num() - 1; i >= 0; --i )
		{
			if (QueuedRPCs[i].Connection == Connection && QueuedRPCs[i].TargetObject == TargetObject)
			{
				uint8 Type = NPTYPE_SendRPC;
				(*FileWriter) << Type;
				(*FileWriter).SerializeIntPacked( QueuedRPCs[i].ActorNameIndex );
				(*FileWriter).SerializeIntPacked( QueuedRPCs[i].FunctionNameIndex );
				(*FileWriter) << QueuedRPCs[i].NumHeaderBits;
				(*FileWriter) << QueuedRPCs[i].NumParameterBits;
				(*FileWriter) << QueuedRPCs[i].NumFooterBits;

				QueuedRPCs.RemoveAtSwap(i);
			}
		}
	}
}

/**
 * Low level FSocket::Send information.
 *
 * @param	SocketDesc				Description of socket data is being sent to
 * @param	Data					Data sent
 * @param	BytesSent				Bytes actually being sent
 */
void FNetworkProfiler::TrackSocketSend( const FString& SocketDesc, const void* Data, uint16 BytesSent )
{
	if( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);
		uint32 DummyIp = 0;
		//TrackSocketSendToCore( SocketDesc, Data, BytesSent, DummyIp );
	}
}

/**
 * Low level FSocket::SendTo information.
 *
 * @param	SocketDesc				Description of socket data is being sent to
 * @param	Data					Data sent
 * @param	BytesSent				Bytes actually being sent
 * @param	Connection				Destination address
 */
void FNetworkProfiler::TrackSocketSendTo(
	const FString& SocketDesc,
	const void* Data,
	uint16 BytesSent,
	uint16 NumPacketIdBits,
	uint16 NumBunchBits,
	uint16 NumAckBits,
	uint16 NumPaddingBits,
	UNetConnection* Connection )
{
	if( bIsTrackingEnabled )
	{
		TrackSocketSendToCore( SocketDesc, Data, BytesSent, NumPacketIdBits, NumBunchBits, NumAckBits, NumPaddingBits, Connection);
	}
}

/**
 * Low level FSocket::SendTo information.
 *
 * @param	SocketDesc				Description of socket data is being sent to
 * @param	Data					Data sent
 * @param	BytesSent				Bytes actually being sent
 * @param	Connection				Destination address
 */
void FNetworkProfiler::TrackSocketSendToCore(
	const FString& SocketDesc,
	const void* Data,
	uint16 BytesSent,
	uint16 NumPacketIdBits,
	uint16 NumBunchBits,
	uint16 NumAckBits,
	uint16 NumPaddingBits,
	UNetConnection* Connection )
{
	if( bIsTrackingEnabled )
	{
		// Low level socket code is called from multiple threads.
		SCOPE_LOCK_REF(CriticalSection);

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_SocketSendTo;
		(*FileWriter) << Type;
		uint32 NameTableIndex = GetNameTableIndex( SocketDesc );
		(*FileWriter).SerializeIntPacked( NameTableIndex );
		(*FileWriter) << BytesSent;
		(*FileWriter) << NumPacketIdBits;
		(*FileWriter) << NumBunchBits;
		(*FileWriter) << NumAckBits;
		(*FileWriter) << NumPaddingBits;
#if NETWORK_PROFILER_TRACK_RAW_NETWORK_DATA
		Type = NPTYPE_RawSocketData;
		(*FileWriter) << Type;
		(*FileWriter) << BytesSent;
		check( FileWriter->IsSaving() );
		FileWriter->Serialize( const_cast<void*>(Data), BytesSent );
#endif
		bHasNoticeableNetworkTrafficOccured = true;
	}
}

/**
 * Mid level UChannel::SendBunch information.
 * 
 * @param	Channel		UChannel data is being sent to/ on
 * @param	OutBunch	FOutBunch being sent
 * @param	NumBits		Num bits to serialize for this bunch (not including merging)
 */
void FNetworkProfiler::TrackSendBunch( FOutBunch* OutBunch, uint16 NumBits, UNetConnection* Connection )
{
	if( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF( CriticalSection );

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_SendBunch;
		(*FileWriter) << Type;
		uint16 ChannelIndex = OutBunch->ChIndex;
		(*FileWriter) << ChannelIndex;
		uint8 ChannelType = OutBunch->ChType;
		(*FileWriter) << ChannelType;
		(*FileWriter) << NumBits;
	}
}

void FNetworkProfiler::PushSendBunch( UNetConnection* Connection, FOutBunch* OutBunch, uint16 NumHeaderBits, uint16 NumPayloadBits )
{
	if ( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);
		OutgoingBunches.FindOrAdd(Connection).Emplace(OutBunch->ChIndex, OutBunch->ChType, NumHeaderBits, NumPayloadBits);
	}
}

void FNetworkProfiler::PopSendBunch( UNetConnection* Connection )
{
	if ( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);
		if ( OutgoingBunches.Contains(Connection) && OutgoingBunches[Connection].Num() > 0 )
		{
			OutgoingBunches[Connection].Pop();
		}
	}
}

void FNetworkProfiler::FlushOutgoingBunches( UNetConnection* Connection )
{
	if ( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF( CriticalSection );

		SetCurrentConnection( Connection );

		if ( OutgoingBunches.Contains( Connection ) )
		{
			
			for ( FSendBunchInfo& BunchInfo : OutgoingBunches[Connection] )
			{
				uint8 Type = NPTYPE_SendBunch;
				(*FileWriter) << Type;
				(*FileWriter) << BunchInfo.ChannelIndex;
				(*FileWriter) << BunchInfo.ChannelType;
				(*FileWriter) << BunchInfo.NumHeaderBits;
				(*FileWriter) << BunchInfo.NumPayloadBits;
			}

			OutgoingBunches[Connection].SetNum(0);
		}
	}
}

/**
 * Track actor being replicated.
 *
 * @param	Actor		Actor being replicated
 */
void FNetworkProfiler::TrackReplicateActor( const AActor* Actor, FReplicationFlags RepFlags, uint32 Cycles, UNetConnection* Connection )
{
	if( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF( CriticalSection );

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_ReplicateActor;
		(*FileWriter) << Type;
		uint8 NetFlags = (RepFlags.bNetInitial << 1) | (RepFlags.bNetOwner << 2);
		(*FileWriter) << NetFlags;
		uint32 NameTableIndex = GetNameTableIndex( Actor->GetName() );
		(*FileWriter).SerializeIntPacked( NameTableIndex );
		float TimeInMS = FPlatformTime::ToMilliseconds(Cycles);	// FIXME: We may want to just pass in cycles to profiler to we don't lose precision
		(*FileWriter) << TimeInMS;
		// Use actor replication as indication whether session is worth keeping or not.
		bHasNoticeableNetworkTrafficOccured = true;
	}
}

/**
 * Track property being replicated.
 *
 * @param	Property	Property being replicated
 * @param	NumBits		Number of bits used to replicate this property
 */
void FNetworkProfiler::TrackReplicateProperty( const UProperty* Property, uint16 NumBits, UNetConnection* Connection )
{
	if( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_ReplicateProperty;
		(*FileWriter) << Type;
		uint32 NameTableIndex = GetNameTableIndex( Property->GetName() );
		(*FileWriter).SerializeIntPacked( NameTableIndex );
		(*FileWriter) << NumBits;
	}
}

void FNetworkProfiler::TrackWritePropertyHeader( const UProperty* Property, uint16 NumBits, UNetConnection* Connection )
{
	if( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_WritePropertyHeader;
		(*FileWriter) << Type;
		uint32 NameTableIndex = GetNameTableIndex( Property->GetName() );
		(*FileWriter).SerializeIntPacked( NameTableIndex );
		(*FileWriter) << NumBits;
	}
}

/**
 * Track event occurring, like e.g. client join/ leave
 *
 * @param	EventName			Name of the event
 * @param	EventDescription	Additional description/ information for event
 */
void FNetworkProfiler::TrackEvent( const FString& EventName, const FString& EventDescription, UNetConnection* Connection )
{
	if( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_Event;
		(*FileWriter) << Type;
		uint32 EventNameNameTableIndex = GetNameTableIndex( EventName );
		(*FileWriter).SerializeIntPacked( EventNameNameTableIndex );
		uint32 EventDescriptionNameTableIndex = GetNameTableIndex( EventDescription );
		(*FileWriter).SerializeIntPacked( EventDescriptionNameTableIndex );
	}
}

/**
 * Called when the server first starts listening and on round changes or other
 * similar game events. We write to a dummy file that is renamed when the current
 * session ends.
 *
 * @param	bShouldContinueTracking		Whether to continue tracking
 * @param	InURL						URL used for new session
 */
void FNetworkProfiler::TrackSessionChange( bool bShouldContinueTracking, const FURL& InURL )
{
#if ALLOW_DEBUG_FILES
	if ( bIsTrackingEnabled )
	{
		UE_LOG( LogNet, Log, TEXT( "Network Profiler: TrackSessionChange.  InURL: %s" ), *InURL.ToString() );

		// Session change might occur while other thread uses low level networking.
		SCOPE_LOCK_REF(CriticalSection);

		// End existing tracking session.
		if( FileWriter )
		{	
			if( bHasNoticeableNetworkTrafficOccured )
			{
				UE_LOG(LogNet, Log, TEXT("Network Profiler: Writing out session file for '%s'"), *CurrentHeader.GetURL());

				// Write end of stream marker.
				uint8 Type = NPTYPE_EndOfStreamMarker;
				(*FileWriter) << Type;

				// Write out name table and update header with offset and count.
				CurrentHeader.SetNameTableValues(FileWriter->Tell(), NameArray.Num());

				for( int32 NameIndex=0; NameIndex<NameArray.Num(); NameIndex++ )
				{
					NameArray[NameIndex].SerializeAsANSICharArray( *FileWriter );
				}

				CurrentHeader.SetAddressTableValues( FileWriter->Tell(), AddressArray.Num() );

				for ( int32 AddressIndex=0; AddressIndex < AddressArray.Num(); AddressIndex++ )
				{
					( *FileWriter ) << AddressArray[AddressIndex];
				}

				// Seek to the beginning of the file and write out proper header.
				FileWriter->Seek( 0 );
				(*FileWriter) << CurrentHeader;

				// Close file writer so we can rename the file to its final destination.
				FileWriter->Close();
			
				// Rename/ move file.
				static int32 Salt = 0;
				Salt++;		// Use a salt to solve the issue where this function is called so fast it produces the same time (seems to happen during seamless travel)
				const FString FinalFileName = FPaths::ProfilingDir() + FApp::GetGameName() + TEXT("-") + FDateTime::Now().ToString() + FString::Printf(TEXT("[%i]"), Salt) + TEXT(".nprof");
				bool bWasMovedSuccessfully = IFileManager::Get().Move( *FinalFileName, *TempFileName );

				// Send data to UnrealConsole to upload to DB.
				if( bWasMovedSuccessfully )
				{
					UE_LOG( LogNet, Log, TEXT( "Network Profiler: Saved SUCCESS: %s" ), *FinalFileName );

					SendDataToPCViaUnrealConsole( TEXT( "UE_PROFILER!NETWORK:" ), *FinalFileName );
				}
				else
				{
					UE_LOG( LogNet, Error, TEXT( "Network Profiler: Saved FAILED: %s" ), *FinalFileName );
				}
			}
			else
			{
				UE_LOG(LogNet, Warning, TEXT("Network Profiler: Nothing important happened"));
				FileWriter->Close();

				// Delete the temporary file.
				IFileManager::Get().Delete(*TempFileName);
			}

			// Clean up.
			delete FileWriter;
			FileWriter = NULL;
			bHasNoticeableNetworkTrafficOccured = false;
		}

		if( bShouldContinueTracking )
		{
			// Delete any stale .tmp files.
			TArray<FString> FoundTempFiles;
			IFileManager::Get().FindFiles(FoundTempFiles, *(FPaths::ProfilingDir() / TEXT("*.tmp")), true, false);
			
			for ( const FString& FoundFile : FoundTempFiles)
			{
				const FString FullFilename = FPaths::ProfilingDir() + FoundFile;
				if ( IFileManager::Get().GetFileAgeSeconds(*FullFilename) > MaxTempFileAgeSeconds)
				{
					IFileManager::Get().Delete(*FullFilename);
				}
			}

			// Start a new tracking session.
			check( FileWriter == NULL );

			// Use a dummy name for sessions in progress that is renamed at end.
			TempFileName = FPaths::CreateTempFilename(*FPaths::ProfilingDir(), TEXT("NetworkProfiling-"));

			// Create folder and file writer.
			IFileManager::Get().MakeDirectory( *FPaths::GetPath(TempFileName) );
			FileWriter = IFileManager::Get().CreateFileWriter( *TempFileName, FILEWRITE_EvenIfReadOnly );
			check( FileWriter );
			
			CurrentHeader.Reset(InURL);

			// Serialize a header of the proper size, overwritten when session ends.
			(*FileWriter) << CurrentHeader;
		}
	}
#endif	//#if ALLOW_DEBUG_FILES
}

void FNetworkProfiler::TrackSendAck( uint16 NumBits, UNetConnection* Connection )
{
	if ( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_SendAck;
		(*FileWriter) << Type;
		(*FileWriter) << NumBits;
	}
}

void FNetworkProfiler::TrackMustBeMappedGuids( uint16 NumGuids, uint16 NumBits, UNetConnection* Connection )
{
	if ( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_MustBeMappedGuids;
		(*FileWriter) << Type;
		(*FileWriter) << NumGuids;
		(*FileWriter) << NumBits;
	}
}

void FNetworkProfiler::TrackExportBunch( uint16 NumBits, UNetConnection* Connection )
{
	if ( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_ExportBunch;
		(*FileWriter) << Type;
		(*FileWriter) << NumBits;
	}
}

 
void FNetworkProfiler::TrackBeginContentBlock( UObject* Object, uint16 NumBits, UNetConnection* Connection )
{
	if ( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_BeginContentBlock;
		(*FileWriter) << Type;
		uint32 NameTableIndex = GetNameTableIndex( Object != nullptr ? Object->GetName() : UnknownName );
		(*FileWriter).SerializeIntPacked( NameTableIndex );
		(*FileWriter) << NumBits;
	 }
}

void FNetworkProfiler::TrackEndContentBlock( UObject* Object, uint16 NumBits, UNetConnection* Connection )
{
	if ( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_EndContentBlock;
		(*FileWriter) << Type;
		uint32 NameTableIndex = GetNameTableIndex( Object != nullptr ? Object->GetName() : UnknownName );
		(*FileWriter).SerializeIntPacked( NameTableIndex );
		(*FileWriter) << NumBits;
	}
}

void FNetworkProfiler::TrackWritePropertyHandle( uint16 NumBits, UNetConnection* Connection )
{
	if ( bIsTrackingEnabled )
	{
		SCOPE_LOCK_REF(CriticalSection);

		SetCurrentConnection( Connection );

		uint8 Type = NPTYPE_WritePropertyHandle;
		(*FileWriter) << Type;
		(*FileWriter) << NumBits;
	}
}

/**
 * Processes any network profiler specific exec commands
 *
 * @param InWorld	The world in this context
 * @param Cmd		The command to parse
 * @param Ar		The output device to write data to
 *
 * @return			True if processed, false otherwise
*/
bool FNetworkProfiler::Exec( UWorld * InWorld, const TCHAR* Cmd, FOutputDevice & Ar )
{
	if (FParse::Command(&Cmd, TEXT("ENABLE")))
	{
		EnableTracking( true );
	}
	else if (FParse::Command(&Cmd, TEXT("DISABLE")))
	{
		EnableTracking( false );
	} 
	else 
	{
		// Default to toggle
		EnableTracking( !bIsTrackingEnabled );
	}

	// If we are tracking, and we don't have a file writer, force one now 
	if ( bIsTrackingEnabled && FileWriter == NULL ) 
	{
		TrackSessionChange( true, InWorld != nullptr ? InWorld->URL : FURL() );
		if ( FileWriter == NULL )
		{
			UE_LOG(LogNet, Warning, TEXT("FNetworkProfiler::Exec: FAILED to create file writer!"));
			EnableTracking( false );
		}
	}

	return true;
}

#endif	//#if USE_NETWORK_PROFILER

