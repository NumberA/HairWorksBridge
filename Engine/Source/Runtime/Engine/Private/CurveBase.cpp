// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "CsvParser.h"
#include "EditorFramework/AssetImportData.h"

DECLARE_CYCLE_STAT(TEXT("RichCurve Eval"), STAT_RichCurve_Eval, STATGROUP_Engine);


/* FKeyHandleMap
 *****************************************************************************/

void FKeyHandleMap::Add( const FKeyHandle& InHandle, int32 InIndex )
{
	KeyHandlesToIndices.Add( InHandle, InIndex );
}


void FKeyHandleMap::Empty()
{
	KeyHandlesToIndices.Empty();
}


void FKeyHandleMap::Remove( const FKeyHandle& InHandle )
{
	KeyHandlesToIndices.Remove( InHandle );
}


const int32* FKeyHandleMap::Find( const FKeyHandle& InHandle ) const
{
	return KeyHandlesToIndices.Find( InHandle );
}


const FKeyHandle* FKeyHandleMap::FindKey( int32 KeyIndex ) const
{
	return KeyHandlesToIndices.FindKey( KeyIndex );
}


int32 FKeyHandleMap::Num() const
{
	return KeyHandlesToIndices.Num();
}


TMap<FKeyHandle, int32>::TConstIterator FKeyHandleMap::CreateConstIterator() const
{
	return KeyHandlesToIndices.CreateConstIterator();
}


TMap<FKeyHandle, int32>::TIterator FKeyHandleMap::CreateIterator() 
{
	return KeyHandlesToIndices.CreateIterator();
}


bool FKeyHandleMap::Serialize(FArchive& Ar)
{
	// only allow this map to be saved to the transaction buffer
	if( Ar.IsTransacting() )
	{
		Ar << KeyHandlesToIndices;
	}

	return true;
}


bool FKeyHandleMap::operator==(const FKeyHandleMap& Other) const
{
	if (KeyHandlesToIndices.Num() != Other.KeyHandlesToIndices.Num())
	{
		return false;
	}

	for (TMap<FKeyHandle, int32>::TConstIterator It(KeyHandlesToIndices); It; ++It)
	{
		int32 const* OtherVal = Other.KeyHandlesToIndices.Find(It.Key());
		if (!OtherVal || *OtherVal != It.Value() )
		{
			return false;
		}
	}

	return true;
}


bool FKeyHandleMap::operator!=(const FKeyHandleMap& Other) const
{
	return !(*this==Other);
}


/* FIndexedCurve
 *****************************************************************************/

int32 FIndexedCurve::GetIndexSafe(FKeyHandle KeyHandle) const
{
	return IsKeyHandleValid(KeyHandle) ? *KeyHandlesToIndices.Find(KeyHandle) : INDEX_NONE;
}


int32 FIndexedCurve::GetIndex(FKeyHandle KeyHandle) const
{
	return *KeyHandlesToIndices.Find(KeyHandle);
}


FKeyHandle FIndexedCurve::GetKeyHandle(int32 KeyIndex) const
{
	check(KeyIndex >= 0 && KeyIndex < GetNumKeys());
	EnsureIndexHasAHandle(KeyIndex);

	return *KeyHandlesToIndices.FindKey(KeyIndex);
}


TMap<FKeyHandle, int32>::TConstIterator FIndexedCurve::GetKeyHandleIterator() const
{
	EnsureAllIndicesHaveHandles();
	return KeyHandlesToIndices.CreateConstIterator();
}


bool FIndexedCurve::IsKeyHandleValid(FKeyHandle KeyHandle) const
{
	EnsureAllIndicesHaveHandles();
	return KeyHandlesToIndices.Find(KeyHandle) != NULL;
}


void FIndexedCurve::EnsureIndexHasAHandle(int32 KeyIndex) const
{
	const FKeyHandle* KeyHandle = KeyHandlesToIndices.FindKey(KeyIndex);
	if (!KeyHandle)
	{
		FKeyHandle OutKeyHandle = FKeyHandle();
		KeyHandlesToIndices.Add(OutKeyHandle, KeyIndex);
	}
}


void FIndexedCurve::EnsureAllIndicesHaveHandles() const
{
	if (KeyHandlesToIndices.Num() != GetNumKeys())
	{
		for (int32 i = 0; i < GetNumKeys(); ++i)
		{
			EnsureIndexHasAHandle(i);
		}
	}
}


/* FNameCurveKey
 *****************************************************************************/

bool FNameCurveKey::operator==(const FNameCurveKey& Curve) const
{
	return ((Time == Curve.Time) && (Value == Curve.Value));
}


bool FNameCurveKey::operator!=(const FNameCurveKey& Other) const
{
	return !(*this == Other);
}


bool FNameCurveKey::Serialize(FArchive& Ar)
{
	Ar << Time << Value;
	return true;
}


/* FNameCurve
 *****************************************************************************/

FKeyHandle FNameCurve::AddKey(float InTime, const FName& InValue, FKeyHandle KeyHandle)
{
	int32 Index = 0;

	// insert key
	for(; Index < Keys.Num() && Keys[Index].Time < InTime; ++Index);
	Keys.Insert(FNameCurveKey(InTime, InValue), Index);

	// update key indices
	for ( auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		int32& KeyIndex = It.Value();
		if (KeyIndex >= Index) {++KeyIndex;}
	}

	KeyHandlesToIndices.Add(KeyHandle, Index);

	return GetKeyHandle(Index);
}


void FNameCurve::DeleteKey(FKeyHandle KeyHandle)
{
	// remove key
	int32 Index = GetIndex(KeyHandle);
	Keys.RemoveAt(Index);

	// update key indices
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		int32& KeyIndex = It.Value();
		if (KeyIndex >= Index)
		{
			--KeyIndex;
		}
	}
}


FKeyHandle FNameCurve::FindKey(float KeyTime, float KeyTimeTolerance) const
{
	int32 Start = 0;
	int32 End = Keys.Num()-1;

	// Binary search since the keys are in sorted order
	while (Start <= End)
	{
		int32 TestPos = Start + (End-Start) / 2;
		float TestKeyTime = Keys[TestPos].Time;

		if (FMath::IsNearlyEqual(TestKeyTime, KeyTime, KeyTimeTolerance))
		{
			return GetKeyHandle(TestPos);
		}

		if (TestKeyTime < KeyTime)
		{
			Start = TestPos+1;
		}
		else
		{
			End = TestPos-1;
		}
	}

	return FKeyHandle();
}


FNameCurveKey& FNameCurve::GetKey(FKeyHandle KeyHandle)
{
	EnsureAllIndicesHaveHandles();
	return Keys[GetIndex(KeyHandle)];
}


