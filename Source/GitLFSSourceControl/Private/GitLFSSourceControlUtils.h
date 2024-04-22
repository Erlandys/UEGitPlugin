// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "GitLFSSourceControlRevision.h"
#include "GitLFSSourceControlState.h"

class FGitLFSCommandHelpers;
class FGitLFSSourceControlProvider;
class FGitLFSSourceControlState;

class FGitLFSSourceControlCommand;

struct FGitLFSVersion;

/**
 * Extract the status of a unmerged (conflict) file
 *
 * Example output of git ls-files --unmerged Content/Blueprints/BP_Test.uasset
 * 100644 d9b33098273547b57c0af314136f35b494e16dcb 1	Content/Blueprints/BP_Test.uasset
 * 100644 a14347dc3b589b78fb19ba62a7e3982f343718bc 2	Content/Blueprints/BP_Test.uasset
 * 100644 f3137a7167c840847cd7bd2bf07eefbfb2d9bcd2 3	Content/Blueprints/BP_Test.uasset
 *
 * 1: The "common ancestor" of the file (the version of the file that both the current and other branch originated from).
 * 2: The version from the current branch (the master branch in this case).
 * 3: The version from the other branch (the test branch)
*/
class FGitConflictStatusParser
{
public:
	/** Parse the unmerge status: extract the base SHA1 identifier of the file */
	FGitConflictStatusParser(const TArray<FString>& InResults)
	{
		const FString& CommonAncestor = InResults[0]; // 1: The common ancestor of merged branches
		CommonAncestorFileId = CommonAncestor.Mid(7, 40);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
		CommonAncestorFilename = CommonAncestor.Right(50);

		if (ensure(InResults.IsValidIndex(2)))
		{
			const FString& RemoteBranch = InResults[2]; // 1: The common ancestor of merged branches
			RemoteFileId = RemoteBranch.Mid(7, 40);
			RemoteFilename = RemoteBranch.Right(50);
		}
#endif
	}

	FString CommonAncestorFileId; ///< SHA1 Id of the file (warning: not the commit Id)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
	FString RemoteFileId; ///< SHA1 Id of the file (warning: not the commit Id)

	FString CommonAncestorFilename;
	FString RemoteFilename;
#endif
};


class FGitLFSSourceControlUtils
{
public:
	static FName GetAppStyleName();

	/**
	 * Run a Git "status" command and parse it.
	 *
	 * @param	InFiles				The files to be operated on
	 * @param	OutErrorMessages	Any errors (from StdErr) as an array per-line
	 * @param   OutStates           The resultant states
	 * @returns true if the command succeeded and returned no errors
	 */
	static bool RunUpdateStatus(
		FGitLFSSourceControlCommand& Command,
		const TArray<FString>& InFiles,
		TArray<FString>& OutErrorMessages,
		TMap<const FString, FGitLFSState>& OutStates,
		TMap<FString, FGitLFSSourceControlState>* OutControlStates = nullptr);

	/**
	 * Run a Git "status" command and parse it.
	 *
	 * @param	InFiles				The files to be operated on
	 * @param	OutErrorMessages	Any errors (from StdErr) as an array per-line
	 * @param   OutStates           The resultant states
	 * @returns true if the command succeeded and returned no errors
	 */
	static bool RunUpdateStatus(
		const FGitLFSSourceControlProvider& Provider,
		const TArray<FString>& InFiles,
		TArray<FString>& OutErrorMessages,
		TMap<FString, FGitLFSSourceControlState>& OutControlStates);

	/**
	 * Find the path to the Git binary, looking into a few places (standalone Git install, and other common tools embedding Git)
	 * @returns the path to the Git binary if found, or an empty string.
	 */
	static FString FindGitBinaryPath();

	static bool UpdateFileStagingOnSaved(const FString& Filename);

	/**
	 * Find the root of the Git repository, looking from the provided path and upward in its parent directories
	 * @param Path				The path to the Game Directory (or any path or file in any git repository)
	 * @param OutRepositoryRoot		The path to the root directory of the Git repository if found, else the path to the ProjectDir
	 * @returns true if the command succeeded and returned no errors
	 */
	static bool FindRootDirectory(const FString& Path, FString& OutRepositoryRoot);

	static bool CheckGitAvailability(const FString& InPathToGitBinary, FGitLFSVersion* OutVersion = nullptr);

	/**
	* Helper function for various commands to collect new states.
	* @returns true if any states were updated
	*/
	static bool CollectNewStates(const TMap<FString, FGitLFSSourceControlState>& States, TMap<const FString, FGitLFSState>& OutResults);

