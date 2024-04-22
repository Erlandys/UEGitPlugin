// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class FGitLFSLockedFilesCache
{
public:
	static FDateTime LastUpdated;

	static void SetLockedFiles(const TMap<FString, FString>& NewLocks);
	static void AddLockedFile(const FString& FilePath, const FString& LockUser);
	static void RemoveLockedFile(const FString& FilePath);

	static const TMap<FString, FString>& GetLockedFiles()
	{
		return LockedFiles;
	}

	/**
	 * Run 'git lfs locks" to extract all lock information for all files in the repository
	 *
	 * @param RepositoryRoot	The Git repository from where to run the command - usually the Game directory
	 * @param PathToGit   The Git binary fallback path
	 * @param OutErrorMessages    Any errors (from StdErr) as an array per-line
	 * @param OutLocks		    The lock results (file, username)
	 * @param bInvalidateCache
	 * @returns true if the command succeeded and returned no errors
	 */
	static bool GetAllLocks(
		const FString& RepositoryRoot,
		const FString& PathToGit,
		TArray<FString>& OutErrorMessages,
		TMap<FString, FString>& OutLocks,
		bool bInvalidateCache);

private:
	static void OnFileLockChanged(const FString& FilePath, const FString& LockUser, bool bLocked);

	// update local read/write state when our own lock statuses change
	static TMap<FString, FString> LockedFiles;
};