FNameCurveKey FNameCurve::GetKey(FKeyHandle KeyHandle) const
{
	EnsureAllIndicesHaveHandles();
	return Keys[GetIndex(KeyHandle)];
}


float FNameCurve::GetKeyTime(FKeyHandle KeyHandle) const
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return 0.f;
	}

	return GetKey(KeyHandle).Time;
}


FKeyHandle FNameCurve::SetKeyTime(FKeyHandle KeyHandle, float NewTime)
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return KeyHandle;
	}

	FNameCurveKey OldKey = GetKey(KeyHandle);
	
	DeleteKey(KeyHandle);
	AddKey(NewTime, OldKey.Value, KeyHandle);

	// Copy all properties from old key, but then fix time to be the new time
	GetKey(KeyHandle) = OldKey;
	GetKey(KeyHandle).Time = NewTime;

	return KeyHandle;
}


void FNameCurve::ShiftCurve(float DeltaTime)
{
	TSet<FKeyHandle> KeyHandles;
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		FKeyHandle& KeyHandle = It.Key();
		KeyHandles.Add(KeyHandle);
	}

	ShiftCurve(DeltaTime, KeyHandles);
}


void FNameCurve::ShiftCurve(float DeltaTime, TSet<FKeyHandle>& KeyHandles)
{
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		const FKeyHandle& KeyHandle = It.Key();
		if (KeyHandles.Num() != 0 && KeyHandles.Contains(KeyHandle))
		{
			SetKeyTime(KeyHandle, GetKeyTime(KeyHandle) + DeltaTime);
		}
	}
}


void FNameCurve::ScaleCurve(float ScaleOrigin, float ScaleFactor)
{
	TSet<FKeyHandle> KeyHandles;
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		FKeyHandle& KeyHandle = It.Key();
		KeyHandles.Add(KeyHandle);
	}

	ScaleCurve(ScaleOrigin, ScaleFactor, KeyHandles);
}


void FNameCurve::ScaleCurve(float ScaleOrigin, float ScaleFactor, TSet<FKeyHandle>& KeyHandles)
{
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		const FKeyHandle& KeyHandle = It.Key();
		if (KeyHandles.Num() != 0 && KeyHandles.Contains(KeyHandle))
		{
			SetKeyTime(KeyHandle, (GetKeyTime(KeyHandle) - ScaleOrigin) * ScaleFactor + ScaleOrigin);
		}
	}
}


FKeyHandle FNameCurve::UpdateOrAddKey(float InTime, const FName& InValue, float KeyTimeTolerance)
{
	// Search for a key that already exists at the time and if found, update its value
	for (int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex)
	{
		float KeyTime = Keys[KeyIndex].Time;

		if (FMath::IsNearlyEqual(KeyTime, InTime, KeyTimeTolerance))
		{
			Keys[KeyIndex].Value = InValue;

			return GetKeyHandle(KeyIndex);
		}

		if (KeyTime > InTime)
		{
			// All the rest of the keys exist after the key we want to add
			// so there is no point in searching
			break;
		}
	}

	// A key wasnt found, add it now
	return AddKey(InTime, InValue);
}


int32 FNameCurve::GetNumKeys() const
{
	return Keys.Num();
}


bool FNameCurve::IsKeyHandleValid(FKeyHandle KeyHandle) const
{
	bool bValid = false;

	if (FIndexedCurve::IsKeyHandleValid(KeyHandle))
	{
		bValid = Keys.IsValidIndex(GetIndex(KeyHandle));
	}

	return bValid;
}


/* FRichCurveKey
 *****************************************************************************/

static void SetModesFromLegacy(FRichCurveKey& InKey, EInterpCurveMode InterpMode)
{
	InKey.InterpMode = RCIM_Linear;
	InKey.TangentWeightMode = RCTWM_WeightedNone;
	InKey.TangentMode = RCTM_Auto;

	if (InterpMode == CIM_Constant)
	{
		InKey.InterpMode = RCIM_Constant;
	}
	else if (InterpMode == CIM_Linear)
	{
		InKey.InterpMode = RCIM_Linear;
	}
	else
	{
		InKey.InterpMode = RCIM_Cubic;

		if (InterpMode == CIM_CurveAuto || InterpMode == CIM_CurveAutoClamped)
		{
			InKey.TangentMode = RCTM_Auto;
		}
		else if (InterpMode == CIM_CurveBreak)
		{
			InKey.TangentMode = RCTM_Break;
		}
		else if (InterpMode == CIM_CurveUser)
		{
			InKey.TangentMode = RCTM_User;
		}
	}
}


FRichCurveKey::FRichCurveKey(const FInterpCurvePoint<float>& InPoint)
{
	SetModesFromLegacy(*this, InPoint.InterpMode);

	Time = InPoint.InVal;
	Value = InPoint.OutVal;

	ArriveTangent = InPoint.ArriveTangent;
	ArriveTangentWeight = 0.f;

	LeaveTangent = InPoint.LeaveTangent;
	LeaveTangentWeight = 0.f;
}


FRichCurveKey::FRichCurveKey(const FInterpCurvePoint<FVector>& InPoint, int32 ComponentIndex)
{
	SetModesFromLegacy(*this, InPoint.InterpMode);

	Time = InPoint.InVal;

	if (ComponentIndex == 0)
	{
		Value = InPoint.OutVal.X;
		ArriveTangent = InPoint.ArriveTangent.X;
		LeaveTangent = InPoint.LeaveTangent.X;
	}
	else if (ComponentIndex == 1)
	{
		Value = InPoint.OutVal.Y;
		ArriveTangent = InPoint.ArriveTangent.Y;
		LeaveTangent = InPoint.LeaveTangent.Y;
	}
	else
	{
		Value = InPoint.OutVal.Z;
		ArriveTangent = InPoint.ArriveTangent.Z;
		LeaveTangent = InPoint.LeaveTangent.Z;
	}

	ArriveTangentWeight = 0.f;
	LeaveTangentWeight = 0.f;
}


bool FRichCurveKey::Serialize(FArchive& Ar)
{
	if (Ar.UE4Ver() < VER_UE4_SERIALIZE_RICH_CURVE_KEY)
	{
		return false;
	}

	// Serialization is handled manually to avoid the extra size overhead of UProperty tagging.
	// Otherwise with many keys in a rich curve the size can become quite large.
	Ar << InterpMode;
	Ar << TangentMode;
	Ar << TangentWeightMode;
	Ar << Time;
	Ar << Value;
	Ar << ArriveTangent;
	Ar << ArriveTangentWeight;
	Ar << LeaveTangent;
	Ar << LeaveTangentWeight;

	return true;
}


