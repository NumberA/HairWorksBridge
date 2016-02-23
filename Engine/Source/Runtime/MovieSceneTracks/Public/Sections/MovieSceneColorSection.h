// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneColorSection.generated.h"

enum class EKeyColorChannel
{
	Red,
	Green,
	Blue,
	Alpha,
};

struct FColorKey
{
	FColorKey( EKeyColorChannel InChannel, float InChannelValue, bool InbIsSlateColor )
	{
		Channel = InChannel;
		ChannelValue = InChannelValue;
		bIsSlateColor = InbIsSlateColor;
	}

	EKeyColorChannel Channel;
	float ChannelValue;
	bool bIsSlateColor;
};

/**
 * A single floating point section
 */
UCLASS( MinimalAPI )
class UMovieSceneColorSection 
	: public UMovieSceneSection
	, public IKeyframeSection<FColorKey>
{
	GENERATED_UCLASS_BODY()
public:
	/** MovieSceneSection interface */
	virtual void MoveSection(float DeltaPosition, TSet<FKeyHandle>& KeyHandles) override;
	virtual void DilateSection(float DilationFactor, float Origin, TSet<FKeyHandle>& KeyHandles) override;
	virtual void GetKeyHandles(TSet<FKeyHandle>& KeyHandles) const override;

	/**
	 * Updates this section
	 *
	 * @param Position	The position in time within the movie scene
	 */
	virtual FLinearColor Eval( float Position, const FLinearColor& DefaultColor ) const;

	// IKeyframeSection interface.
	virtual void AddKey( float Time, const FColorKey& Key, EMovieSceneKeyInterpolation KeyInterpolation ) override;
	virtual bool NewKeyIsNewData(float Time, const FColorKey& Key) const override;
	virtual bool HasKeys(const FColorKey& Key) const override;
	virtual void SetDefault(const FColorKey& Key ) override;

	/**
	 * Gets the red color curve
	 *
	 * @return The rich curve for this color channel
	 */
	FRichCurve& GetRedCurve() { return RedCurve; }
	const FRichCurve& GetRedCurve() const { return RedCurve; }

	/**
	 * Gets the green color curve
	 *
	 * @return The rich curve for this color channel
	 */
	FRichCurve& GetGreenCurve() { return GreenCurve; }
	const FRichCurve& GetGreenCurve() const { return GreenCurve; }
	/**
	 * Gets the blue color curve
	 *
	 * @return The rich curve for this color channel
	 */
	FRichCurve& GetBlueCurve() { return BlueCurve; }
	const FRichCurve& GetBlueCurve() const { return BlueCurve; }
	
	/**
	 * Gets the alpha color curve
	 *
	 * @return The rich curve for this color channel
	 */
	FRichCurve& GetAlphaCurve() { return AlphaCurve; }
	const FRichCurve& GetAlphaCurve() const { return AlphaCurve; }

private:

	/** Red curve data */
	UPROPERTY()
	FRichCurve RedCurve;

	/** Green curve data */
	UPROPERTY()
	FRichCurve GreenCurve;

	/** Blue curve data */
	UPROPERTY()
	FRichCurve BlueCurve;

	/** Alpha curve data */
	UPROPERTY()
	FRichCurve AlphaCurve;
};
