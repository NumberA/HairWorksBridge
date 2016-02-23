// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GCObject.h"

/** 
 * This set of functions contains blueprint related UObject functionality.
 */
struct FBlueprintSupport
{
	/** 
	 * Defined in BlueprintSupport.cpp
	 * Duplicates all fields of a struct in depth-first order. It makes sure that everything contained
	 * in a class is duplicated before the struct itself, as well as all function parameters before the
	 * function itself.
	 *
	 * @param	StructToDuplicate			Instance of the struct that is about to be duplicated
	 * @param	Writer						duplicate writer instance to write the duplicated data to
	 */
	static void DuplicateAllFields(class UStruct* StructToDuplicate, class FDuplicateDataWriter& Writer);

	/** 
	 * A series of query functions that we can use to easily gate-off/disable 
	 * aspects of the deferred loading (mostly for testing purposes). 
	 */
	static bool UseDeferredDependencyLoading();
	static bool IsDeferredExportCreationDisabled();
	static bool IsDeferredCDOInitializationDisabled();
};

#if WITH_EDITOR
/**
 * This is a helper struct that allows us to gather all previously unloaded class dependencies of a UClass
 * The first time we create a new UClass object in FLinkerLoad::CreateExport(), we register it as a dependency
 * master.  Any subsequent UClasses that are created for the first time during the preload of that class are
 * added to the list as potential cyclic referencers.  We then step over the list at the end of the load, and
 * recompile any classes that may depend on each other a second time to ensure that that functions and properties
 * are properly resolved
 */
struct COREUOBJECT_API FScopedClassDependencyGather
{
public:
	FScopedClassDependencyGather(UClass* ClassToGather);
	~FScopedClassDependencyGather();

	/**
	 * Post load, some systems would like an easy list of dependencies. This will
	 * retrieve that latest BatchClassDependencies (filled with dependencies from
	 * the last loaded class).
	 * 
	 * @return The most recent array of tracked dependencies.
	 */
	static TArray<UClass*> const& GetCachedDependencies();

private:
	/** Whether or not this dependency gather is the dependency master, and thus should process all dependencies in the destructor */
	bool bMasterClass;	

	/** The current class that is gathering potential dependencies in this scope */
	static UClass* BatchMasterClass;

	/** List of dependencies (i.e. UClasses that have been newly instantiated) in the scope of this dependency gather */
	static TArray<UClass*> BatchClassDependencies;

	FScopedClassDependencyGather();
};

enum class EReplacementResult
{
	/** Don't replace the provided package at all */
	DontReplace,

	/** Generate a stub file, but don't replace the package */
	GenerateStub,

	/** Completely replace the file with generated code */
	ReplaceCompletely
};

/**
 * The struct is used while saving cooked package to find replacements for 
 * converted Blueprint assets.
 */
struct COREUOBJECT_API FScriptCookReplacementCoordinator
{
	static FScriptCookReplacementCoordinator* Get();
	static void Create(bool bEnabled, const TArray<FString>& ExcludedAssetTypes, const TArray<FString>& ExcludedBlueprintTypes, const TMap<UObject*, UClass*>& ReplacementMap);

public:
	/**
	 * Enables this for handling (script) asset replacements (works only when  
	 * running a commandlet - presumably the cooker).
	 * 
	 * @return True if this was successfully enabled, otherwise false.
	 */
	bool Initialize();

	/** 
	 * Checks to see if we're running with this on, aiming to swap out assets 
	 * with native counterparts.
	 */
	bool IsEnabled() const
	{
		return bEnabled;
	}

	/**
	 * Determines whether the provided package needs to be replaced (in part or completely)
	 * 
	 * @param Package	The package in question
	 * @return Whether the package should be converted
	 */
	EReplacementResult IsTargetedForReplacement(const UPackage* Package) const;
	
	/**
	* Determines whether the provided object needs to be replaced (in part or completely).
	* Some objects in a package may require conversion and some may not. If any object 
	* in a package wants to be converted then it is implied that all other objects will 
	* be converted with it (no support for partial package conversion, beyond stubs)
	*
	* @param Object	The package in question
	* @return Whether the object should be converted
	*/
	EReplacementResult IsTargetedForReplacement(const UObject* Object) const;