bool FRichCurveKey::operator==( const FRichCurveKey& Curve ) const
{
	return (Time == Curve.Time) && (Value == Curve.Value) && (InterpMode == Curve.InterpMode) &&
		   (TangentMode == Curve.TangentMode) && (TangentWeightMode == Curve.TangentWeightMode) &&
		   ((InterpMode != RCIM_Cubic) || //also verify if it is cubic that tangents are the same
		   ((ArriveTangent == Curve.ArriveTangent) && (LeaveTangent == Curve.LeaveTangent) ));

}


bool FRichCurveKey::operator!=(const FRichCurveKey& Other) const
{
	return !(*this == Other);
}


FKeyHandle::FKeyHandle()
{
	static uint32 LastKeyHandleIndex = 1;
	check(LastKeyHandleIndex != 0); // check in the unlikely event that this overflows

	Index = ++LastKeyHandleIndex;
}


/* FRichCurve
 *****************************************************************************/

TArray<FRichCurveKey> FRichCurve::GetCopyOfKeys() const
{
	return Keys;
}


TArray<FRichCurveKey>::TConstIterator FRichCurve::GetKeyIterator() const
{
	return Keys.CreateConstIterator();
}


FRichCurveKey& FRichCurve::GetKey(FKeyHandle KeyHandle)
{
	EnsureAllIndicesHaveHandles();
	return Keys[GetIndex(KeyHandle)];
}


FRichCurveKey FRichCurve::GetKey(FKeyHandle KeyHandle) const
{
	EnsureAllIndicesHaveHandles();
	return Keys[GetIndex(KeyHandle)];
}


FRichCurveKey FRichCurve::GetFirstKey() const
{
	check(Keys.Num() > 0);
	return Keys[0];
}


FRichCurveKey FRichCurve::GetLastKey() const
{
	check(Keys.Num() > 0);
	return Keys[Keys.Num()-1];
}


FKeyHandle FRichCurve::GetNextKey(FKeyHandle KeyHandle) const
{
	int32 KeyIndex = GetIndex(KeyHandle);
	KeyIndex++;

	if (Keys.IsValidIndex(KeyIndex))
	{
		return GetKeyHandle(KeyIndex);
	}

	return FKeyHandle();
}


FKeyHandle FRichCurve::GetPreviousKey(FKeyHandle KeyHandle) const
{
	int32 KeyIndex = GetIndex(KeyHandle);
	KeyIndex--;

	if (Keys.IsValidIndex(KeyIndex))
	{
		return GetKeyHandle(KeyIndex);
	}

	return FKeyHandle();
}


int32 FRichCurve::GetNumKeys() const
{
	return Keys.Num();
}


bool FRichCurve::IsKeyHandleValid(FKeyHandle KeyHandle) const
{
	bool bValid = false;

	if (FIndexedCurve::IsKeyHandleValid(KeyHandle))
	{
		bValid = Keys.IsValidIndex( GetIndex(KeyHandle) );
	}

	return bValid;
}


FKeyHandle FRichCurve::AddKey( const float InTime, const float InValue, const bool bUnwindRotation, FKeyHandle NewHandle )
{
	int32 Index = 0;
	for(; Index < Keys.Num() && Keys[Index].Time < InTime; ++Index);
	Keys.Insert(FRichCurveKey(InTime, InValue), Index);

	// If we were asked to treat this curve as a rotation value and to unwindow the rotation, then
	// we'll look at the previous key and modify the key's value to use a rotation angle that is
	// continuous with the previous key while retaining the exact same rotation angle, if at all necessary
	if( Index > 0 && bUnwindRotation )
	{
		const float OldValue = Keys[ Index - 1 ].Value;
		float NewValue = Keys[ Index ].Value;

		while( NewValue - OldValue > 180.0f )
		{
			NewValue -= 360.0f;
		}
		while( NewValue - OldValue < -180.0f )
		{
			NewValue += 360.0f;
		}

		Keys[Index].Value = NewValue;
	}
	
	for ( auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		const FKeyHandle& KeyHandle = It.Key();
		int32& KeyIndex = It.Value();

		if (KeyIndex >= Index) {++KeyIndex;}
	}

	KeyHandlesToIndices.Add(NewHandle, Index);

	return GetKeyHandle(Index);
}


void FRichCurve::DeleteKey(FKeyHandle InKeyHandle)
{
	int32 Index = GetIndex(InKeyHandle);
	
	Keys.RemoveAt(Index);
	AutoSetTangents();

	KeyHandlesToIndices.Remove(InKeyHandle);

	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		const FKeyHandle& KeyHandle = It.Key();
		int32& KeyIndex = It.Value();

		if (KeyIndex >= Index)
		{
			--KeyIndex;
		}
	}
}


FKeyHandle FRichCurve::UpdateOrAddKey(float InTime, float InValue, const bool bUnwindRotation, float KeyTimeTolerance)
{
	// Search for a key that already exists at the time and if found, update its value
	for (int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex)
	{
		float KeyTime = Keys[KeyIndex].Time;

		if (FMath::IsNearlyEqual(KeyTime, InTime, KeyTimeTolerance))
		{
			Keys[KeyIndex].Value = InValue;

			return GetKeyHandle(KeyIndex);
		}

		if (KeyTime > InTime)
		{
			// All the rest of the keys exist after the key we want to add
			// so there is no point in searching
			break;
		}
	}

	// A key wasnt found, add it now
	return AddKey(InTime, InValue, bUnwindRotation);
}


FKeyHandle FRichCurve::SetKeyTime( FKeyHandle KeyHandle, float NewTime )
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return KeyHandle;
	}

	FRichCurveKey OldKey = GetKey(KeyHandle);
	
	DeleteKey(KeyHandle);
	AddKey(NewTime, OldKey.Value, false, KeyHandle);

	// Copy all properties from old key, but then fix time to be the new time
	GetKey(KeyHandle) = OldKey;
	GetKey(KeyHandle).Time = NewTime;

	return KeyHandle;
}


float FRichCurve::GetKeyTime(FKeyHandle KeyHandle) const
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return 0.f;
	}

	return GetKey(KeyHandle).Time;
}


FKeyHandle FRichCurve::FindKey(float KeyTime, float KeyTimeTolerance) const
{
	int32 Start = 0;
	int32 End = Keys.Num()-1;

	// Binary search since the keys are in sorted order
	while (Start <= End)
	{
		int32 TestPos = Start + (End-Start) / 2;
		float TestKeyTime = Keys[TestPos].Time;

		if (FMath::IsNearlyEqual(TestKeyTime, KeyTime, KeyTimeTolerance))
		{
			return GetKeyHandle(TestPos);
		}
		else if(TestKeyTime < KeyTime)
		{
			Start = TestPos+1;
		}
		else
		{
			End = TestPos-1;
		}
	}

	return FKeyHandle();
}


