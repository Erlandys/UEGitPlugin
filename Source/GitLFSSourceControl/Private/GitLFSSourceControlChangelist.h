// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "ISourceControlChangelist.h"
#include "GitLFSSourceControlHelpers.h"

class FGitLFSSourceControlChangelist : public ISourceControlChangelist
{
public:
	FGitLFSSourceControlChangelist() = default;

	explicit FGitLFSSourceControlChangelist(FString&& InChangelistName, const bool bInInitialized = false)
		: ChangelistName(MoveTemp(InChangelistName))
		 , bInitialized(bInInitialized)
	{
	}

	virtual bool CanDelete() const override
	{
		return false;
	}

	bool operator==(const FGitLFSSourceControlChangelist& InOther) const
	{
		return ChangelistName == InOther.ChangelistName;
	}

	bool operator!=(const FGitLFSSourceControlChangelist& InOther) const
	{
		return ChangelistName != InOther.ChangelistName;
	}

#if GIT_ENGINE_VERSION >= 503
	virtual bool IsDefault() const override
	{
		return ChangelistName == WorkingChangelist.ChangelistName;
	}
#endif

	void SetInitialized()
	{
		bInitialized = true;
	}

	bool IsInitialized() const
	{
		return bInitialized;
	}

	void Reset()
	{
		ChangelistName.Reset();
		bInitialized = false;
	}

	friend FORCEINLINE uint32 GetTypeHash(const FGitLFSSourceControlChangelist& InGitChangelist)
	{
		return GetTypeHash(InGitChangelist.ChangelistName);
	}

	FString GetName() const
	{
		return ChangelistName;
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
	virtual FString GetIdentifier() const override
	{
		return ChangelistName;
	}
#endif

public:
	static FGitLFSSourceControlChangelist WorkingChangelist;
	static FGitLFSSourceControlChangelist StagedChangelist;

private:
	FString ChangelistName;
	bool bInitialized = false;
};