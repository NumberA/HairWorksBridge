// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "CorePrivatePCH.h"


// Collection of events listening for this trigger.
static TArray<FEvent*> ListeningEvents;


/*******************************************************************
 * FIOSFramePacer implementation
 *******************************************************************/

@interface FIOSFramePacer : NSObject
{
    @public
	FEvent *FramePacerEvent;
}

-(void)run:(id)param;
-(void)signal:(id)param;

@end


@implementation FIOSFramePacer

-(void)run:(id)param
{
	NSRunLoop *runloop = [NSRunLoop currentRunLoop];
	CADisplayLink *displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(signal:)];
	displayLink.frameInterval = FIOSPlatformRHIFramePacer::FrameInterval;
    
	[displayLink addToRunLoop:runloop forMode:NSDefaultRunLoopMode];
	[runloop run];
}


-(void)signal:(id)param
{
    for( auto& NextEvent : ListeningEvents )
    {
        NextEvent->Trigger();
    }
}

@end



/*******************************************************************
 * FIOSPlatformRHIFramePacer implementation
 *******************************************************************/


namespace IOSDisplayConstants
{
    const uint32 MaxRefreshRate = 60;
}

uint32 FIOSPlatformRHIFramePacer::FrameInterval = 1;
FIOSFramePacer* FIOSPlatformRHIFramePacer::FramePacer = nil;


bool FIOSPlatformRHIFramePacer::IsEnabled()
{
    static bool bIsRHIFramePacerEnabled = false;
	static bool bInitialized = false;

	if (!bInitialized)
	{
		FString FrameRateLockAsEnum;
		GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("FrameRateLock"), FrameRateLockAsEnum, GEngineIni);

		uint32 FrameRateLock = 60;
		FParse::Value(*FrameRateLockAsEnum, TEXT("PUFRL_"), FrameRateLock);
		if (FrameRateLock == 0)
		{
			FrameRateLock = 60;
		}

		if (!bIsRHIFramePacerEnabled)
		{
			check((IOSDisplayConstants::MaxRefreshRate % FrameRateLock) == 0);
			FrameInterval = IOSDisplayConstants::MaxRefreshRate / FrameRateLock;

			bIsRHIFramePacerEnabled = (FrameInterval > 0);
		}
		bInitialized = true;
	}
	
	return bIsRHIFramePacerEnabled;
}

void FIOSPlatformRHIFramePacer::InitWithEvent(FEvent* TriggeredEvent)
{
    // Create display link thread
    FramePacer = [[FIOSFramePacer alloc] init];
    [NSThread detachNewThreadSelector:@selector(run:) toTarget:FramePacer withObject:nil];
        
    // Only one supported for now, we may want more eventually.
    ListeningEvents.Add( TriggeredEvent );
}

void FIOSPlatformRHIFramePacer::Suspend()
{
    // send a signal to the events if we are enabled
    if (IsEnabled())
    {
        [FramePacer signal:0];
    }
}

void FIOSPlatformRHIFramePacer::Resume()
{
    
}

void FIOSPlatformRHIFramePacer::Destroy()
{
    if( FramePacer != nil )
    {
        [FramePacer release];
        FramePacer = nil;
    }
}