void FRichCurve::SetKeyValue(FKeyHandle KeyHandle, float NewValue, bool bAutoSetTangents)
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return;
	}

	GetKey(KeyHandle).Value = NewValue;

	if (bAutoSetTangents)
	{
		AutoSetTangents();
	}
}


float FRichCurve::GetKeyValue(FKeyHandle KeyHandle) const
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return 0.f;
	}

	return GetKey(KeyHandle).Value;
}


void FRichCurve::ShiftCurve(float DeltaTime)
{
	TSet<FKeyHandle> KeyHandles;
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		FKeyHandle& KeyHandle = It.Key();
		KeyHandles.Add(KeyHandle);
	}

	ShiftCurve(DeltaTime, KeyHandles);
}


void FRichCurve::ShiftCurve(float DeltaTime, TSet<FKeyHandle>& KeyHandles)
{
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		const FKeyHandle& KeyHandle = It.Key();
		if (KeyHandles.Num() != 0 && KeyHandles.Contains(KeyHandle))
		{
			SetKeyTime(KeyHandle, GetKeyTime(KeyHandle)+DeltaTime);
		}
	}
}


void FRichCurve::ScaleCurve(float ScaleOrigin, float ScaleFactor)
{
	TSet<FKeyHandle> KeyHandles;
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		FKeyHandle& KeyHandle = It.Key();
		KeyHandles.Add(KeyHandle);
	}

	ScaleCurve(ScaleOrigin, ScaleFactor, KeyHandles);
}


void FRichCurve::ScaleCurve(float ScaleOrigin, float ScaleFactor, TSet<FKeyHandle>& KeyHandles)
{
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		const FKeyHandle& KeyHandle = It.Key();
		if (KeyHandles.Num() != 0 && KeyHandles.Contains(KeyHandle))
		{
			SetKeyTime(KeyHandle, (GetKeyTime(KeyHandle) - ScaleOrigin) * ScaleFactor + ScaleOrigin);
		}
	}
}


void FRichCurve::SetKeyInterpMode(FKeyHandle KeyHandle, ERichCurveInterpMode NewInterpMode)
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return;
	}

	GetKey(KeyHandle).InterpMode = NewInterpMode;
	AutoSetTangents();
}


void FRichCurve::SetKeyTangentMode(FKeyHandle KeyHandle, ERichCurveTangentMode NewTangentMode)
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return;
	}

	GetKey(KeyHandle).TangentMode = NewTangentMode;
	AutoSetTangents();
}


void FRichCurve::SetKeyTangentWeightMode(FKeyHandle KeyHandle, ERichCurveTangentWeightMode NewTangentWeightMode)
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return;
	}

	GetKey(KeyHandle).TangentWeightMode = NewTangentWeightMode;
	AutoSetTangents();
}


ERichCurveInterpMode FRichCurve::GetKeyInterpMode(FKeyHandle KeyHandle) const
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return RCIM_Linear;
	}

	return GetKey(KeyHandle).InterpMode;
}


ERichCurveTangentMode FRichCurve::GetKeyTangentMode(FKeyHandle KeyHandle) const
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return RCTM_Auto;
	}

	return GetKey(KeyHandle).TangentMode;
}


void FRichCurve::GetTimeRange(float& MinTime, float& MaxTime) const
{
	if (Keys.Num() == 0)
	{
		MinTime = 0.f;
		MaxTime = 0.f;
	}
	else
	{
		MinTime = Keys[0].Time;
		MaxTime = Keys[Keys.Num()-1].Time;
	}
}


/*	 Finds min/max for cubic curves:
	Looks for feature points in the signal(determined by change in direction of local tangent), these locations are then re-examined in closer detail recursively */
template<class T>
void FeaturePointMethod(T& Function , float StartTime,  float EndTime, float StartValue,float Mu, int Depth, int MaxDepth, float& MaxV, float& MinVal)
{
	if (Depth >= MaxDepth)
	{
		return;
	}

	float PrevValue   = StartValue;
	float PrevTangent = StartValue - Function.Eval(StartTime-Mu);
	EndTime += Mu;

	for (float f = StartTime + Mu;f < EndTime; f += Mu)
	{
		float Value		 = Function.Eval(f);

		MaxV   = FMath::Max(Value, MaxV);
		MinVal = FMath::Min(Value, MinVal);

		float CurTangent = Value - PrevValue;
		
		//Change direction? Examine this area closer
		if (FMath::Sign(CurTangent) != FMath::Sign(PrevTangent))
		{
			//feature point centered around the previous tangent
			float FeaturePointTime = f-Mu*2.0f;
			FeaturePointMethod(Function, FeaturePointTime, f, Function.Eval(FeaturePointTime), Mu*0.4f,Depth+1, MaxDepth, MaxV, MinVal);
		}

		PrevTangent = CurTangent;
		PrevValue = Value;
	}
}


void FRichCurve::GetValueRange(float& MinValue, float& MaxValue) const
{
	if (Keys.Num() == 0)
	{
		MinValue = MaxValue = 0.f;
	}
	else
	{
		int32 LastKeyIndex = Keys.Num()-1;
		MinValue = MaxValue = Keys[0].Value;

		for (int32 i = 0; i < Keys.Num(); i++)
		{
			const FRichCurveKey& Key = Keys[i];

			MinValue = FMath::Min(MinValue, Key.Value);
			MaxValue = FMath::Max(MaxValue, Key.Value);

			if (Key.InterpMode == RCIM_Cubic && i != LastKeyIndex)
			{
				const FRichCurveKey& NextKey = Keys[i+1];
				float TimeStep = (NextKey.Time - Key.Time) * 0.2f;

				FeaturePointMethod(*this, Key.Time, NextKey.Time, Key.Value, TimeStep, 0, 3, MaxValue, MinValue);
			}
		}
	}
}


void FRichCurve::Reset()
{
	Keys.Empty();
	KeyHandlesToIndices.Empty();
}


