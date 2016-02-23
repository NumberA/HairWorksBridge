// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaSampleQueue.h"
#include "Sound/SoundWave.h"
#include "MediaSoundWave.generated.h"


class FMediaSampleQueue;
class IMediaAudioTrack;
class IMediaPlayer;
class UMediaPlayer;


/**
 * Implements a playable sound asset for audio streams from UMediaPlayer assets.
 */
UCLASS(hidecategories=(Compression, Sound, SoundWave, Subtitles))
class MEDIAASSETS_API UMediaSoundWave
	: public USoundWave
{
	GENERATED_UCLASS_BODY()

	/** The index of the MediaPlayer's audio track to get the wave data from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=MediaPlayer)
	int32 AudioTrackIndex;

	/** The MediaPlayer asset to stream audio from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=MediaPlayer)
	UMediaPlayer* MediaPlayer;

public:

	/**
	 * Sets the MediaPlayer asset to be used for this texture.
	 *
	 * @param InMediaPlayer The asset to set.
	 */
	UFUNCTION(BlueprintCallable, Category="Media|MediaSound")
	void SetMediaPlayer(UMediaPlayer* InMediaPlayer);

public:

	/**
	 * Gets the low-level player associated with the assigned UMediaPlayer asset.
	 *
	 * @return The player, or nullptr if no player is available.
	 */
	TSharedPtr<IMediaPlayer> GetPlayer() const;

public:

	// USoundWave overrides

	virtual int32 GeneratePCMData(uint8* PCMData, const int32 SamplesNeeded) override;
	virtual FByteBulkData* GetCompressedData(FName Format) override;
	virtual int32 GetResourceSizeForFormat(FName Format) override;
	virtual void InitAudioResource(FByteBulkData& CompressedData) override;
	virtual bool InitAudioResource(FName Format) override;

public:

	// UObject overrides

	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	virtual SIZE_T GetResourceSize(EResourceSizeMode::Type Mode) override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;

protected:

	/** Initializes the audio track. */
	void InitializeTrack();

private:

	/** Callback for when the UMediaPlayer changed tracks. */
	void HandleMediaPlayerTracksChanged();

private:

	/** The audio sample queue. */
	TSharedRef<FMediaSampleQueue, ESPMode::ThreadSafe> AudioQueue;

	/** Holds the selected audio track. */
	TSharedPtr<IMediaAudioTrack, ESPMode::ThreadSafe> AudioTrack;

	/** Holds the media player asset currently being used. */
	UPROPERTY()
	TWeakObjectPtr<UMediaPlayer> CurrentMediaPlayer;

	/** Holds queued audio samples. */
	TArray<uint8> QueuedAudio;

	bool bSetupDelegates;
};
