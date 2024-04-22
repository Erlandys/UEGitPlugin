// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class FGitLFSSourceControlRevision;
class FGitLFSScopedTempFile;
class FGitLFSSourceControlCommand;
class FGitLFSSourceControlProvider;

class FGitLFSCommandHelpers
{
public:
	explicit FGitLFSCommandHelpers(const FString& PathToGit, const FString& RepositoryRoot);
	explicit FGitLFSCommandHelpers(const FGitLFSSourceControlProvider& Provider);
	explicit FGitLFSCommandHelpers(const FGitLFSSourceControlCommand& Command);

	/**
	 * Get Git config value
	 * @param	Config			Name of config key to look for
	 * @returns config value if set, else will return empty string
	 */
	FString GetConfig(const FString& Config) const;

	/**
	 * Get Git current checked-out branch
	 * @param	OutBranchName		Name of the current checked-out branch (if any, ie. not in detached HEAD)
	 * @returns true if the command succeeded and returned no errors
	 */
	bool GetBranchName(FString& OutBranchName) const;

	/**
	 * Get Git remote tracking branch
	 * @param	OutBranchName		Name of the current checked-out branch (if any, ie. not in detached HEAD)
	 * @returns false if the branch is not tracking a remote
	 */
	bool GetRemoteBranchName(FString& OutBranchName) const;

	/**
	 * Get the URL of the "origin" defaut remote server
	 * @param	OutRemoteUrl		URL of "origin" defaut remote server
	 * @returns true if the command succeeded and returned no errors
	 */
	bool GetRemoteUrl(FString& OutRemoteUrl) const;

	/**
	 * Gets Git attribute to see if these extensions are lockable
	 */
	bool CheckLFSLockable(const TArray<FString>& Files, TArray<FString>& OutErrorMessages) const;

	/**
	* Get Git remote tracking branches that match wildcard
	* @returns false if no matching branches
	*/
	bool GetRemoteBranchesWildcard(const FString& PatternMatch, TArray<FString>& OutBranchNames) const;

	bool FetchRemote(
		bool bUsingGitLfsLocking,
		TArray<FString>& OutResults,
		TArray<FString>& OutErrorMessages) const;

	bool PullOrigin(
		const TArray<FString>& InFiles,
		TArray<FString>& OutFiles,
		TArray<FString>& OutResults,
		TArray<FString>& OutErrorMessages) const;

	bool GetLocks(
		const FString& Params,
		const FString& LockUser,
		TMap<FString, FString>& OutLocks,
		TArray<FString>& OutErrorMessages);

	bool GetStatusNoLocks(
		bool bAll,
		const TArray<FString>& Files,
		TArray<FString>& OutFiles,
		TArray<FString>& OutErrors) const;

	bool GetLog(
		const TArray<FString>& Parameters,
		const TArray<FString>& Files,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;

	/**
	 * Get Git current commit details
	 * @param	OutCommitId			Current Commit full SHA1
	 * @param	OutCommitSummary	Current Commit description's Summary
	 * @returns true if the command succeeded and returned no errors
	 */
	bool GetCommitInfo(
		FString& OutCommitId,
		FString& OutCommitSummary) const;

	bool RunReset(
		bool bHard,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;
	bool RunClean(
		bool bForce,
		bool bRemoveDirectories,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;
	bool RunRemove(
		const TArray<FString>& Files,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;
	bool RunCheckout(
		const TArray<FString>& Files,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;

	bool LockFiles(
		const TArray<FString>& Files,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;

	bool UnlockFiles(
		const TArray<FString>& Files,
		bool bAbsolutePaths,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;

	bool RunAdd(
		bool bAll,
		const TArray<FString>& Files,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;

	bool RunCommit(
		const FGitLFSScopedTempFile& TempFile,
		const TArray<FString>& Files,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;

	bool RunPush(
		const TArray<FString>& Parameters,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;

	bool RunDiff(
		const TArray<FString>& Parameters,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;

	bool RunLSTree(
		const TArray<FString>& Parameters,
		const FString& File,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;

	bool RunRestore(
		bool bStaged,
		const TArray<FString>& Files,
		TArray<FString>& OutResult,
		TArray<FString>& OutErrors) const;

	bool RunLSRemote(
		bool bPrintRemoteURL,
		bool bOnlyBranches) const;

	bool RunStash(bool bSave) const;

	bool RunShow(TArray<FString>& OutInfoMessages, TArray<FString>& OutErrors) const;

	void RunInit();
	void RunAddOrigin(const FString& URL);
	void RunLFSInstall();

	bool RunGitVersion(FString& Output) const;
	bool RunLSFiles(bool bUnmerged, const FString& File, TArray<FString>& Output) const;

	// Removes ignored files from original list and returns them in a separate list
	TArray<FString> RemoveIgnoredFiles(TArray<FString>& Files);

public:
	void SetRepositoryRoot(const FString& NewRepositoryRoot)
	{
		RepositoryRoot = NewRepositoryRoot;
	}

	const FString& GetRepositoryRoot() const
	{
		return RepositoryRoot;
	}

public:
	static bool IsFileLFSLockable(const FString& InFile);

private:
	/**
	 * Parse informations on a file locked with Git LFS
	 *
	 * Examples output of "git lfs locks":
	 * Content\ThirdPersonBP\Blueprints\ThirdPersonCharacter.uasset    SRombauts       ID:891
	 * Content\ThirdPersonBP\Blueprints\ThirdPersonCharacter.uasset                    ID:891
	 * Content\ThirdPersonBP\Blueprints\ThirdPersonCharacter.uasset    ID:891
	 */
	static void ParseGitLockLine(
		const FString& RepositoryRoot,
		const FString& Line,
		bool bAbsolutePath,
		FString& OutFileName,
		FString& OutUser);

	/**
	 * Unloads packages of specified named files
	 */
	static TArray<UPackage*> UnlinkPackages(const TArray<FString>& PackageNames);

private:
	FString PathToGit;
	FString RepositoryRoot;
	FString GitRoot;

private:
	static TArray<FString> LockableTypes;
};