void FRichCurve::AutoSetTangents(float Tension)
{
	// Iterate over all points in this InterpCurve
	for (int32 KeyIndex = 0; KeyIndex<Keys.Num(); KeyIndex++)
	{
		FRichCurveKey& Key = Keys[KeyIndex];
		float ArriveTangent = Key.ArriveTangent;
		float LeaveTangent  = Key.LeaveTangent;

		if (KeyIndex == 0)
		{
			if (KeyIndex < Keys.Num()-1) // Start point
			{
				// If first section is not a curve, or is a curve and first point has manual tangent setting.
				if (Key.TangentMode == RCTM_Auto)
				{
					LeaveTangent = 0.0f;
				}
			}
		}
		else
		{
			
			if (KeyIndex < Keys.Num() - 1) // Inner point
			{
				FRichCurveKey& PrevKey =  Keys[KeyIndex-1];

				if (Key.InterpMode == RCIM_Cubic && (Key.TangentMode == RCTM_Auto))
				{
						FRichCurveKey& NextKey =  Keys[KeyIndex+1];
						ComputeCurveTangent(
							Keys[ KeyIndex - 1 ].Time,		// Previous time
							Keys[ KeyIndex - 1 ].Value,	// Previous point
							Keys[ KeyIndex ].Time,			// Current time
							Keys[ KeyIndex ].Value,		// Current point
							Keys[ KeyIndex + 1 ].Time,		// Next time
							Keys[ KeyIndex + 1 ].Value,	// Next point
							Tension,							// Tension
							false,						// Want clamping?
							ArriveTangent );					// Out

						// In 'auto' mode, arrive and leave tangents are always the same
						LeaveTangent = ArriveTangent;
				}
				else if ((PrevKey.InterpMode == RCIM_Constant) || (Key.InterpMode == RCIM_Constant))
				{
					if (Keys[ KeyIndex - 1 ].InterpMode != RCIM_Cubic)
					{
						ArriveTangent = 0.0f;
					}

					LeaveTangent  = 0.0f;
				}
				
			}
			else // End point
			{
				// If last section is not a curve, or is a curve and final point has manual tangent setting.
				if (Key.InterpMode == RCIM_Cubic && Key.TangentMode == RCTM_Auto)
				{
					ArriveTangent = 0.0f;
				}
			}
		}

		Key.ArriveTangent = ArriveTangent;
		Key.LeaveTangent = LeaveTangent;
	}
}


void FRichCurve::ReadjustTimeRange(float NewMinTimeRange, float NewMaxTimeRange, bool bInsert/* whether insert or remove*/, float OldStartTime, float OldEndTime)
{
	// first readjust modified time keys
	float ModifiedDuration = OldEndTime - OldStartTime;

	if (bInsert)
	{
		for(int32 KeyIndex=0; KeyIndex<Keys.Num(); ++KeyIndex)
		{
			float& CurrentTime = Keys[KeyIndex].Time;
			if (CurrentTime >= OldStartTime)
			{
				CurrentTime += ModifiedDuration;
			}
		}
	}
	else
	{
		// since we only allow one key at a given time, we will just cache the value that needs to be saved
		// this is the key to be replaced when this section is gone
		bool bAddNewKey = false; 
		float NewValue = 0.f;
		TArray<int32> KeysToDelete;

		for(int32 KeyIndex=0; KeyIndex<Keys.Num(); ++KeyIndex)
		{
			float& CurrentTime = Keys[KeyIndex].Time;
			// if this key exists between range of deleted
			// we'll evaluate the value at the "OldStartTime"
			// and re-add key, so that it keeps the previous value at the
			// start time
			// But that means if there are multiple keys, 
			// since we don't want multiple values in the same time
			// the last one will override the value
			if( CurrentTime >= OldStartTime && CurrentTime <= OldEndTime)
			{
				// get new value and add new key on one of OldStartTime, OldEndTime;
				// this is a bit complicated problem since we don't know if OldStartTime or OldEndTime is preferred. 
				// generall we use OldEndTime unless OldStartTime == 0.f
				// which means it's cut in the beginning. Otherwise it will always use the end time. 
				bAddNewKey = true;
				if (OldStartTime != 0.f)
				{
					NewValue = Eval(OldStartTime);
				}
				else
				{
					NewValue = Eval(OldEndTime);
				}
				// remove this key, but later because it might change eval result
				KeysToDelete.Add(KeyIndex);
			}
			else if (CurrentTime > OldEndTime)
			{
				CurrentTime -= ModifiedDuration;
			}
		}

		if (bAddNewKey)
		{
			for (auto KeyIndex : KeysToDelete)
			{
				const FKeyHandle* KeyHandle = KeyHandlesToIndices.FindKey(KeyIndex);
				if(KeyHandle)
				{
					DeleteKey(*KeyHandle);
				}
			}

			UpdateOrAddKey(OldStartTime, NewValue);
		}
	}

	// now remove all redundant key
	TArray<FRichCurveKey> NewKeys;
	Exchange(NewKeys, Keys);

	for(int32 KeyIndex=0; KeyIndex<NewKeys.Num(); ++KeyIndex)
	{
		UpdateOrAddKey(NewKeys[KeyIndex].Time, NewKeys[KeyIndex].Value);
	}

	// now cull out all out of range 
	float MinTime, MaxTime;
	GetTimeRange(MinTime, MaxTime);

	bool bNeedToDeleteKey=false;

	// if there is key below min time, just add key at new min range, 
	if (MinTime < NewMinTimeRange)
	{
		float NewValue = Eval(NewMinTimeRange);
		UpdateOrAddKey(NewMinTimeRange, NewValue);

		bNeedToDeleteKey = true;
	}

	// if there is key after max time, just add key at new max range, 
	if(MaxTime > NewMaxTimeRange)
	{
		float NewValue = Eval(NewMaxTimeRange);
		UpdateOrAddKey(NewMaxTimeRange, NewValue);

		bNeedToDeleteKey = true;
	}

	// delete the keys outside of range
	if (bNeedToDeleteKey)
	{
		for (int32 KeyIndex=0; KeyIndex<Keys.Num(); ++KeyIndex)
		{
			if (Keys[KeyIndex].Time < NewMinTimeRange || Keys[KeyIndex].Time > NewMaxTimeRange)
			{
				const FKeyHandle* KeyHandle = KeyHandlesToIndices.FindKey(KeyIndex);
				if (KeyHandle)
				{
					DeleteKey(*KeyHandle);
					--KeyIndex;
				}
			}
		}
	}
}


/** Util to find float value on bezier defined by 4 control points */ 
static float BezierInterp(float P0, float P1, float P2, float P3, float Alpha)
{
	const float P01 = FMath::Lerp(P0, P1, Alpha);
	const float P12 = FMath::Lerp(P1, P2, Alpha);
	const float P23 = FMath::Lerp(P2, P3, Alpha);
	const float P012 = FMath::Lerp(P01, P12, Alpha);
	const float P123 = FMath::Lerp(P12, P23, Alpha);
	const float P0123 = FMath::Lerp(P012, P123, Alpha);

	return P0123;
}


