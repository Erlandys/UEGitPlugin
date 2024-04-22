// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlState.h"
#include "GitLFSSourceControlRevision.h"

#if GIT_ENGINE_VERSION >= 500
#include "Textures/SlateIcon.h"
#endif
#if GIT_ENGINE_VERSION >= 502
#include "RevisionControlStyle/RevisionControlStyle.h"
#endif

#define LOCTEXT_NAMESPACE "GitSourceControl.State"

TSharedPtr<ISourceControlRevision> FGitLFSSourceControlState::GetHistoryItem(const int32 HistoryIndex) const
{
	check(History.IsValidIndex(HistoryIndex));
	return History[HistoryIndex];
}

TSharedPtr<ISourceControlRevision> FGitLFSSourceControlState::FindHistoryRevision(const int32 RevisionNumber) const
{
	for (const TSharedRef<FGitLFSSourceControlRevision>& Revision : History)
	{
		if (Revision->GetRevisionNumber() == RevisionNumber)
		{
			return Revision;
		}
	}

	return nullptr;
}

TSharedPtr<ISourceControlRevision> FGitLFSSourceControlState::FindHistoryRevision(const FString& InRevision) const
{
	for (const TSharedRef<FGitLFSSourceControlRevision>& Revision : History)
	{
		if (Revision->GetRevision() == InRevision)
		{
			return Revision;
		}
	}

	return nullptr;
}

#if GIT_ENGINE_VERSION >= 503
ISourceControlState::FResolveInfo FGitLFSSourceControlState::GetResolveInfo() const
{
	return PendingResolveInfo;
}
#else
TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FGitLFSSourceControlState::GetBaseRevForMerge() const
{
	for (const TSharedRef<FGitLFSSourceControlRevision>& Revision : History)
	{
		// look for the the SHA1 id of the file, not the commit id (revision)
		if (Revision->FileHash == PendingMergeBaseFileHash)
		{
			return Revision;
		}
	}

	return nullptr;
}
#endif

#if GIT_ENGINE_VERSION >= 500
FSlateIcon FGitLFSSourceControlState::GetIcon() const
#else
FName FGitLFSSourceControlState::GetIconName() const
#endif
{

#if GIT_ENGINE_VERSION >= 502
#define GET_ICON_RETURN(NAME) FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl." #NAME)
#elif GIT_ENGINE_VERSION >= 500
#define GET_ICON_RETURN(NAME) FSlateIcon(FAppStyle::GetAppStyleSetName(), "Perforce." #NAME)
#else
#define GET_ICON_RETURN(NAME) FName("ContentBrowser.SCC_" #NAME)
#endif

	switch (GetGitState())
	{
	case EGitLFSState::NotAtHead: return GET_ICON_RETURN(NotAtHeadRevision);
	case EGitLFSState::LockedOther: return GET_ICON_RETURN(CheckedOutByOtherUser);
	case EGitLFSState::NotLatest: return GET_ICON_RETURN(ModifiedOtherBranch);
	case EGitLFSState::Unmerged: return GET_ICON_RETURN(Branched);
	case EGitLFSState::Added: return GET_ICON_RETURN(OpenForAdd);
	case EGitLFSState::Untracked: return GET_ICON_RETURN(NotInDepot);
	case EGitLFSState::Deleted: return GET_ICON_RETURN(MarkedForDelete);
	case EGitLFSState::Modified:
	case EGitLFSState::CheckedOut: return GET_ICON_RETURN(CheckedOut);
	case EGitLFSState::Ignored: return GET_ICON_RETURN(NotInDepot);
	default:
#if GIT_ENGINE_VERSION >= 500
		return {};
#else
		return NAME_None;
#endif
	}
}

#if GIT_ENGINE_VERSION < 500
FName FGitLFSSourceControlState::GetSmallIconName() const
{
	switch (GetGitState())
	{
	case EGitLFSState::NotAtHead: return FName("ContentBrowser.SCC_NotAtHeadRevision_Small");
	case EGitLFSState::LockedOther: return FName("ContentBrowser.SCC_CheckedOutByOtherUser_Small");
	case EGitLFSState::NotLatest: return FName("ContentBrowser.SCC_ModifiedOtherBranch_Small");
	case EGitLFSState::Unmerged: return FName("ContentBrowser.SCC_Branched_Small");
	case EGitLFSState::Added: return FName("ContentBrowser.SCC_OpenForAdd_Small");
	case EGitLFSState::Untracked: return FName("ContentBrowser.SCC_NotInDepot_Small");
	case EGitLFSState::Deleted: return FName("ContentBrowser.SCC_MarkedForDelete_Small");
	case EGitLFSState::Modified:
	case EGitLFSState::CheckedOut: return FName("ContentBrowser.SCC_CheckedOut_Small");
	case EGitLFSState::Ignored: return FName("ContentBrowser.SCC_NotInDepot_Small");
	default: return NAME_None;
	}
}
#endif

