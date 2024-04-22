// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "ISourceControlChangelistState.h"
#include "ISourceControlState.h"
#include "GitLFSSourceControlHelpers.h"
#include "GitLFSSourceControlChangelist.h"

class FGitLFSSourceControlChangelistState : public ISourceControlChangelistState
{
public:
	explicit FGitLFSSourceControlChangelistState(const FGitLFSSourceControlChangelist& Changelist, const FString& Description = FString())
		: Changelist(Changelist)
		, Description(Description)
	{
	}

	explicit FGitLFSSourceControlChangelistState(FGitLFSSourceControlChangelist&& Changelist, FString&& Description)
		: Changelist(MoveTemp(Changelist))
		, Description(MoveTemp(Description))
	{
	}

	//~ Begin ISourceControlChangelistState Interface
	virtual FName GetIconName() const override
	{
		// Mimic P4V colors, returning the red icon if there are active file(s), the blue if the changelist is empty or all the files are shelved.
		return FName("SourceControl.Changelist");
	}

	virtual FName GetSmallIconName() const override
	{
		return GetIconName();
	}

	virtual FText GetDisplayText() const override
	{
		return FText::FromString(Changelist.GetName());
	}

	virtual FText GetDescriptionText() const override
	{
		return FText::FromString(Description);
	}

	virtual FText GetDisplayTooltip() const override
	{
		return NSLOCTEXT("GitSourceControl.ChangelistState", "Tooltip", "Tooltip");
	}

	virtual const FDateTime& GetTimeStamp() const override
	{
		return TimeStamp;
	}

	virtual const GIT_UE_504_SWITCH(TArray<FSourceControlStateRef>&, TArray<FSourceControlStateRef>) GetFilesStates() const override
	{
		return Files;
	}

#if GIT_ENGINE_VERSION >= 504
	virtual int32 GetFilesStatesNum() const override
	{
		return Files.Num();
	}
#endif

	virtual const GIT_UE_504_SWITCH(TArray<FSourceControlStateRef>&, TArray<FSourceControlStateRef>) GetShelvedFilesStates() const override
	{
		return ShelvedFiles;
	}

#if GIT_ENGINE_VERSION >= 504
	virtual int32 GetShelvedFilesStatesNum() const override
	{
		return ShelvedFiles.Num();
	}
#endif

	virtual FSourceControlChangelistRef GetChangelist() const override
	{
		return StaticCastSharedRef<ISourceControlChangelist>(MakeShared<FGitLFSSourceControlChangelist>(Changelist));
	}
	//~ End ISourceControlChangelistState Interface

public:
	FGitLFSSourceControlChangelist Changelist;
	FString Description;
	TArray<FSourceControlStateRef> Files;
	TArray<FSourceControlStateRef> ShelvedFiles;

	/** The timestamp of the last update */
	FDateTime TimeStamp;
};