static float BezierInterp2(float P0, float Y1, float Y2, float P3, float mu)
{
	float P1 = (-5.f * P0 + 18.f * Y1 - 9.f * Y2 + 2.f * P3) / 6.f;
	float P2 = (2.f * P0 -  9.f * Y1 + 18.f * Y2 - 5.f * P3) / 6.f;
	float A = P3 - 3.0f * P2 + 3.0f * P1 - P0;
	float B = 3.0f * P2 - 6.0f * P1 + 3.0f * P0;
	float C = 3.0f * P1 - 3.0f * P0;
	float D = P0;
	float Result = A * (mu * mu * mu) + B * (mu * mu) + C * mu + D;

	return Result;
}


static void CycleTime(float MinTime, float MaxTime, float& InTime, int& CycleCount)
{
	float InitTime = InTime;
	float Duration = MaxTime - MinTime;

	if (InTime > MaxTime)
	{
		CycleCount = FMath::FloorToInt((MaxTime-InTime)/Duration);
		InTime = InTime + Duration*CycleCount;
	}
	else if (InTime < MinTime)
	{
		CycleCount = FMath::FloorToInt((InTime-MinTime)/Duration);
		InTime = InTime - Duration*CycleCount;
	}

	if (InTime == MaxTime && InitTime < MinTime)
	{
		InTime = MinTime;
	}

	if (InTime == MinTime && InitTime > MaxTime)
	{
		InTime = MaxTime;
	}

	CycleCount = FMath::Abs(CycleCount);
}


void FRichCurve::RemapTimeValue(float& InTime, float& CycleValueOffset) const
{
	const int32 NumKeys = Keys.Num();
	
	if (NumKeys < 2)
	{
		return;
	} 

	if (InTime <= Keys[0].Time)
	{
		if (PreInfinityExtrap != RCCE_Linear && PreInfinityExtrap != RCCE_Constant)
		{
			float MinTime = Keys[0].Time;
			float MaxTime = Keys[NumKeys - 1].Time;

			int CycleCount = 0;
			CycleTime(MinTime, MaxTime, InTime, CycleCount);

			if (PreInfinityExtrap == RCCE_CycleWithOffset)
			{
				float DV = Keys[0].Value - Keys[NumKeys - 1].Value;
				CycleValueOffset = DV * CycleCount;
			}
			else if (PreInfinityExtrap == RCCE_Oscillate)
			{
				if (CycleCount % 2 == 1)
				{
					InTime = MinTime + (MaxTime - InTime);
				}
			}
		}
	}
	else if (InTime >= Keys[NumKeys - 1].Time)
	{
		if (PostInfinityExtrap != RCCE_Linear && PostInfinityExtrap != RCCE_Constant)
		{
			float MinTime = Keys[0].Time;
			float MaxTime = Keys[NumKeys - 1].Time;

			int CycleCount = 0; 
			CycleTime(MinTime, MaxTime, InTime, CycleCount);

			if (PostInfinityExtrap == RCCE_CycleWithOffset)
			{
				float DV = Keys[NumKeys - 1].Value - Keys[0].Value;
				CycleValueOffset = DV * CycleCount;
			}
			else if (PostInfinityExtrap == RCCE_Oscillate)
			{
				if (CycleCount % 2 == 1)
				{
					InTime = MinTime + (MaxTime - InTime);
				}
			}
		}
	}
}


float FRichCurve::Eval(float InTime, float InDefaultValue) const
{
	SCOPE_CYCLE_COUNTER(STAT_RichCurve_Eval);

	// Remap time if extrapolation is present and compute offset value to use if cycling 
	float CycleValueOffset = 0;
	RemapTimeValue(InTime, CycleValueOffset);

	const int32 NumKeys = Keys.Num();

	// If the default value hasn't been initialized, use the incoming default value
	float InterpVal = DefaultValue == MAX_flt ? InDefaultValue : DefaultValue;

	if (NumKeys == 0)
	{
		// If no keys in curve, return the Default value.
	} 
	else if (NumKeys < 2 || (InTime <= Keys[0].Time))
	{
		if (PreInfinityExtrap == RCCE_Linear && NumKeys > 1)
		{
			float DT = Keys[1].Time - Keys[0].Time;
			
			if (FMath::IsNearlyZero(DT))
			{
				InterpVal = Keys[0].Value;
			}
			else
			{
				float DV = Keys[1].Value - Keys[0].Value;
				float Slope = DV / DT;

				InterpVal = Slope * (InTime - Keys[0].Time) + Keys[0].Value;
			}
		}
		else
		{
			// Otherwise if constant or in a cycle or oscillate, always use the first key value
			InterpVal = Keys[0].Value;
		}
	}
	else if (InTime < Keys[NumKeys - 1].Time)
	{
		// perform a lower bound to get the second of the interpolation nodes
		int32 first = 1;
		int32 last = NumKeys - 1;
		int32 count = last - first;

		while (count > 0)
		{
			int32 step = count / 2;
			int32 middle = first + step;

			if (InTime >= Keys[middle].Time)
			{
				first = middle + 1;
				count -= step + 1;
			}
			else
			{
				count = step;
			}
		}

		int32 InterpNode = first;
		const float Diff = Keys[InterpNode].Time - Keys[InterpNode - 1].Time;

		if (Diff > 0.f && Keys[InterpNode - 1].InterpMode != RCIM_Constant)
		{
			const float Alpha = (InTime - Keys[InterpNode - 1].Time) / Diff;
			const float P0 = Keys[InterpNode - 1].Value;
			const float P3 = Keys[InterpNode].Value;

			if (Keys[InterpNode - 1].InterpMode == RCIM_Linear)
			{
				InterpVal = FMath::Lerp(P0, P3, Alpha);
			}
			else
			{
				const float OneThird = 1.0f / 3.0f;
				const float P1 = P0 + (Keys[InterpNode - 1].LeaveTangent * Diff*OneThird);
				const float P2 = P3 - (Keys[InterpNode].ArriveTangent * Diff*OneThird);

				InterpVal = BezierInterp(P0, P1, P2, P3, Alpha);
			}
		}
		else
		{
			InterpVal = Keys[InterpNode - 1].Value;
		}
	}
	else
	{
		if (PostInfinityExtrap == RCCE_Linear)
		{
			float DT = Keys[NumKeys - 2].Time - Keys[NumKeys - 1].Time;
			
			if (FMath::IsNearlyZero(DT))
			{
				InterpVal = Keys[NumKeys - 1].Value;
			}
			else
			{
				float DV = Keys[NumKeys - 2].Value - Keys[NumKeys - 1].Value;
				float Slope = DV / DT;

				InterpVal = Slope * (InTime - Keys[NumKeys - 1].Time) + Keys[NumKeys - 1].Value;
			}
		}
		else
		{
			// Otherwise if constant or in a cycle or oscillate, always use the last key value
			InterpVal = Keys[NumKeys - 1].Value;
		}
	}

	return InterpVal+CycleValueOffset;
}