	/**
	 * Helper function for various commands to update cached states.
	 * @returns true if any states were updated
	 */
	static bool UpdateCachedStates(const TMap<const FString, FGitLFSState>& States);

	static bool UpdateChangelistStateByCommand();

	static void AbsoluteFilenames(const FString& RepositoryRoot, TArray<FString>& FileNames);

	/**
	 * Helper function to convert a filename array to absolute paths.
	 * @param	InFileNames		The filename array (relative paths)
	 * @param	InRelativeTo	Path to the WorkspaceRoot
	 * @return an array of filenames, transformed into absolute paths
	 */
	static TArray<FString> AbsoluteFilenames(const TArray<FString>& InFileNames, const FString& InRelativeTo);

	/**
	 * Reloads packages for these packages
	 */
	static void ReloadPackages(TArray<UPackage*>& InPackagesToReload);

	static TArray<FString> GetLockedFiles(const TArray<FString>& Files);

	/**
	 * Helper function to convert a filename array to relative paths.
	 * @param	InFileNames		The filename array
	 * @param	InRelativeTo	Path to the WorkspaceRoot
	 * @return an array of filenames, transformed into relative paths
	 */
	static TArray<FString> RelativeFilenames(const TArray<FString>& InFileNames, const FString& InRelativeTo);

	/**
	 * Run a Git "log" command and parse it.
	 *
	 * @param Command
	 * @param InFile				The file to be operated on
	 * @param bMergeConflict		In case of a merge conflict, we also need to get the tip of the "remote branch" (MERGE_HEAD) before the log of the "current branch" (HEAD)
	 * @param OutErrorMessages	Any errors (from StdErr) as an array per-line
	 * @param OutHistory			The history of the file
	 */
	static bool GetHistory(
		const FGitLFSSourceControlCommand& Command,
		const FString& InFile,
		bool bMergeConflict,
		TArray<FString>& OutErrorMessages,
		TArray<TSharedRef<FGitLFSSourceControlRevision>>& OutHistory);

	/**
	 * Helper function for various commands to collect new states.
	 * @returns true if any states were updated
	 */
	static bool CollectNewStates(
		const TArray<FString>& Files,
		TMap<const FString, FGitLFSState>& OutResults,
		EGitLFSFileState FileState,
		EGitLFSTreeState TreeState = EGitLFSTreeState::Unset,
		EGitLFSLockState LockState = EGitLFSLockState::Unset,
		EGitLFSRemoteState RemoteState = EGitLFSRemoteState::Unset);

	/**
	 * Parse the array of strings results of a 'git log' command
	 *
	 * Example git log results:
	 * commit 97a4e7626681895e073aaefd68b8ac087db81b0b
	 * Author: Sébastien Rombauts <sebastien.rombauts@gmail.com>
	 * Date:   2014-2015-05-15 21:32:27 +0200
	 * 
	 * 	Another commit used to test History
	 * 
	 * 	 - with many lines
	 * 	 - some <xml>
	 * 	 - and strange characteres $*+
	 * 
	 * M	Content/Blueprints/Blueprint_CeilingLight.uasset
	 * R100	Content/Textures/T_Concrete_Poured_D.uasset Content/Textures/T_Concrete_Poured_D2.uasset
	 * 
	 * commit 355f0df26ebd3888adbb558fd42bb8bd3e565000
	 * Author: Sébastien Rombauts <sebastien.rombauts@gmail.com>
	 * Date:   2014-2015-05-12 11:28:14 +0200
	 * 
	 * 	Testing git status, edit, and revert
	 * 
	 * A	Content/Blueprints/Blueprint_CeilingLight.uasset
	 * C099	Content/Textures/T_Concrete_Poured_N.uasset Content/Textures/T_Concrete_Poured_N2.uasset
	*/
	static void ParseLogResults(const TArray<FString>& InResults, TArray<TSharedRef<FGitLFSSourceControlRevision>>& OutHistory);

private:
	static bool RunUpdateStatus(
		const FString& PathToGit,
		const FString& PathToRepository,
		bool bUsingGitLFSLocking,
		const TArray<FString>& InFiles,
		bool bCollectNewStates,
		TArray<FString>& OutErrorMessages,
		TMap<const FString, FGitLFSState>& OutStates,
		TMap<FString, FGitLFSSourceControlState>* OutControlStates);