	// Get class of converted asset. One that was specified in AddConvertedObject
	UClass* FindReplacedClass(const UObject* Obj) const;

private: 
	/** Private so we can keep this as a singleton */
	FScriptCookReplacementCoordinator(bool bEnabled, const TArray<FString>& ExcludedAssetTypes, const TArray<FString>& ExcludedBlueprintTypes, const TMap<UObject*, UClass*>& ReplacementMap);

	/** 
	 * Used to tell if replacements should be used (substituting native objects 
	 * for assets). 
	 */
	bool bEnabled;

	const TArray<FString> ExcludedAssetTypes;
	const TArray<FString> ExcludedBlueprintTypes;

	/** Tracks which assets has a replaced class */
	TMap<UObject*, UClass*> ReplacementMap;

	TSet<FString> ReplacedPackages;

	/** singleton instance: */
	static FScriptCookReplacementCoordinator* CoordinatorInstance;
};

#endif // WITH_EDITOR

/** 
 * A helper struct for storing FObjectInitializers that were not run on 
 * Blueprint CDO's post-construction (presumably because that CDO's super had 
 * not been fully serialized yet). 
 * 
 * This was designed to hold onto FObjectInitializers until a later point, when 
 * they can properly be ran (presumably in FLinkerLoad::ResolveDeferredExports,
 * after the super has been finalized).
 */
struct FDeferredObjInitializerTracker : TThreadSingleton<FDeferredObjInitializerTracker>
{
public:
	FDeferredObjInitializerTracker() : ResolvingClass(nullptr) {}

	/** Stores a copy of the specified FObjectInitializer and returns a pointer to it (could be null if a corresponding class could not be determined). */
	static FObjectInitializer* Add(const FObjectInitializer& DeferringInitializer);

	/** Looks up a FObjectInitializer that was deferred for the specified class (an FObjectInitializer for that class's CDO). */
	static FObjectInitializer* Find(UClass* LoadClass);

	/** Checks to see if the specified class has been logged as deferred (meaning its CDO hasn't had FObjectInitializer::PostConstructInit() ran on it yet). */
	static bool IsCdoDeferred(UClass* LoadClass);

	/** Determines if the specified sub-object should have its Preload() skipped; if so, this will cache the sub-object and return true. */
	static bool DeferSubObjectPreload(UObject* SubObject);

	/** Destroys any FObjectInitializers that were cached corresponding to the specified class. */
	static void Remove(UClass* LoadClass);

	/** Runs FObjectInitializer::PostConstructInit() on the specified class's CDO (if it was deferred), and preloads any sub-objects that were skipped. */
	static bool ResolveDeferredInitialization(UClass* LoadClass);

	/**  */
	static void ResolveDeferredSubObjects(UObject* CDO);

	/**  */
	static void ResolveDeferredSubClassObjects(UClass* SuperClass);

private:
	/** A map that tracks the relationship between Blueprint classes and FObjectInitializers for their CDOs */
	TMap<UClass*, FObjectInitializer> DeferredInitializers;
	/** Track default sub-objects that had their Preload() skipped, because the owning CDO's initialization should happen first */
	TMultiMap<UClass*, UObject*> DeferredSubObjects;
	/** Used to keep ResolveDeferredSubObjects() from re-adding sub-objects via DeferSubObjectPreload() */
	UClass* ResolvingClass;
	/** Tracks sub-classes that have had their CDO deferred as a result of the super not being fully serialized */
	TMultiMap<UClass*, UClass*> SuperClassMap;
};


struct COREUOBJECT_API FBlueprintDependencyData
{
	FName PackageName;
	FName ObjectName;
	FName ClassPackageName;
	FName ClassName;
};

/**
 *	Stores info about dependencies of native classes converted from BPs
 */
struct COREUOBJECT_API FConvertedBlueprintsDependencies
{
	typedef void(*GetDependenciesNamesFunc)(TArray<FBlueprintDependencyData>&);

private:

	TMap<FName, GetDependenciesNamesFunc> PackageNameToGetter;

public:
	static FConvertedBlueprintsDependencies& Get();

	void RegisterClass(FName PackageName, GetDependenciesNamesFunc GetAssets);

	/** Get all assets paths necessary for the class with the given class name and all converted classes that dependencies. */
	void GetAssets(FName PackageName, TArray<FBlueprintDependencyData>& OutDependencies) const;
};