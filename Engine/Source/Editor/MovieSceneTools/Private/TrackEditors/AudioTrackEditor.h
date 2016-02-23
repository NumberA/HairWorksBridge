// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Tools for audio tracks
 */
class FAudioTrackEditor
	: public FMovieSceneTrackEditor
{
public:

	/**
	 * Constructor
	 *
	 * @param InSequencer The sequencer instance to be used by this tool
	 */
	FAudioTrackEditor(TSharedRef<ISequencer> InSequencer);

	/** Virtual destructor. */
	virtual ~FAudioTrackEditor();

	/**
	 * Creates an instance of this class.  Called by a sequencer 
	 *
	 * @param OwningSequencer The sequencer instance to be used by this tool
	 * @return The new instance of this class
	 */
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer);

public:

	// ISequencerTrackEditor interface

	virtual void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override;
	virtual bool HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	
protected:

	/** Delegate for AnimatablePropertyChanged in HandleAssetAdded for master sounds */
	bool AddNewMasterSound(float KeyTime, class USoundBase* Sound);

	/** Delegate for AnimatablePropertyChanged in HandleAssetAdded for attached sounds */
	bool AddNewAttachedSound(float KeyTime, class USoundBase* Sound, TArray<UObject*> ObjectsToAttachTo);

private:

	/** Callback for executing the "Add Event Track" menu entry. */
	void HandleAddAudioTrackMenuEntryExecute();
};


/**
 * Class for audio sections, handles drawing of all waveform previews.
 */
class FAudioSection
	: public ISequencerSection
	, public TSharedFromThis<FAudioSection>
{
public:

	/** Constructor. */
	FAudioSection(UMovieSceneSection& InSection, bool bOnAMasterTrack);

	/** Virtual destructor. */
	virtual ~FAudioSection();

public:

	// ISequencerSection interface

	virtual UMovieSceneSection* GetSectionObject() override;
	virtual bool ShouldDrawKeyAreaBackground() const override;
	virtual FText GetDisplayName() const override;
	virtual FText GetSectionTitle() const override;
	virtual float GetSectionHeight() const override;
	virtual void GenerateSectionLayout( class ISectionLayoutBuilder& LayoutBuilder) const override { }
	virtual int32 OnPaintSection(const FGeometry& AllottedGeometry, const FSlateRect& SectionClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const FGeometry& ParentGeometry, const double InCurrentTime, const float InDeltaTime) override;
	
private:

	/* Re-creates the texture used to preview the waveform. */
	void RegenerateWaveforms(TRange<float> DrawRange, int32 XOffset, int32 XSize);

private:

	/** The section we are visualizing. */
	UMovieSceneSection& Section;
	
	/** The waveform thumbnail render object. */
	TSharedPtr<class FAudioThumbnail> WaveformThumbnail;

	/** Stored data about the waveform to determine when it is invalidated. */
	TRange<float> StoredDrawRange;
	int32 StoredXOffset;
	int32 StoredXSize;

	/** Whether this section is on a master audio track or an attached audio track. */
	bool bIsOnAMasterTrack;
};