	/**
	 * Checks remote branches to see file differences.
	 *
	 * @param	PathToGitBinary	The path to the Git binary
	 * @param	RepositoryRoot	The Git repository from where to run the command - usually the Game directory
	 * @param	OutErrorMessages	Any errors (from StdErr) as an array per-line
	 * @param OutStates
	 */
	static void CheckRemote(
		const FString& PathToGitBinary,
		const FString& RepositoryRoot,
		TArray<FString>& OutErrorMessages,
		TMap<FString, FGitLFSSourceControlState>& OutStates);

	/**
	 * Remove redundant errors (that contain a particular string) and also
	 * update the commands success status if all errors were removed.
	 */
	static void RemoveRedundantErrors(FGitLFSSourceControlCommand& Command, const FString& Filter);

private:
	static FString FilenameFromGitStatus(const FString& Result);
	static FString GetFullPathFromGitStatus(const FString& FilePath, const FString& RepositoryRoot);
	/**
	 * @brief Detects how to parse the result of a "status" command to get workspace file states
	 *
	 *  It is either a command for a whole directory (ie. "Content/", in case of "Submit to Revision Control" menu),
	 * or for one or more files all on a same directory (by design, since we group files by directory in RunUpdateStatus())
	 *
	 * @param[in]	InPathToGitBinary	The path to the Git binary
	 * @param[in]	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
	 * @param[in]	InUsingLfsLocking	Tells if using the Git LFS file Locking workflow
	 * @param[in]	InFiles				List of files in a directory, or the path to the directory itself (never empty).
	 * @param[out]	InResults			Results from the "status" command
	 * @param[out]	OutStates			States of files for witch the status has been gathered (distinct than InFiles in case of a "directory status")
	 */
	static void ParseStatusResults(
		const FString& InPathToGitBinary,
		const FString& InRepositoryRoot,
		const bool InUsingLfsLocking,
		const TArray<FString>& InFiles,
		const TMap<FString, FString>& InResults,
		TMap<FString, FGitLFSSourceControlState>& OutStates);
	/** Parse the array of strings results of a 'git status' command for a directory
	 *
	 *  Called in case of a "directory status" (no file listed in the command) ONLY to detect Deleted/Missing/Untracked files
	 * since those files are not listed by the 'git ls-files' command.
	 *
	 * @see #ParseFileStatusResult() above for an example of a 'git status' results
	 */
	static void ParseDirectoryStatusResult(
		const bool InUsingLfsLocking,
		const TMap<FString, FString>& Results,
		TMap<FString, FGitLFSSourceControlState>& OutStates);
	static void ParseGitStatus(
		const FString& Line,
		EGitLFSFileState& OutFileState,
		EGitLFSTreeState& OutTreeState);
	/** Parse the array of strings results of a 'git status' command for a provided list of files all in a common directory
	 *
	 * Called in case of a normal refresh of status on a list of assets in a the Content Browser (or user selected "Refresh" context menu).
	 *
	 * Example git status results:
	 * M  Content/Textures/T_Perlin_Noise_M.uasset
	 * R  Content/Textures/T_Perlin_Noise_M.uasset -> Content/Textures/T_Perlin_Noise_M2.uasset
	 * ?? Content/Materials/M_Basic_Wall.uasset
	 * !! BasicCode.sln
	*/
	static void ParseFileStatusResult(
		const FString& InPathToGitBinary,
		const FString& InRepositoryRoot,
		const bool InUsingLfsLocking,
		const TSet<FString>& InFiles,
		const TMap<FString, FString>& InResults,
		TMap<FString, FGitLFSSourceControlState>& OutStates);

	/**
	 * Parse the output from the "version" command into GitMajorVersion and GitMinorVersion.
	 * @param InVersionString       The version string returned by `git --version`
	 * @param OutVersion            The FGitLFSVersion to populate
	 */
	static void ParseGitVersion(const FString& InVersionString, FGitLFSVersion* OutVersion);

	/**
	 * Extract the SHA1 identifier and size of a blob (file) from a Git "ls-tree" command.
	 *
	 * Example output for the command git ls-tree --long 7fdaeb2 Content/Blueprints/BP_Test.uasset
	 * 100644 blob a14347dc3b589b78fb19ba62a7e3982f343718bc   70731	Content/Blueprints/BP_Test.uasset
	*/
	static void ParseLSTreeOutput(const TArray<FString>& Output, FString& OutFileHash, int32& OutFileSize);

private:
	static TArray<FString> LockableTypes;
};