bool FRichCurve::operator==(const FRichCurve& Curve) const
{
	if(Keys.Num() != Curve.Keys.Num())
	{
		return false;
	}

	for(int32 i = 0;i<Keys.Num();++i)
	{
		if(!(Keys[i] == Curve.Keys[i]))
		{
			return false;
		}
	}

	if (PreInfinityExtrap != Curve.PreInfinityExtrap || PostInfinityExtrap != Curve.PostInfinityExtrap)
	{
		return false;
	}

	return true;
}


/* UCurveBase
 *****************************************************************************/

UCurveBase::UCurveBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{ }


#if WITH_EDITORONLY_DATA
void UCurveBase::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	if (AssetImportData)
	{
		OutTags.Add(FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden));
	}

	Super::GetAssetRegistryTags(OutTags);
}


void UCurveBase::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}

	Super::PostInitProperties();
}


void UCurveBase::PostLoad()
{
	Super::PostLoad();
	if (!ImportPath_DEPRECATED.IsEmpty() && AssetImportData)
	{
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(ImportPath_DEPRECATED));
		AssetImportData->SourceData = MoveTemp(Info);
	}
}
#endif


void UCurveBase::GetTimeRange(float& MinTime, float& MaxTime) const
{
	TArray<FRichCurveEditInfoConst> Curves = GetCurves();
	if(Curves.Num() > 0)
	{
		check(Curves[0].CurveToEdit);
		Curves[0].CurveToEdit->GetTimeRange(MinTime, MaxTime);

		for(int32 i=1; i<Curves.Num(); i++)
		{
			float CurveMin, CurveMax;
			check(Curves[i].CurveToEdit != NULL);
			Curves[i].CurveToEdit->GetTimeRange(CurveMin, CurveMax);

			MinTime = FMath::Min(CurveMin, MinTime);
			MaxTime = FMath::Max(CurveMax, MaxTime);
		}
	}
}


void UCurveBase::GetValueRange(float& MinValue, float& MaxValue) const
{
	TArray<FRichCurveEditInfoConst> Curves = GetCurves();
	if(Curves.Num() > 0)
	{
		check(Curves[0].CurveToEdit);
		Curves[0].CurveToEdit->GetValueRange(MinValue, MaxValue);

		for(int32 i=1; i<Curves.Num(); i++)
		{
			float CurveMin, CurveMax;
			check(Curves[i].CurveToEdit != NULL);
			Curves[i].CurveToEdit->GetValueRange(CurveMin, CurveMax);

			MinValue = FMath::Min(CurveMin, MinValue);
			MaxValue = FMath::Max(CurveMax, MaxValue);
		}
	}
}


void UCurveBase::ModifyOwner() 
{
	Modify(true);
}


void UCurveBase::MakeTransactional() 
{
	SetFlags(GetFlags() | RF_Transactional);
}


void UCurveBase::OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos)
{
}


void UCurveBase::ResetCurve()
{
	TArray<FRichCurveEditInfo> Curves = GetCurves();

	for(int32 CurveIdx=0; CurveIdx<Curves.Num(); CurveIdx++)
	{
		if(Curves[CurveIdx].CurveToEdit != NULL)
		{
			Curves[CurveIdx].CurveToEdit->Reset();
		}
	}
}


TArray<FString> UCurveBase::CreateCurveFromCSVString(const FString& InString)
{
	// Array used to store problems about curve import
	TArray<FString> OutProblems;

	TArray<FRichCurveEditInfo> Curves = GetCurves();
	const int32 NumCurves = Curves.Num();

	const FCsvParser Parser(InString);
	const FCsvParser::FRows& Rows = Parser.GetRows();

	if(Rows.Num() == 0)
	{
		OutProblems.Add(FString(TEXT("No data.")));
		return OutProblems;
	}

	// First clear out old data.
	ResetCurve();

	// Each row represents a point
	for(int32 RowIdx=0; RowIdx<Rows.Num(); RowIdx++)
	{
		const TArray<const TCHAR*>& Cells = Rows[RowIdx];
		const int32 NumCells = Cells.Num();

		// Need at least two cell, Time and one Value
		if(NumCells < 2)
		{
			OutProblems.Add(FString::Printf(TEXT("Row '%d' has less than 2 cells."), RowIdx));
			continue;
		}

		float Time = FCString::Atof(Cells[0]);
		for(int32 CellIdx=1; CellIdx<NumCells && CellIdx<(NumCurves+1); CellIdx++)
		{
			FRichCurve* Curve = Curves[CellIdx-1].CurveToEdit;
			if(Curve != NULL)
			{
				FKeyHandle KeyHandle = Curve->AddKey(Time, FCString::Atof(Cells[CellIdx]));
				Curve->SetKeyInterpMode(KeyHandle, RCIM_Linear);
			}
		}

		// If we get more cells than curves (+1 for time cell)
		if(NumCells > (NumCurves + 1))
		{
			OutProblems.Add(FString::Printf(TEXT("Row '%d' has too many cells for the curve(s)."), RowIdx));
		}
		// If we got too few cells
		else if(NumCells < (NumCurves + 1))
		{
			OutProblems.Add(FString::Printf(TEXT("Row '%d' has too few cells for the curve(s)."), RowIdx));
		}
	}

	Modify(true);

	return OutProblems;
}


//////////////////////////////////////////////////////////////////////////

int32 FIntegralCurve::GetNumKeys() const
{
	return Keys.Num();
}


bool FIntegralCurve::IsKeyHandleValid(FKeyHandle KeyHandle) const
{
	bool bValid = false;
	if (FIndexedCurve::IsKeyHandleValid(KeyHandle))
	{
		bValid = Keys.IsValidIndex( GetIndex(KeyHandle) );
	}
	return bValid;
}


int32 FIntegralCurve::Evaluate(float Time, int32 InDefaultValue) const
{
	// If the default value hasn't been initialized, use the incoming default value
	int32 ReturnVal = DefaultValue == MAX_int32 ? InDefaultValue : DefaultValue;

	if( Keys.Num() == 0 || (bUseDefaultValueBeforeFirstKey && Time < Keys[0].Time) )
	{
		// If no keys in curve, or bUseDefaultValueBeforeFirstKey is set and the time is before the first key, return the Default value.
	}
	else if( Keys.Num() < 2 || Time < Keys[0].Time )
	{
		// There is only one key or the time is before the first value. Return the first value
		ReturnVal = Keys[0].Value;
	}
	else if( Time < Keys[Keys.Num()-1].Time )
	{
		// The key is in the range of Key[0] to Keys[Keys.Num()-1].  Find it by searching
		for(int32 i = 0; i < Keys.Num(); ++i)
		{
			if(Time < Keys[i].Time)
			{
				ReturnVal = Keys[FMath::Max(0, i - 1)].Value;
				break;
			}
		}
	}
	else
	{
		// Key is beyon the last point in the curve.  Return it's value
		ReturnVal = Keys[Keys.Num() - 1].Value;
	}

	return ReturnVal;
}


