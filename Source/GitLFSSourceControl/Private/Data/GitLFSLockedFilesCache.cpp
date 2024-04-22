// Fill out your copyright notice in the Description page of Project Settings.

#include "GitLFSLockedFilesCache.h"

#include "GitLFSSourceControlModule.h"

FDateTime FGitLFSLockedFilesCache::LastUpdated = FDateTime::MinValue();
TMap<FString, FString> FGitLFSLockedFilesCache::LockedFiles = TMap<FString, FString>();

void FGitLFSLockedFilesCache::SetLockedFiles(const TMap<FString, FString>& NewLocks)
{
	for (const auto& It : LockedFiles)
	{
		if (!NewLocks.Contains(It.Key))
		{
			OnFileLockChanged(It.Key, It.Value, false);
		}
	}

	for (const auto& It : NewLocks)
	{
		if (!LockedFiles.Contains(It.Key))
		{
			OnFileLockChanged(It.Key, It.Value, true);
		}
	}

	LockedFiles = NewLocks;
}

void FGitLFSLockedFilesCache::AddLockedFile(const FString& FilePath, const FString& LockUser)
{
	LockedFiles.Add(FilePath, LockUser);
	OnFileLockChanged(FilePath, LockUser, true);
}

void FGitLFSLockedFilesCache::RemoveLockedFile(const FString& FilePath)
{
	FString User;
	LockedFiles.RemoveAndCopyValue(FilePath, User);
	OnFileLockChanged(FilePath, User, false);
}

bool FGitLFSLockedFilesCache::GetAllLocks(
	const FString& RepositoryRoot,
	const FString& PathToGit,
	TArray<FString>& OutErrorMessages,
	TMap<FString, FString>& OutLocks,
	const bool bInvalidateCache)
{
	static const FTimespan CacheLimit = FTimespan::FromSeconds(30);

	// You may ask, why are we ignoring state cache, and instead maintaining our own lock cache?
	// The answer is that state cache updating is another operation, and those that update status
	// (and thus the state cache) are using GetAllLocks. However, querying remote locks are almost always
	// irrelevant in most of those update status cases. So, we need to provide a fast way to provide
	// an updated local lock state. We could do this through the relevant lfs lock command arguments, which
	// as you will see below, we use only for offline cases, but the exec cost of doing this isn't worth it
	// when we can easily maintain this cache here. So, we are really emulating an internal Git LFS locks cache
	// call, which gets fed into the state cache, rather than reimplementing the state cache :)
	const FDateTime CurrentTime = FDateTime::Now();
	bool bCacheExpired = bInvalidateCache;
	if (!bInvalidateCache)
	{
		const FTimespan CacheTimeElapsed = CurrentTime - LastUpdated;
		bCacheExpired = CacheTimeElapsed > CacheLimit;
	}
	bool bResult = false;

	FGitLFSCommandHelpers Helpers(PathToGit, RepositoryRoot);

	if (bCacheExpired)
	{
		// Our cache expired, or they asked us to expire cache. Query locks directly from the remote server.
		if (Helpers.GetLocks(TEXT(""), TEXT(""), OutLocks, OutErrorMessages))
		{
			LastUpdated = CurrentTime;
			SetLockedFiles(OutLocks);
			return true;
		}

		// We tried to invalidate the UE cache, but we failed for some reason. Try updating lock state from LFS cache.
		// Get the last known state of remote locks
		if (FGitLFSSourceControlModule* GitSourceControl = FGitLFSSourceControlModule::GetThreadSafe())
		{
			const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl->GetProvider();
			const FString& LockUser = Provider->GetLockUser();

			bResult = Helpers.GetLocks(TEXT("--cached"), LockUser, OutLocks, OutErrorMessages);
			bResult &= Helpers.GetLocks(TEXT("--local"), LockUser, OutLocks, OutErrorMessages);
		}
		else
		{
			bResult = false;
		}
	}

	if (!bResult)
	{
		// We can use our internally tracked local lock cache (an effective combination of --cached and --local)
		OutLocks = GetLockedFiles();
		bResult = true;
	}

	return bResult;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void FGitLFSLockedFilesCache::OnFileLockChanged(const FString& FilePath, const FString& LockUser, const bool bLocked)
{
	const TSharedPtr<FGitLFSSourceControlProvider> Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider) ||
		Provider->GetLockUser() != LockUser)
	{
		return;
	}

	FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*FilePath, !bLocked);
}