FText FGitLFSSourceControlState::GetDisplayName() const
{
	switch (GetGitState())
	{
	case EGitLFSState::NotAtHead: return LOCTEXT("NotCurrent", "Not current");
	case EGitLFSState::LockedOther: return FText::Format(LOCTEXT("CheckedOutOther", "Checked out by: {0}"), FText::FromString(State.LockUser));
	case EGitLFSState::NotLatest: return FText::Format(LOCTEXT("ModifiedOtherBranch", "Modified in branch: {0}"), FText::FromString(State.HeadBranch));
	case EGitLFSState::Unmerged: return LOCTEXT("Conflicted", "Conflicted");
	case EGitLFSState::Added: return LOCTEXT("OpenedForAdd", "Opened for add");
	case EGitLFSState::Untracked: return LOCTEXT("NotControlled", "Not Under Revision Control");
	case EGitLFSState::Deleted: return LOCTEXT("MarkedForDelete", "Marked for delete");
	case EGitLFSState::Modified:
	case EGitLFSState::CheckedOut: return LOCTEXT("CheckedOut", "Checked out");
	case EGitLFSState::Ignored: return LOCTEXT("Ignore", "Ignore");
	case EGitLFSState::Lockable: return LOCTEXT("ReadOnly", "Read only");
	case EGitLFSState::None: return LOCTEXT("Unknown", "Unknown");
	default: return {};
	}
}

FText FGitLFSSourceControlState::GetDisplayTooltip() const
{
	switch (GetGitState())
	{
	case EGitLFSState::NotAtHead: return LOCTEXT("NotCurrent_Tooltip", "The file(s) are not at the head revision");
	case EGitLFSState::LockedOther: return FText::Format(LOCTEXT("CheckedOutOther_Tooltip", "Checked out by: {0}"), FText::FromString(State.LockUser));
	case EGitLFSState::NotLatest: return FText::Format(LOCTEXT("ModifiedOtherBranch_Tooltip", "Modified in branch: {0} CL:{1} ({2})"), FText::FromString(State.HeadBranch), FText::FromString(HeadCommit), FText::FromString(HeadAction));
	case EGitLFSState::Unmerged: return LOCTEXT("ContentsConflict_Tooltip", "The contents of the item conflict with updates received from the repository.");
	case EGitLFSState::Added: return LOCTEXT("OpenedForAdd_Tooltip", "The file(s) are opened for add");
	case EGitLFSState::Untracked: return LOCTEXT("NotControlled_Tooltip", "Item is not under revision control.");
	case EGitLFSState::Deleted: return LOCTEXT("MarkedForDelete_Tooltip", "The file(s) are marked for delete");
	case EGitLFSState::Modified:
	case EGitLFSState::CheckedOut: return LOCTEXT("CheckedOut_Tooltip", "The file(s) are checked out");
	case EGitLFSState::Ignored: return LOCTEXT("Ignored_Tooltip", "Item is being ignored.");
	case EGitLFSState::Lockable: return LOCTEXT("ReadOnly_Tooltip", "The file(s) are marked locally as read-only");
	case EGitLFSState::None: return LOCTEXT("Unknown_Tooltip", "Unknown revision control state");
	default: return {};
	}
}

bool FGitLFSSourceControlState::CanCheckIn() const
{
	// We can check in if this is new content
	if (IsAdded())
	{
		return true;
	}

	// Cannot check back in if conflicted or not current 
	if (!IsCurrent() ||
		IsConflicted())
	{
		return false;
	}

	// We can check back in if we're locked.
	if (State.LockState == EGitLFSLockState::Locked)
	{
		return true;
	}

	// We can check in any file that has been modified, unless someone else locked it.
	if (State.LockState != EGitLFSLockState::LockedOther &&
		IsModified() &&
		IsSourceControlled())
	{
		return true;
	}

	return false;
}

bool FGitLFSSourceControlState::CanCheckout() const
{
	if (State.LockState == EGitLFSLockState::Unlockable)
	{
		// Everything is already available for check in (checked out).
		return false;
	}

	// We don't want to allow checkout if the file is out-of-date, as modifying an out-of-date binary file will most likely result in a merge conflict
	return
		State.LockState == EGitLFSLockState::NotLocked &&
		IsCurrent();
}

bool FGitLFSSourceControlState::IsCheckedOut() const
{
	if (State.LockState == EGitLFSLockState::Unlockable)
	{
		// TODO: try modified instead? might block editing the file with a holding pattern
		return IsSourceControlled();
	}

	// We check for modified here too, because sometimes you don't lock a file but still want to push it. CanCheckout still true, so that you can lock it later...
	if (State.LockState == EGitLFSLockState::Locked)
	{
		return true;
	}

	return
		State.FileState == EGitLFSFileState::Modified &&
		State.LockState != EGitLFSLockState::LockedOther;
}