TArray<FIntegralKey>::TConstIterator FIntegralCurve::GetKeyIterator() const
{
	return Keys.CreateConstIterator();
}


FKeyHandle FIntegralCurve::AddKey( float InTime, int32 InValue, FKeyHandle InKeyHandle )
{
	int32 Index = 0;
	for(; Index < Keys.Num() && Keys[Index].Time < InTime; ++Index);
	Keys.Insert(FIntegralKey(InTime, InValue), Index);
	
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		int32& KeyIndex = It.Value();
		if (KeyIndex >= Index)
		{
			++KeyIndex;
		}
	}

	KeyHandlesToIndices.Add(InKeyHandle, Index);

	return GetKeyHandle(Index);
}


void FIntegralCurve::DeleteKey(FKeyHandle InKeyHandle)
{
	int32 Index = GetIndex(InKeyHandle);
	
	Keys.RemoveAt(Index);

	KeyHandlesToIndices.Remove(InKeyHandle);

	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		int32& KeyIndex = It.Value();
		if (KeyIndex >= Index)
		{
			--KeyIndex;
		}
	}
}


FKeyHandle FIntegralCurve::UpdateOrAddKey( float InTime, int32 Value, float KeyTimeTolerance )
{
	for( int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex )
	{
		float KeyTime = Keys[KeyIndex].Time;

		if (FMath::IsNearlyEqual(KeyTime, InTime, KeyTimeTolerance))
		{
			Keys[KeyIndex].Value = Value;
			return GetKeyHandle(KeyIndex);
		}

		if( KeyTime > InTime )
		{
			// All the rest of the keys exist after the key we want to add
			// so there is no point in searching
			break;
		}
	}

	// A key wasnt found, add it now
	return AddKey( InTime, Value );
}


FKeyHandle FIntegralCurve::SetKeyTime( FKeyHandle KeyHandle, float NewTime )
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return KeyHandle;
	}

	FIntegralKey OldKey = GetKey(KeyHandle);
	
	DeleteKey(KeyHandle);
	AddKey(NewTime, OldKey.Value, KeyHandle);

	// Copy all properties from old key, but then fix time to be the new time
	GetKey(KeyHandle) = OldKey;
	GetKey(KeyHandle).Time = NewTime;

	return KeyHandle;
}


float FIntegralCurve::GetKeyTime(FKeyHandle KeyHandle) const
{
	if (!IsKeyHandleValid(KeyHandle))
	{
		return 0.f;
	}

	return GetKey(KeyHandle).Time;
}


void FIntegralCurve::ShiftCurve(float DeltaTime)
{
	TSet<FKeyHandle> KeyHandles;
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		FKeyHandle& KeyHandle = It.Key();
		KeyHandles.Add(KeyHandle);
	}

	ShiftCurve(DeltaTime, KeyHandles);
}


void FIntegralCurve::ShiftCurve(float DeltaTime, TSet<FKeyHandle>& KeyHandles)
{
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		const FKeyHandle& KeyHandle = It.Key();
		if (KeyHandles.Num() != 0 && KeyHandles.Contains(KeyHandle))
		{
			SetKeyTime(KeyHandle, GetKeyTime(KeyHandle) + DeltaTime);
		}
	}
}


void FIntegralCurve::ScaleCurve(float ScaleOrigin, float ScaleFactor)
{
	TSet<FKeyHandle> KeyHandles;
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		FKeyHandle& KeyHandle = It.Key();
		KeyHandles.Add(KeyHandle);
	}

	ScaleCurve(ScaleOrigin, ScaleFactor, KeyHandles);
}


void FIntegralCurve::ScaleCurve(float ScaleOrigin, float ScaleFactor, TSet<FKeyHandle>& KeyHandles)
{
	for (auto It = KeyHandlesToIndices.CreateIterator(); It; ++It)
	{
		const FKeyHandle& KeyHandle = It.Key();
		if (KeyHandles.Num() != 0 && KeyHandles.Contains(KeyHandle))
		{
			SetKeyTime(KeyHandle, (GetKeyTime(KeyHandle) - ScaleOrigin) * ScaleFactor + ScaleOrigin);
		}
	}
}


FIntegralKey& FIntegralCurve::GetKey(FKeyHandle KeyHandle)
{
	EnsureAllIndicesHaveHandles();
	return Keys[GetIndex(KeyHandle)];
}


FIntegralKey FIntegralCurve::GetKey(FKeyHandle KeyHandle) const
{
	EnsureAllIndicesHaveHandles();
	return Keys[GetIndex(KeyHandle)];
}


FKeyHandle FIntegralCurve::FindKey(float KeyTime, float KeyTimeTolerance) const
{
	int32 Start = 0;
	int32 End = Keys.Num() - 1;

	// Binary search since the keys are in sorted order
	while (Start <= End)
	{
		int32 TestPos = Start + (End - Start) / 2;
		float TestKeyTime = Keys[TestPos].Time;

		if (FMath::IsNearlyEqual(TestKeyTime, KeyTime, KeyTimeTolerance))
		{
			return GetKeyHandle(TestPos);
		}
		else if (TestKeyTime < KeyTime)
		{
			Start = TestPos + 1;
		}
		else
		{
			End = TestPos - 1;
		}
	}

	return FKeyHandle();
}

FKeyHandle FIntegralCurve::FindKeyBeforeOrAt( float KeyTime ) const
{
	// If there are no keys or the time is before the first key return an invalid handle.
	if ( Keys.Num() == 0 || KeyTime < Keys[0].Time )
	{
		return FKeyHandle();
	}

	// If the time is after or at the last key return the last key.
	if ( KeyTime >= Keys[Keys.Num() - 1].Time )
	{
		return GetKeyHandle(Keys.Num() - 1);
	}

	// Otherwise binary search to find the handle of the nearest key at or before the time.
	int32 Start = 0;
	int32 End = Keys.Num() - 1;
	int32 FoundIndex = -1;
	while ( FoundIndex < 0 )
	{
		int32 TestPos = (Start + End) / 2;
		float TestKeyTime = Keys[TestPos].Time;
		float NextTestKeyTime = Keys[TestPos + 1].Time;
		if ( TestKeyTime <= KeyTime )
		{
			if( NextTestKeyTime > KeyTime )
			{
				FoundIndex = TestPos;
			}
			else
			{
				Start = TestPos + 1;
			}
		}
		else
		{
			End = TestPos;
		}
	}
	return GetKeyHandle(FoundIndex);
}
