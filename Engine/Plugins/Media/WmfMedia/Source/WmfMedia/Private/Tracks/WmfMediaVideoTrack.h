// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AllowWindowsPlatformTypes.h"


/**
 * Implements a video track using the Windows Media Foundation.
 */
class FWmfMediaVideoTrack
	: public FWmfMediaTrack
	, public IMediaVideoTrack
{
public:

	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InMediaType The media type information for this track.
	 * @param InPresentationDescriptor The descriptor of the presentation that this stream belongs to.
	 * @param InSampler The sample grabber callback object to use.
	 * @param InStreamIndex The stream's index number in the presentation.
	 * @param InStreamDescriptor The stream's descriptor object.
	 */
	FWmfMediaVideoTrack(IMFMediaType* InMediaType, IMFPresentationDescriptor* InPresentationDescriptor, FWmfMediaSampler* InSampler, IMFStreamDescriptor* InStreamDescriptor, DWORD InStreamIndex)
		: FWmfMediaTrack(InPresentationDescriptor, InSampler, InStreamDescriptor, InStreamIndex)
		, Height(0)
		, Width(0)
	{
		BitRate = ::MFGetAttributeUINT32(InMediaType, MF_MT_AVG_BITRATE, 0);
		::MFGetAttributeSize(InMediaType, MF_MT_FRAME_SIZE, &Width, &Height);

		UINT32 Numerator = 0, Denominator = 1;

		if (FAILED(::MFGetAttributeRatio(InMediaType, MF_MT_FRAME_RATE, &Numerator, &Denominator)))
		{
			FrameRate = 0.0f;
		}
		else
		{
			FrameRate = static_cast<float>(Numerator) / Denominator;
		}
	}

public:

	// IMediaVideoTrack interface

	virtual uint32 GetBitRate() const override
	{
		return BitRate;
	}

	virtual FIntPoint GetDimensions() const override
	{
		return FIntPoint(Width, Height);
	}

	virtual float GetFrameRate() const override
	{
		return FrameRate;
	}

	virtual IMediaStream& GetStream() override
	{
		return *this;
	}

#if WITH_ENGINE
	virtual void BindTexture(class FRHITexture* Texture) override { }
	virtual void UnbindTexture(class FRHITexture* Texture) override { }
#endif

private:

	/** The video's average bit rate. */
	uint32 BitRate;

	/** The video's frame rate. */
	float FrameRate;

	/** The video's height in pixels. */
	UINT32 Height;

	/** The video's width in pixels. */
	UINT32 Width;
};


#include "HideWindowsPlatformTypes.h"