bool FGitLFSSourceControlState::IsCheckedOutOther(FString* Who) const
{
	if (Who)
	{
		// The packages dialog uses our lock user regardless if it was locked by other or us.
		// But, if there is no lock user, it shows information about modification in other branches, which is important.
		// So, only show our own lock user if it hasn't been modified in another branch.
		// This is a very, very rare state (maybe impossible), but one that should be displayed properly.
		if (State.LockState == EGitLFSLockState::LockedOther ||
			(State.LockState == EGitLFSLockState::Locked && !IsModifiedInOtherBranch()))
		{
			*Who = State.LockUser;
		}
	}

	return State.LockState == EGitLFSLockState::LockedOther;
}

bool FGitLFSSourceControlState::GetOtherBranchHeadModification(FString& HeadBranchOut, FString& ActionOut, int32& HeadChangeListOut) const
{
	if (!IsModifiedInOtherBranch())
	{
		return false;
	}

	HeadBranchOut = State.HeadBranch;
	// TODO: from EGitLFSRemoteState
	ActionOut = HeadAction;
	// TODO: get head commit
	HeadChangeListOut = 0;
	return true;
}

bool FGitLFSSourceControlState::IsCurrent() const
{
	return
		State.RemoteState != EGitLFSRemoteState::NotAtHead &&
		State.RemoteState != EGitLFSRemoteState::NotLatest;
}

bool FGitLFSSourceControlState::IsSourceControlled() const
{
	return
		State.TreeState != EGitLFSTreeState::Untracked &&
		State.TreeState != EGitLFSTreeState::Ignored &&
		State.TreeState != EGitLFSTreeState::NotInRepo;
}

bool FGitLFSSourceControlState::IsAdded() const
{
	// Added is when a file was untracked and is now added.
	return State.FileState == EGitLFSFileState::Added;
}

bool FGitLFSSourceControlState::IsDeleted() const
{
	return State.FileState == EGitLFSFileState::Deleted;
}

bool FGitLFSSourceControlState::IsIgnored() const
{
	return State.TreeState == EGitLFSTreeState::Ignored;
}

bool FGitLFSSourceControlState::CanEdit() const
{
	// Perforce does not care about it being current
	return
		IsCheckedOut() ||
		IsAdded();
}

bool FGitLFSSourceControlState::CanDelete() const
{
	// Perforce enforces that a deleted file must be current.
	if (!IsCurrent())
	{
		return false;
	}

	// If someone else hasn't checked it out, we can delete revision controlled files.
	return
		!IsCheckedOutOther() &&
		IsSourceControlled();
}

bool FGitLFSSourceControlState::IsUnknown() const
{
	return
		State.FileState == EGitLFSFileState::Unknown &&
		State.TreeState == EGitLFSTreeState::NotInRepo;
}

bool FGitLFSSourceControlState::IsModified() const
{
	return
		State.TreeState == EGitLFSTreeState::Working ||
		State.TreeState == EGitLFSTreeState::Staged;
}


bool FGitLFSSourceControlState::CanAdd() const
{
	return State.TreeState == EGitLFSTreeState::Untracked;
}

bool FGitLFSSourceControlState::IsConflicted() const
{
	return State.FileState == EGitLFSFileState::Unmerged;
}

bool FGitLFSSourceControlState::CanRevert() const
{
	// Can revert the file state if we modified, even if it was locked by someone else.
	// Useful for when someone locked a file, and you just wanna play around with it locallly, and then revert it.
	return
		CanCheckIn() ||
		IsModified();
}

EGitLFSState FGitLFSSourceControlState::GetGitState() const
{
	// No matter what, we must pull from remote, even if we have locked or if we have modified.
	switch (State.RemoteState)
	{
	case EGitLFSRemoteState::NotAtHead: return EGitLFSState::NotAtHead;
	default: break;
	}

	/** Someone else locked this file across branches. */
	// We cannot push under any circumstance, if someone else has locked.
	if (State.LockState == EGitLFSLockState::LockedOther)
	{
		return EGitLFSState::LockedOther;
	}

	// We could theoretically push, but we shouldn't.
	if (State.RemoteState == EGitLFSRemoteState::NotLatest)
	{
		return EGitLFSState::NotLatest;
	}

	switch (State.FileState)
	{
	case EGitLFSFileState::Unmerged: return EGitLFSState::Unmerged;
	case EGitLFSFileState::Added: return EGitLFSState::Added;
	case EGitLFSFileState::Deleted: return EGitLFSState::Deleted;
	case EGitLFSFileState::Modified: return EGitLFSState::Modified;
	default: break;
	}

	if (State.TreeState == EGitLFSTreeState::Untracked)
	{
		return EGitLFSState::Untracked;
	}

	if (State.LockState == EGitLFSLockState::Locked)
	{
		return EGitLFSState::CheckedOut;
	}

	if (IsSourceControlled())
	{
		if (CanCheckout())
		{
			return EGitLFSState::Lockable;
		}

		return EGitLFSState::Unmodified;
	}

	return EGitLFSState::None;
}

#undef LOCTEXT_NAMESPACE
