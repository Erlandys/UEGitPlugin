// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "ISourceControlState.h"
#include "GitLFSSourceControlHelpers.h"
#include "GitLFSSourceControlChangelist.h"

class FGitLFSSourceControlRevision;

/** A consolidation of state priorities. */
enum class EGitLFSState
{
	Unset,
	NotAtHead,
#if 0
	AddedAtHead,
	DeletedAtHead,
#endif
	LockedOther,
	NotLatest,

	/** Unmerged state (modified, but conflicts) */
	Unmerged,
	Added,
	Deleted,
	Modified,

	/** Not modified, but locked explicitly. */
	CheckedOut,
	Untracked,
	Lockable,
	Unmodified,
	Ignored,

	/** Whatever else. */
	None,
};

/** Corresponds to diff file states. */
enum class EGitLFSFileState
{
	Unset,
	Unknown,
	Added,
	Copied,
	Deleted,
	Modified,
	Renamed,
	Missing,
	Unmerged,
};

/** Where in the world is this file? */
enum class EGitLFSTreeState
{
	Unset,

	/** This file is synced to commit */
	Unmodified,

	/** This file is modified, but not in staging tree */
	Working,

	/** This file is in staging tree (git add) */
	Staged,

	/** This file is not tracked in the repo yet */
	Untracked,

	/** This file is ignored by the repo */
	Ignored,

	/** This file is outside the repo folder */
	NotInRepo,
};

/** LFS locks status of this file */
enum class EGitLFSLockState
{
	Unset,
	Unknown,
	Unlockable,
	NotLocked,
	Locked,
	LockedOther,
};

/** What is this file doing at HEAD? */
enum class EGitLFSRemoteState
{
	Unset,

	/** Up to date */
	UpToDate,

	/** Local version is behind remote */
	NotAtHead,

#if 0
	// TODO: handle these
	/** Remote file does not exist on local */
	AddedAtHead,

	/** Local was deleted on remote */
	DeletedAtHead,
#endif

	/** Not at the latest revision amongst the tracked branches */
	NotLatest,
};

/** Combined state, for updating cache in a map. */
struct FGitLFSState
{
	EGitLFSFileState FileState = EGitLFSFileState::Unknown;
	EGitLFSTreeState TreeState = EGitLFSTreeState::NotInRepo;
	EGitLFSLockState LockState = EGitLFSLockState::Unknown;

	/** Name of user who has locked the file */
	FString LockUser;

	EGitLFSRemoteState RemoteState = EGitLFSRemoteState::UpToDate;

	/** The branch with the latest commit for this file */
	FString HeadBranch;
};

class FGitLFSSourceControlState : public ISourceControlState
{
public:
	FGitLFSSourceControlState(const FString &InLocalFilename)
		: LocalFilename(InLocalFilename)
	{
	}

	//~ Begin ISourceControlState Interface
	virtual int32 GetHistorySize() const override
	{
		return History.Num();
	}
	virtual TSharedPtr<ISourceControlRevision> GetHistoryItem(const int32 HistoryIndex) const override;
	virtual TSharedPtr<ISourceControlRevision> FindHistoryRevision(int32 RevisionNumber) const override;
	virtual TSharedPtr<ISourceControlRevision> FindHistoryRevision(const FString& InRevision) const override;

#if GIT_ENGINE_VERSION >= 503
	virtual FResolveInfo GetResolveInfo() const override;
#else
	virtual TSharedPtr<ISourceControlRevision> GetBaseRevForMerge() const override;
#endif

#if GIT_ENGINE_VERSION >= 502
	virtual TSharedPtr<ISourceControlRevision> GetCurrentRevision() const override
	{
		return nullptr;
	}
#endif

#if GIT_ENGINE_VERSION >= 500
	virtual FSlateIcon GetIcon() const override;
#else
	virtual FName GetIconName() const override;
	virtual FName GetSmallIconName() const override;
#endif

	virtual FText GetDisplayName() const override;
	virtual FText GetDisplayTooltip() const override;
	virtual const FString& GetFilename() const override
	{
		return LocalFilename;
	}
	virtual const FDateTime& GetTimeStamp() const override
	{
		return TimeStamp;
	}
	// Deleted and Missing assets cannot appear in the Content Browser, but they do in the Submit files to Revision Control window!
	virtual bool CanCheckIn() const override;
	virtual bool CanCheckout() const override;
	virtual bool IsCheckedOut() const override;
	virtual bool IsCheckedOutOther(FString* Who = NULL) const override;
	virtual bool IsCheckedOutInOtherBranch(const FString& CurrentBranch = FString()) const override
	{
		// You can't check out separately per branch
		return false;
	}
	virtual bool IsModifiedInOtherBranch(const FString& CurrentBranch = FString()) const override
	{
		return State.RemoteState == EGitLFSRemoteState::NotLatest;
	}
	virtual bool IsCheckedOutOrModifiedInOtherBranch(const FString& CurrentBranch = FString()) const override
	{
		return IsModifiedInOtherBranch(CurrentBranch);
	}
	virtual TArray<FString> GetCheckedOutBranches() const override
	{
		return {};
	}
	virtual FString GetOtherUserBranchCheckedOuts() const override
	{
		return {};
	}
	virtual bool GetOtherBranchHeadModification(FString& HeadBranchOut, FString& ActionOut, int32& HeadChangeListOut) const override;
	virtual bool IsCurrent() const override;
	virtual bool IsSourceControlled() const override;
	virtual bool IsAdded() const override;
	virtual bool IsDeleted() const override;
	virtual bool IsIgnored() const override;
	virtual bool CanEdit() const override;
	virtual bool IsUnknown() const override;
	virtual bool IsModified() const override;
	virtual bool CanAdd() const override;
	virtual bool CanDelete() const override;
	virtual bool IsConflicted() const override;
	virtual bool CanRevert() const override;
	//~ End ISourceControlState Interface

private:
	EGitLFSState GetGitState() const;

public:
	/** History of the item, if any */
	TArray<TSharedRef<FGitLFSSourceControlRevision>> History;

	/** Filename on disk */
	FString LocalFilename;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
	/** Pending rev info with which a file must be resolved, invalid if no resolve pending */
	FResolveInfo PendingResolveInfo;

	UE_DEPRECATED(5.3, "Use PendingResolveInfo.BaseRevision instead")
#endif
	/** File Id with which our local revision diverged from the remote revision */
	FString PendingMergeBaseFileHash;

	/** Status of the file */
	FGitLFSState State;

	FGitLFSSourceControlChangelist Changelist;

	/** The timestamp of the last update */
	FDateTime TimeStamp = 0;

	/** The action within the head branch TODO */
	FString HeadAction = TEXT("Changed");

	/** The last file modification time in the head branch TODO */
	int64 HeadModTime = 0;

	/** The change list the last modification TODO */
	FString HeadCommit = TEXT("Unknown");
};