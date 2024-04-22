// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlUtils.h"

#include "GitLFSMessageLog.h"
#include "GitLFSCommandHelpers.h"
#include "GitLFSSourceControlModule.h"
#include "GitLFSSourceControlCommand.h"
#include "Data/GitLFSLockedFilesCache.h"
#include "GitLFSSourceControlChangelistState.h"

#include "PackageTools.h"

#if ENGINE_MAJOR_VERSION >= 5
#include "HAL/PlatformFileManager.h"
#else
#include "HAL/PlatformFilemanager.h"
#endif

#if GIT_ENGINE_VERSION < 501
#include "EditorStyleSet.h"
#endif

#ifndef GIT_DEBUG_STATUS
#define GIT_DEBUG_STATUS 0
#endif

#define LOCTEXT_NAMESPACE "GitSourceControl"

FName FGitLFSSourceControlUtils::GetAppStyleName()
{
#if GIT_ENGINE_VERSION >= 501
	return FAppStyle::GetAppStyleSetName();
#else
	return FEditorStyle::GetStyleSetName();
#endif
}

// Run a batch of Git "status" command to update status of given files and/or directories.
bool FGitLFSSourceControlUtils::RunUpdateStatus(
	FGitLFSSourceControlCommand& Command,
	const TArray<FString>& InFiles,
	TArray<FString>& OutErrorMessages,
	TMap<const FString, FGitLFSState>& OutStates,
	TMap<FString, FGitLFSSourceControlState>* OutControlStates)
{
	const bool bSuccess = RunUpdateStatus(
		Command.PathToGitBinary,
		Command.PathToRepositoryRoot,
		Command.bUsingGitLfsLocking,
		InFiles,
		true,
		OutErrorMessages,
		OutStates,
		OutControlStates);

	RemoveRedundantErrors(Command, TEXT("' is outside repository"));

	return bSuccess;
}

bool FGitLFSSourceControlUtils::RunUpdateStatus(
	const FGitLFSSourceControlProvider& Provider,
	const TArray<FString>& InFiles,
	TArray<FString>& OutErrorMessages,
	TMap<FString, FGitLFSSourceControlState>& OutControlStates)
{
	TMap<const FString, FGitLFSState> DummyStates;
	return RunUpdateStatus(
		Provider.GetGitBinaryPath(),
		Provider.GetPathToRepositoryRoot(),
		Provider.UsesGitLFSLocking(),
		InFiles,
		false,
		OutErrorMessages,
		DummyStates,
		&OutControlStates);
}

FString FGitLFSSourceControlUtils::FindGitBinaryPath()
{
#if PLATFORM_WINDOWS
	// 1) First of all, look into standard install directories
	// NOTE using only "git" (or "git.exe") relying on the "PATH" envvar does not always work as expected, depending on the installation:
	// If the PATH is set with "git/cmd" instead of "git/bin",
	// "git.exe" launch "git/cmd/git.exe" that redirect to "git/bin/git.exe" and ExecProcess() is unable to catch its outputs streams.
	// First check the 64-bit program files directory:
	FString GitBinaryPath(TEXT("C:/Program Files/Git/bin/git.exe"));
	bool bFound = CheckGitAvailability(GitBinaryPath);
	if (!bFound)
	{
		// otherwise check the 32-bit program files directory.
		GitBinaryPath = TEXT("C:/Program Files (x86)/Git/bin/git.exe");
		bFound = CheckGitAvailability(GitBinaryPath);
	}
	if (!bFound)
	{
		// else the install dir for the current user: C:\Users\UserName\AppData\Local\Programs\Git\cmd
		const FString AppDataLocalPath = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		GitBinaryPath = FString::Printf(TEXT("%s/Programs/Git/cmd/git.exe"), *AppDataLocalPath);
		bFound = CheckGitAvailability(GitBinaryPath);
	}

	// 2) Else, look for the version of Git bundled with SmartGit "Installer with JRE"
	if (!bFound)
	{
		GitBinaryPath = TEXT("C:/Program Files (x86)/SmartGit/git/bin/git.exe");
		bFound = CheckGitAvailability(GitBinaryPath);
		if (!bFound)
		{
			// If git is not found in "git/bin/" subdirectory, try the "bin/" path that was in use before
			GitBinaryPath = TEXT("C:/Program Files (x86)/SmartGit/bin/git.exe");
			bFound = CheckGitAvailability(GitBinaryPath);
		}
	}

	// 3) Else, look for the local_git provided by SourceTree
	if (!bFound)
	{
		// C:\Users\UserName\AppData\Local\Atlassian\SourceTree\git_local\bin
		const FString AppDataLocalPath = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		GitBinaryPath = FString::Printf(TEXT("%s/Atlassian/SourceTree/git_local/bin/git.exe"), *AppDataLocalPath);
		bFound = CheckGitAvailability(GitBinaryPath);
	}

	// 4) Else, look for the PortableGit provided by GitHub Desktop
	if (!bFound)
	{
		// The latest GitHub Desktop adds its binaries into the local appdata directory:
		// C:\Users\UserName\AppData\Local\GitHub\PortableGit_c2ba306e536fdf878271f7fe636a147ff37326ad\cmd
		const FString AppDataLocalPath = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		const FString SearchPath = FString::Printf(TEXT("%s/GitHub/PortableGit_*"), *AppDataLocalPath);
		TArray<FString> PortableGitFolders;
		IFileManager::Get().FindFiles(PortableGitFolders, *SearchPath, false, true);
		if (PortableGitFolders.Num() > 0)
		{
			// FindFiles just returns directory names, so we need to prepend the root path to get the full path.
			GitBinaryPath = FString::Printf(TEXT("%s/GitHub/%s/cmd/git.exe"), *AppDataLocalPath, *(PortableGitFolders.Last())); // keep only the last PortableGit found
			bFound = CheckGitAvailability(GitBinaryPath);
			if (!bFound)
			{
				// If Portable git is not found in "cmd/" subdirectory, try the "bin/" path that was in use before
				GitBinaryPath = FString::Printf(TEXT("%s/GitHub/%s/bin/git.exe"), *AppDataLocalPath, *(PortableGitFolders.Last())); // keep only the last
				// PortableGit found
				bFound = CheckGitAvailability(GitBinaryPath);
			}
		}
	}

	// 5) Else, look for the version of Git bundled with Tower
	if (!bFound)
	{
		GitBinaryPath = TEXT("C:/Program Files (x86)/fournova/Tower/vendor/Git/bin/git.exe");
		bFound = CheckGitAvailability(GitBinaryPath);
	}

	// 6) Else, look for the PortableGit provided by Fork
	if (!bFound)
	{
		// The latest Fork adds its binaries into the local appdata directory:
		// C:\Users\UserName\AppData\Local\Fork\gitInstance\2.39.1\cmd
		const FString AppDataLocalPath = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		const FString SearchPath = FString::Printf(TEXT("%s/Fork/gitInstance/*"), *AppDataLocalPath);
		TArray<FString> PortableGitFolders;
		IFileManager::Get().FindFiles(PortableGitFolders, *SearchPath, false, true);
		if (PortableGitFolders.Num() > 0)
		{
			// FindFiles just returns directory names, so we need to prepend the root path to get the full path.
			GitBinaryPath = FString::Printf(TEXT("%s/Fork/gitInstance/%s/cmd/git.exe"), *AppDataLocalPath, *(PortableGitFolders.Last())); // keep only the last PortableGit found
			bFound = CheckGitAvailability(GitBinaryPath);
			if (!bFound)
			{
				// If Portable git is not found in "cmd/" subdirectory, try the "bin/" path that was in use before
				GitBinaryPath = FString::Printf(TEXT("%s/Fork/gitInstance/%s/bin/git.exe"), *AppDataLocalPath, *(PortableGitFolders.Last())); // keep only the last
				// PortableGit found
				bFound = CheckGitAvailability(GitBinaryPath);
			}
		}
	}

#elif PLATFORM_MAC
		// 1) First of all, look for the version of git provided by official git
		FString GitBinaryPath = TEXT("/usr/local/git/bin/git");
		bool bFound = CheckGitAvailability(GitBinaryPath);

		// 2) Else, look for the version of git provided by Homebrew
		if (!bFound)
		{
			GitBinaryPath = TEXT("/usr/local/bin/git");
			bFound = CheckGitAvailability(GitBinaryPath);
		}

		// 3) Else, look for the version of git provided by MacPorts
		if (!bFound)
		{
			GitBinaryPath = TEXT("/opt/local/bin/git");
			bFound = CheckGitAvailability(GitBinaryPath);
		}

		// 4) Else, look for the version of git provided by Command Line Tools
		if (!bFound)
		{
			GitBinaryPath = TEXT("/usr/bin/git");
			bFound = CheckGitAvailability(GitBinaryPath);
		}

		{
			SCOPED_AUTORELEASE_POOL;
			NSWorkspace* SharedWorkspace = [NSWorkspace sharedWorkspace];

			// 5) Else, look for the version of local_git provided by SmartGit
			if (!bFound)
			{
				NSURL* AppURL = [SharedWorkspace URLForApplicationWithBundleIdentifier:@"com.syntevo.smartgit"];
				if (AppURL != nullptr)
				{
					NSBundle* Bundle = [NSBundle bundleWithURL:AppURL];
					GitBinaryPath = FString::Printf(TEXT("%s/git/bin/git"), *FString([Bundle resourcePath]));
					bFound = CheckGitAvailability(GitBinaryPath);
				}
			}

			// 6) Else, look for the version of local_git provided by SourceTree
			if (!bFound)
			{
				NSURL* AppURL = [SharedWorkspace URLForApplicationWithBundleIdentifier:@"com.torusknot.SourceTreeNotMAS"];
				if (AppURL != nullptr)
				{
					NSBundle* Bundle = [NSBundle bundleWithURL:AppURL];
					GitBinaryPath = FString::Printf(TEXT("%s/git_local/bin/git"), *FString([Bundle resourcePath]));
					bFound = CheckGitAvailability(GitBinaryPath);
				}
			}

			// 7) Else, look for the version of local_git provided by GitHub Desktop
			if (!bFound)
			{
				NSURL* AppURL = [SharedWorkspace URLForApplicationWithBundleIdentifier:@"com.github.GitHubClient"];
				if (AppURL != nullptr)
				{
					NSBundle* Bundle = [NSBundle bundleWithURL:AppURL];
					GitBinaryPath = FString::Printf(TEXT("%s/app/git/bin/git"), *FString([Bundle resourcePath]));
					bFound = CheckGitAvailability(GitBinaryPath);
				}
			}

			// 8) Else, look for the version of local_git provided by Tower2
			if (!bFound)
			{
				NSURL* AppURL = [SharedWorkspace URLForApplicationWithBundleIdentifier:@"com.fournova.Tower2"];
				if (AppURL != nullptr)
				{
					NSBundle* Bundle = [NSBundle bundleWithURL:AppURL];
					GitBinaryPath = FString::Printf(TEXT("%s/git/bin/git"), *FString([Bundle resourcePath]));
					bFound = CheckGitAvailability(GitBinaryPath);
				}
			}
		}

#else
		FString GitBinaryPath = TEXT("/usr/bin/git");
		bool bFound = CheckGitAvailability(GitBinaryPath);
#endif

	if (bFound)
	{
		FPaths::MakePlatformFilename(GitBinaryPath);
	}
	else
	{
		// If we did not find a path to Git, set it empty
		GitBinaryPath.Empty();
	}

	return GitBinaryPath;
}

bool FGitLFSSourceControlUtils::UpdateFileStagingOnSaved(const FString& Filename)
{
	FGitLFSSourceControlModule* Module = FGitLFSSourceControlModule::GetThreadSafe();
	if (!Module)
	{
		return false;
	}

	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = Module->GetProvider();
	if (!Provider ||
		!Provider->IsGitAvailable())
	{
		return false;
	}

	const TSharedRef<FGitLFSSourceControlState> State = Provider->GetStateInternal(Filename);

	bool bResult = false;
	if (State->Changelist.GetName().Equals(TEXT("Staged")))
	{
		TArray<FString> Output, Errors;
		const FGitLFSCommandHelpers Helpers(*Provider);
		bResult = Helpers.RunAdd(false, { Filename }, Output, Errors);
	}

	return bResult;
}

bool FGitLFSSourceControlUtils::FindRootDirectory(const FString& Path, FString& OutRepositoryRoot)
{
	OutRepositoryRoot = Path;

	auto TrimTrailing = [](FString& Str, const TCHAR Char)
	{
		int32 Len = Str.Len();
		while (
			Len > 0 &&
			Str[Len - 1] == Char)
		{
			Str = Str.LeftChop(1);
			Len = Str.Len();
		}
	};

	TrimTrailing(OutRepositoryRoot, '\\');
	TrimTrailing(OutRepositoryRoot, '/');

	bool bFound = false;
	while (
		!bFound &&
		!OutRepositoryRoot.IsEmpty())
	{
		// Look for the ".git" subdirectory (or file) present at the root of every Git repository
		FString PathToGitSubdirectory = OutRepositoryRoot / TEXT(".git");
		bFound =
			IFileManager::Get().DirectoryExists(*PathToGitSubdirectory) ||
			IFileManager::Get().FileExists(*PathToGitSubdirectory);
		if (!bFound)
		{
			int32 LastSlashIndex = -1;
			if (OutRepositoryRoot.FindLastChar('/', LastSlashIndex))
			{
				OutRepositoryRoot = OutRepositoryRoot.Left(LastSlashIndex);
			}
			else
			{
				OutRepositoryRoot.Empty();
			}
		}
	}
	if (!bFound)
	{
		OutRepositoryRoot = Path; // If not found, return the provided dir as best possible root.
	}
	return bFound;
}

bool FGitLFSSourceControlUtils::CheckGitAvailability(const FString& InPathToGitBinary, FGitLFSVersion* OutVersion)
{
	const FGitLFSCommandHelpers Helpers(InPathToGitBinary, "");

	FString ResultString;
	if (!Helpers.RunGitVersion(ResultString))
	{
		return false;
	}

	if (!ResultString.StartsWith("git version"))
	{
		return false;
	}

	if (OutVersion)
	{
		ParseGitVersion(ResultString, OutVersion);
	}

	return true;
}

bool FGitLFSSourceControlUtils::CollectNewStates(const TMap<FString, FGitLFSSourceControlState>& States, TMap<const FString, FGitLFSState>& OutResults)
{
	for (const auto& It : States)
	{
		OutResults.Add(It.Key, It.Value.State);
	}

	return States.Num() > 0;
}

bool FGitLFSSourceControlUtils::UpdateCachedStates(const TMap<const FString, FGitLFSState>& States)
{
	if (States.Num() == 0)
	{
		return false;
	}

	FGitLFSSourceControlModule* GitSourceControl = FGitLFSSourceControlModule::GetThreadSafe();
	if (!GitSourceControl)
	{
		return false;
	}

	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl->GetProvider();
	if (!ensure(Provider))
	{
		return false;
	}

	// TODO without LFS : Workaround a bug with the Source Control Module not updating file state after a simple "Save" with no "Checkout" (when not using File Lock)
	const FDateTime Now = Provider->UsesCheckout() ? FDateTime::Now() : FDateTime::MinValue();

	for (const auto& It : States)
	{
		const TSharedRef<FGitLFSSourceControlState> ControlState = Provider->GetStateInternal(It.Key);

		const FGitLFSState& State = It.Value;

		if (State.FileState != EGitLFSFileState::Unset)
		{
			// Invalid transition
			if (State.FileState == EGitLFSFileState::Added &&
				!ControlState->IsUnknown() &&
				!ControlState->CanAdd())
			{
				continue;
			}
			ControlState->State.FileState = State.FileState;
		}

		if (State.TreeState != EGitLFSTreeState::Unset)
		{
			ControlState->State.TreeState = State.TreeState;
		}

		// If we're updating lock state, also update user
		if (State.LockState != EGitLFSLockState::Unset)
		{
			ControlState->State.LockState = State.LockState;
			ControlState->State.LockUser = State.LockUser;
		}

		if (State.RemoteState != EGitLFSRemoteState::Unset)
		{
			ControlState->State.RemoteState = State.RemoteState;
			if (State.RemoteState == EGitLFSRemoteState::UpToDate)
			{
				ControlState->State.HeadBranch = TEXT("");
			}
			else
			{
				ControlState->State.HeadBranch = State.HeadBranch;
			}
		}
		ControlState->TimeStamp = Now;

		// We've just updated the state, no need for UpdateStatus to be ran for this file again.
		Provider->AddFileToIgnoreForceCache(ControlState->LocalFilename);
	}

	return true;
}

bool FGitLFSSourceControlUtils::UpdateChangelistStateByCommand()
{
	FGitLFSSourceControlModule& GitSourceControl = FModuleManager::GetModuleChecked<FGitLFSSourceControlModule>("GitLFSSourceControl");
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl.GetProvider();
	if (!Provider.IsValid() ||
		!Provider->IsGitAvailable())
	{
		return false;
	}

	const TSharedRef<FGitLFSSourceControlChangelistState> StagedChangelist = Provider->GetStateInternal(FGitLFSSourceControlChangelist::StagedChangelist);
	const TSharedRef<FGitLFSSourceControlChangelistState> WorkingChangelist = Provider->GetStateInternal(FGitLFSSourceControlChangelist::WorkingChangelist);
	StagedChangelist->Files.RemoveAll([](const FSourceControlStateRef&) { return true; });
	WorkingChangelist->Files.RemoveAll([](const FSourceControlStateRef&) { return true; });

	TArray<FString> Results;
	TArray<FString> Errors;

	const FGitLFSCommandHelpers Helpers(*Provider);
	Helpers.GetStatusNoLocks(false, { TEXT("Content/") }, Results, Errors);

	for (const FString& Result : Results)
	{
		FString File = GetFullPathFromGitStatus(Result, Provider->GetPathToRepositoryRoot());
		TSharedRef<FGitLFSSourceControlState> State = Provider->GetStateInternal(File);

		// Staged check
		if (!TChar<TCHAR>::IsWhitespace(Result[0]))
		{
			WorkingChangelist->Files.Remove(State);
			UpdateFileStagingOnSaved(Result);
			State->Changelist = FGitLFSSourceControlChangelist::StagedChangelist;
			StagedChangelist->Files.AddUnique(State);
			continue;
		}
		// Working check
		if (!TChar<TCHAR>::IsWhitespace(Result[1]))
		{
			StagedChangelist->Files.Remove(State);
			State->Changelist = FGitLFSSourceControlChangelist::WorkingChangelist;
			WorkingChangelist->Files.AddUnique(State);
		}
	}
	return true;
}

void FGitLFSSourceControlUtils::AbsoluteFilenames(const FString& RepositoryRoot, TArray<FString>& FileNames)
{
	for (FString& FileName : FileNames)
	{
		FileName = FPaths::ConvertRelativePathToFull(RepositoryRoot, FileName);
	}
}

TArray<FString> FGitLFSSourceControlUtils::AbsoluteFilenames(const TArray<FString>& InFileNames, const FString& InRelativeTo)
{
	TArray<FString> AbsFiles;

	for (const FString& FileName : InFileNames) // string copy to be able to convert it inplace
	{
		AbsFiles.Add(FPaths::Combine(InRelativeTo, FileName));
	}

	return AbsFiles;
}

void FGitLFSSourceControlUtils::ReloadPackages(TArray<UPackage*>& InPackagesToReload)
{
	// UE-COPY: ContentBrowserUtils::SyncPathsFromSourceControl()
	// Syncing may have deleted some packages, so we need to unload those rather than re-load them...
	TArray<UPackage*> PackagesToUnload;
	InPackagesToReload.RemoveAll([&](UPackage* InPackage) -> bool
	{
		const FString PackageExtension = InPackage->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(InPackage->GetName(), PackageExtension);
		if (!FPaths::FileExists(PackageFilename))
		{
			PackagesToUnload.Emplace(InPackage);
			return true; // remove package
		}
		return false; // keep package
	});

	// Hot-reload the new packages...
	UPackageTools::ReloadPackages(InPackagesToReload);

	// Unload any deleted packages...
	UPackageTools::UnloadPackages(PackagesToUnload);
}

TArray<FString> FGitLFSSourceControlUtils::GetLockedFiles(const TArray<FString>& Files)
{
	TArray<FString> Result;
	FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl.GetProvider();
	if (!Provider)
	{
		return Result;
	}

	TArray<TSharedRef<ISourceControlState>> LocalStates;
	Provider->GetState(Files, LocalStates, EStateCacheUsage::Use);
	for (const TSharedRef<ISourceControlState>& State : LocalStates)
	{
		const TSharedRef<FGitLFSSourceControlState>& GitState = StaticCastSharedRef<FGitLFSSourceControlState>(State);
		if (GitState->State.LockState == EGitLFSLockState::Locked)
		{
			Result.Add(GitState->GetFilename());
		}
	}

	return Result;
}

TArray<FString> FGitLFSSourceControlUtils::RelativeFilenames(const TArray<FString>& InFileNames, const FString& InRelativeTo)
{
	TArray<FString> RelativeFiles;
	FString RelativeTo = InRelativeTo;

	// Ensure that the path ends w/ '/'
	if (RelativeTo.Len() > 0 &&
		!RelativeTo.EndsWith(TEXT("/"), ESearchCase::CaseSensitive) &&
		!RelativeTo.EndsWith(TEXT("\\"), ESearchCase::CaseSensitive))
	{
		RelativeTo += TEXT("/");
	}

	for (FString FileName : InFileNames) // string copy to be able to convert it inplace
	{
		if (FPaths::MakePathRelativeTo(FileName, *RelativeTo))
		{
			RelativeFiles.Add(FileName);
		}
	}

	return RelativeFiles;
}

bool FGitLFSSourceControlUtils::GetHistory(const FGitLFSSourceControlCommand& Command, const FString& InFile, bool bMergeConflict, TArray<FString>& OutErrorMessages, TArray<TSharedRef<FGitLFSSourceControlRevision>>& OutHistory)
{
	const FGitLFSCommandHelpers Helpers(Command);

	bool bResults = false;
	{
		TArray<FString> Results;
		TArray<FString> Parameters;
		// follow file renames
		Parameters.Add(TEXT("--follow"));
		Parameters.Add(TEXT("--date=raw"));
		// relative filename at this revision, preceded by a status character
		Parameters.Add(TEXT("--name-status"));
		// make sure format matches expected in ParseLogResults
		Parameters.Add(TEXT("--pretty=medium"));

		if (bMergeConflict)
		{
			// In case of a merge conflict, we also need to get the tip of the "remote branch" (MERGE_HEAD) before the log of the "current branch" (HEAD)
			// @todo does not work for a cherry-pick! Test for a rebase.
			Parameters.Add(TEXT("MERGE_HEAD"));
			Parameters.Add(TEXT("--max-count 1"));
		}
		else
		{
			// Increase default count to 250 from 100
			Parameters.Add(TEXT("--max-count 250"));
		}

		TArray<FString> Files;
		Files.Add(*InFile);
		if (Helpers.GetLog(Parameters, { InFile }, Results, OutErrorMessages))
		{
			bResults = true;
			ParseLogResults(Results, OutHistory);
		}
	}

	for (const TSharedRef<FGitLFSSourceControlRevision>& Revision : OutHistory)
	{
		// Get file (blob) sha1 id and size
		TArray<FString> Results;
		bResults &= Helpers.RunLSTree({ TEXT("--long"), Revision->GetRevision() }, Revision->GetFilename(), Results, OutErrorMessages);

		if (bResults)
		{
			ParseLSTreeOutput(Results, Revision->FileHash, Revision->FileSize);
		}

		Revision->PathToRepoRoot = Command.PathToRepositoryRoot;
	}

	return bResults;
}

bool FGitLFSSourceControlUtils::CollectNewStates(
	const TArray<FString>& Files,
	TMap<const FString, FGitLFSState>& OutResults,
	const EGitLFSFileState FileState,
	const EGitLFSTreeState TreeState,
	const EGitLFSLockState LockState,
	const EGitLFSRemoteState RemoteState)
{
	if (Files.Num() == 0)
	{
		return false;
	}

	FGitLFSState NewState;
	NewState.FileState = FileState;
	NewState.TreeState = TreeState;
	NewState.LockState = LockState;
	NewState.RemoteState = RemoteState;

	for (const FString& File : Files)
	{
		FGitLFSState& State = OutResults.FindOrAdd(File, NewState);
		if (NewState.FileState != EGitLFSFileState::Unset)
		{
			State.FileState = NewState.FileState;
		}
		if (NewState.TreeState != EGitLFSTreeState::Unset)
		{
			State.TreeState = NewState.TreeState;
		}
		if (NewState.LockState != EGitLFSLockState::Unset)
		{
			State.LockState = NewState.LockState;
		}
		if (NewState.RemoteState != EGitLFSRemoteState::Unset)
		{
			State.RemoteState = NewState.RemoteState;
		}
	}

	return true;
}

void FGitLFSSourceControlUtils::ParseLogResults(
	const TArray<FString>& InResults,
	TArray<TSharedRef<FGitLFSSourceControlRevision>>& OutHistory)
{
	TSharedRef<FGitLFSSourceControlRevision> SourceControlRevision = MakeShared<FGitLFSSourceControlRevision>();
	for (const FString& Result : InResults)
	{
		if (Result.StartsWith(TEXT("commit "))) // Start of a new commit
		{
			// End of the previous commit
			if (SourceControlRevision->RevisionNumber != 0)
			{
				OutHistory.Add(MoveTemp(SourceControlRevision));
				SourceControlRevision = MakeShared<FGitLFSSourceControlRevision>();
			}
			// Full commit SHA1 hexadecimal string
			SourceControlRevision->CommitId = Result.RightChop(7);
			// Short revision ; first 8 hex characters (max that can hold a 32
			SourceControlRevision->ShortCommitId = SourceControlRevision->CommitId.Left(8);
			// bit integer)
			SourceControlRevision->CommitIdNumber = FParse::HexNumber(*SourceControlRevision->ShortCommitId);
			// RevisionNumber will be set at the end, based off the index in the History
			SourceControlRevision->RevisionNumber = -1;
		}
		// Author name & email
		else if (Result.StartsWith(TEXT("Author: ")))
		{
			// Remove the 'email' part of the UserName
			FString UserNameEmail = Result.RightChop(8);
			int32 EmailIndex = 0;
			if (UserNameEmail.FindLastChar('<', EmailIndex))
			{
				SourceControlRevision->UserName = UserNameEmail.Left(EmailIndex - 1);
			}
		}
		// Commit date
		else if (Result.StartsWith(TEXT("Date:   ")))
		{
			FString Date = Result.RightChop(8);
			SourceControlRevision->Date = FDateTime::FromUnixTimestamp(FCString::Atoi(*Date));
		}
		//	else if(Result.IsEmpty()) // empty line before/after commit message has already been taken care by FString::ParseIntoArray()
		// Multi-lines commit message
		else if (Result.StartsWith(TEXT("    ")))
		{
			SourceControlRevision->Description += Result.RightChop(4);
			SourceControlRevision->Description += TEXT("\n");
		}
		// Name of the file, starting with an uppercase status letter ("A"/"M"...)
		else
		{
			const TCHAR Status = Result[0];
			// Readable action string ("Added", Modified"...) instead of "A"/"M"...
			switch (Status)
			{
			case TEXT(' '): SourceControlRevision->Action = FString("unmodified"); break;
			case TEXT('M'): SourceControlRevision->Action = FString("modified"); break;
				// added: keyword "add" to display a specific icon instead of the default "edit" action one
			case TEXT('A'): SourceControlRevision->Action = FString("add"); break;
				// deleted: keyword "delete" to display a specific icon instead of the default "edit" action one
			case TEXT('D'): SourceControlRevision->Action = FString("delete"); break;
				// renamed keyword "branch" to display a specific icon instead of the default "edit" action one
			case TEXT('R'): SourceControlRevision->Action = FString("branch"); break;
				// copied keyword "branch" to display a specific icon instead of the default "edit" action one
			case TEXT('C'): SourceControlRevision->Action = FString("branch"); break;
			case TEXT('T'): SourceControlRevision->Action = FString("type changed"); break;
			case TEXT('U'): SourceControlRevision->Action = FString("unmerged"); break;
			case TEXT('X'): SourceControlRevision->Action = FString("unknown"); break;
			case TEXT('B'): SourceControlRevision->Action = FString("broked pairing"); break;
			default: SourceControlRevision->Action = {}; break;
			}
			// Take care of special case for Renamed/Copied file: extract the second filename after second tabulation
			int32 IdxTab = -1;
			if (Result.FindLastChar('\t', IdxTab))
			{
				// relative filename
				SourceControlRevision->Filename = Result.RightChop(IdxTab + 1);
			}
		}
	}
	// End of the last commit
	if (SourceControlRevision->RevisionNumber != 0)
	{
		OutHistory.Add(MoveTemp(SourceControlRevision));
	}

	// Then set the revision number of each Revision based on its index (reverse order since the log starts with the most recent change)
	for (int32 Index = 0; Index < OutHistory.Num(); Index++)
	{
		const TSharedRef<FGitLFSSourceControlRevision>& SourceControlRevisionItem = OutHistory[Index];
		SourceControlRevisionItem->RevisionNumber = OutHistory.Num() - Index;

		// Special case of a move ("branch" in Perforce term): point to the previous change (so the next one in the order of the log)
		if (SourceControlRevisionItem->Action == "branch" &&
			Index < OutHistory.Num() - 1)
		{
			SourceControlRevisionItem->BranchSource = OutHistory[Index + 1];
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// Run a batch of Git "status" command to update status of given files and/or directories.
bool FGitLFSSourceControlUtils::RunUpdateStatus(
	const FString& PathToGit,
	const FString& PathToRepository,
	bool bUsingGitLFSLocking,
	const TArray<FString>& InFiles,
	bool bCollectNewStates,
	TArray<FString>& OutErrorMessages,
	TMap<const FString, FGitLFSState>& OutStates,
	TMap<FString, FGitLFSSourceControlState>* OutControlStates)
{
	TMap<FString, FGitLFSSourceControlState> NewStates;
	// Remove files that aren't in the repository
	const TArray<FString>& RepoFiles = InFiles.FilterByPredicate([&PathToRepository](const FString& File)
	{
		return File.StartsWith(PathToRepository);
	});

	if (!RepoFiles.Num())
	{
		return false;
	}

	// We skip checking ignored since no one ignores files that Unreal would read in as revision controlled (Content/{*.uasset,*.umap},Config/*.ini).
	TArray<FString> Results;

	const FGitLFSCommandHelpers Helpers(PathToGit, PathToRepository);

	// avoid locking the index when not needed (useful for status updates)
	const bool bSuccess = Helpers.GetStatusNoLocks(true, RepoFiles, Results, OutErrorMessages);

	TMap<FString, FString> ResultsMap;
	for (const FString& Result : Results)
	{
		const FString& RelativeFilename = FilenameFromGitStatus(Result);
		const FString& File = FPaths::ConvertRelativePathToFull(PathToRepository, RelativeFilename);
		ResultsMap.Add(File, Result);
	}

	if (bSuccess)
	{
		ParseStatusResults(PathToGit, PathToRepository, bUsingGitLFSLocking, RepoFiles, ResultsMap, NewStates);
	}

	UpdateChangelistStateByCommand();

	CheckRemote(PathToGit, PathToRepository, OutErrorMessages, NewStates);

	if (bSuccess)
	{
		if (OutControlStates)
		{
			*OutControlStates = NewStates;
		}
		if (bCollectNewStates)
		{
			CollectNewStates(NewStates, OutStates);
		}
	}

	return bSuccess;
}

void FGitLFSSourceControlUtils::CheckRemote(
	const FString& PathToGitBinary,
	const FString& RepositoryRoot,
	TArray<FString>& OutErrorMessages,
	TMap<FString, FGitLFSSourceControlState>& OutStates)
{
	// We can obtain a list of files that were modified between our remote branches and HEAD. Assumes that fetch has been run to get accurate info.

	// Gather valid remote branches
	FGitLFSSourceControlModule* GitSourceControl = FGitLFSSourceControlModule::GetThreadSafe();
	if (!GitSourceControl)
	{
		return;
	}

	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl->GetProvider();
	if (!Provider)
	{
		return;
	}

	const TArray<FString> StatusBranches = Provider->GetStatusBranchNames();

	TSet<FString> BranchesToDiff{StatusBranches};

	bool bDiffAgainstRemoteCurrent = false;

	FGitLFSCommandHelpers Helpers(PathToGitBinary, RepositoryRoot);

	// Get the current branch's remote.
	FString CurrentBranchName;
	if (Helpers.GetRemoteBranchName(CurrentBranchName))
	{
		// We have a valid remote, so diff against it.
		bDiffAgainstRemoteCurrent = true;
		// Ensure that the remote branch is in there.
		BranchesToDiff.Add(CurrentBranchName);
	}

	if (BranchesToDiff.Num() == 0)
	{
		return;
	}

	TArray<FString> ErrorMessages;

	TMap<FString, FString> NewerFiles;

	// const TArray<FString>& RelativeFiles = RelativeFilenames(Files, InRepositoryRoot);
	// Get the full remote status of the Content folder, since it's the only lockable folder we track in editor. 
	// This shows any new files as well.
	// Also update the status of `.checksum`.
	TArray<FString> FilesToDiff{ FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), ".checksum", "Binaries/", "Plugins/" };
	TArray<FString> Parameters{ TEXT("--pretty="), TEXT("--name-only"), TEXT(""), TEXT("--") };
	for (const FString& Branch : BranchesToDiff)
	{
		const bool bCurrentBranch =
			bDiffAgainstRemoteCurrent &&
			Branch.Equals(CurrentBranchName);

		// empty defaults to HEAD
		// .. means commits in the right that are not in the left
		Parameters[2] = FString::Printf(TEXT("..%s"), *Branch);

		TArray<FString> Results;
		if (!Helpers.GetLog(Parameters, FilesToDiff, Results, ErrorMessages))
		{
			continue;
		}

		for (const FString& NewerFileName : Results)
		{
			// Don't care about mergeable files (.collection, .ini, .uproject, etc)
			if (!FGitLFSCommandHelpers::IsFileLFSLockable(NewerFileName))
			{
				const bool bNewerFileMatches =
					NewerFileName == TEXT(".checksum") ||
					NewerFileName.StartsWith("Binaries/", ESearchCase::IgnoreCase) ||
					NewerFileName.StartsWith("Plugins/", ESearchCase::IgnoreCase);

				// Check if there's newer binaries pending on this branch
				if (bCurrentBranch &&
					bNewerFileMatches)
				{
					Provider->bPendingRestart = true;
				}
				continue;
			}

			const FString& NewerFilePath = FPaths::ConvertRelativePathToFull(RepositoryRoot, NewerFileName);
			if (bCurrentBranch ||
				!NewerFiles.Contains(NewerFilePath))
			{
				NewerFiles.Add(NewerFilePath, Branch);
			}
		}
	}

	for (const auto& It : NewerFiles)
	{
		if (FGitLFSSourceControlState* FileState = OutStates.Find(It.Key))
		{
			FileState->State.RemoteState = It.Value.Equals(CurrentBranchName) ? EGitLFSRemoteState::NotAtHead : EGitLFSRemoteState::NotLatest;
			FileState->State.HeadBranch = It.Value;
		}
	}

	OutErrorMessages.Append(ErrorMessages);
}

void FGitLFSSourceControlUtils::RemoveRedundantErrors(FGitLFSSourceControlCommand& Command, const FString& Filter)
{
	struct FRemoveRedundantErrors
	{
		FRemoveRedundantErrors(const FString& InFilter)
			: Filter(InFilter)
		{
		}

		bool operator()(const FString& String) const
		{
			return String.Contains(Filter);
		}

		/** The filter string we try to identify in the reported error */
		FString Filter;
	};

	bool bFoundRedundantError = false;
	for (const FString& Message : Command.ResultInfo.ErrorMessages)
	{
		if (Message.Contains(Filter))
		{
			Command.ResultInfo.InfoMessages.Add(Message);
			bFoundRedundantError = true;
		}
	}

	Command.ResultInfo.ErrorMessages.RemoveAll(FRemoveRedundantErrors(Filter));

	// if we have no error messages now, assume success!
	if (bFoundRedundantError &&
		Command.ResultInfo.ErrorMessages.Num() == 0 &&
		!Command.bCommandSuccessful)
	{
		Command.bCommandSuccessful = true;
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/**
 * @brief Extract the relative filename from a Git status result.
 *
 * Examples of status results:
 * M  Content/Textures/T_Perlin_Noise_M.uasset
 * R  Content/Textures/T_Perlin_Noise_M.uasset -> Content/Textures/T_Perlin_Noise_M2.uasset
 * ?? Content/Materials/M_Basic_Wall.uasset
 * !! BasicCode.sln
 *
 * @param[in] Result One line of status
 * @return Relative filename extracted from the line of status
 *
 * @see FGitStatusFileMatcher and StateFromGitStatus()
 */
FString FGitLFSSourceControlUtils::FilenameFromGitStatus(const FString& Result)
{
	int32 RenameIndex = -1;
	if (Result.FindLastChar('>', RenameIndex))
	{
		// Extract only the second part of a rename "from -> to"
		return Result.RightChop(RenameIndex + 2);
	}
	else
	{
		// Extract the relative filename from the Git status result (after the 2 letters status and 1 space)
		return Result.RightChop(3);
	}
}

FString FGitLFSSourceControlUtils::GetFullPathFromGitStatus(const FString& FilePath, const FString& RepositoryRoot)
{
	const FString& RelativeFilename = FilenameFromGitStatus(FilePath);
	FString File = FPaths::ConvertRelativePathToFull(RepositoryRoot, RelativeFilename);
	return File;
}

void FGitLFSSourceControlUtils::ParseStatusResults(
	const FString& InPathToGitBinary,
	const FString& InRepositoryRoot,
	const bool InUsingLfsLocking,
	const TArray<FString>& InFiles,
	const TMap<FString, FString>& InResults,
	TMap<FString, FGitLFSSourceControlState>& OutStates)
{
	const FGitLFSCommandHelpers Helpers(InPathToGitBinary, InRepositoryRoot);
	TSet<FString> Files;
	for (const FString& File : InFiles)
	{
		if (FPaths::DirectoryExists(File))
		{
			TArray<FString> DirectoryFiles;
			if (Helpers.RunLSFiles(false, File, DirectoryFiles))
			{
				AbsoluteFilenames(InRepositoryRoot, DirectoryFiles);
				for (const FString& InnerFile : DirectoryFiles)
				{
					Files.Add(InnerFile);
				}
			}
		}
		else
		{
			Files.Add(File);
		}
	}

	ParseFileStatusResult(InPathToGitBinary, InRepositoryRoot, InUsingLfsLocking, Files, InResults, OutStates);
}

void FGitLFSSourceControlUtils::ParseDirectoryStatusResult(
	const bool InUsingLfsLocking,
	const TMap<FString, FString>& Results,
	TMap<FString, FGitLFSSourceControlState>& OutStates)
{
	// Iterate on each line of result of the status command
	for (const auto& It : Results)
	{
		FGitLFSSourceControlState ControlState(It.Key);
		if (!InUsingLfsLocking)
		{
			ControlState.State.LockState = EGitLFSLockState::Unlockable;
		}
		EGitLFSFileState FileState;
		EGitLFSTreeState TreeState;
		ParseGitStatus(It.Value, FileState, TreeState);

		if (EGitLFSFileState::Deleted == FileState ||
			EGitLFSFileState::Missing == FileState ||
			EGitLFSTreeState::Untracked == TreeState)
		{
			ControlState.State.FileState = FileState;
			ControlState.State.TreeState = TreeState;
			OutStates.Add(It.Key, MoveTemp(ControlState));
		}
	}
}

void FGitLFSSourceControlUtils::ParseGitStatus(const FString& Line, EGitLFSFileState& OutFileState, EGitLFSTreeState& OutTreeState)
{
	const TCHAR IndexState = Line[0];
	const TCHAR WCopyState = Line[1];
	if ((IndexState == 'U' || WCopyState == 'U') ||
		(IndexState == 'A' && WCopyState == 'A') ||
		(IndexState == 'D' && WCopyState == 'D'))
	{
		// "Unmerged" conflict cases are generally marked with a "U",
		// but there are also the special cases of both "A"dded, or both "D"eleted
		OutFileState = EGitLFSFileState::Unmerged;
		OutTreeState = EGitLFSTreeState::Working;
		return;
	}

	if (IndexState == ' ')
	{
		OutTreeState = EGitLFSTreeState::Working;
	}
	else if (WCopyState == ' ')
	{
		OutTreeState = EGitLFSTreeState::Staged;
	}

	if (IndexState == '?' ||
		WCopyState == '?')
	{
		OutTreeState = EGitLFSTreeState::Untracked;
		OutFileState = EGitLFSFileState::Unknown;
	}
	else if (
		IndexState == '!' ||
		WCopyState == '!')
	{
		OutTreeState = EGitLFSTreeState::Ignored;
		OutFileState = EGitLFSFileState::Unknown;
	}
	else if (IndexState == 'A')
	{
		OutFileState = EGitLFSFileState::Added;
	}
	else if (IndexState == 'D')
	{
		OutFileState = EGitLFSFileState::Deleted;
	}
	else if (WCopyState == 'D')
	{
		OutFileState = EGitLFSFileState::Missing;
	}
	else if (
		IndexState == 'M' ||
		WCopyState == 'M')
	{
		OutFileState = EGitLFSFileState::Modified;
	}
	else if (IndexState == 'R')
	{
		OutFileState = EGitLFSFileState::Renamed;
	}
	else if (IndexState == 'C')
	{
		OutFileState = EGitLFSFileState::Copied;
	}
	else
	{
		// Unmodified never yield a status
		OutFileState = EGitLFSFileState::Unknown;
	}
}

void FGitLFSSourceControlUtils::ParseFileStatusResult(
	const FString& InPathToGitBinary,
	const FString& InRepositoryRoot,
	const bool InUsingLfsLocking,
	const TSet<FString>& InFiles,
	const TMap<FString, FString>& InResults,
	TMap<FString, FGitLFSSourceControlState>& OutStates)
{
	FGitLFSSourceControlModule* GitSourceControl = FGitLFSSourceControlModule::GetThreadSafe();
	if (!GitSourceControl)
	{
		return;
	}

	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl->GetProvider();
	if (!ensure(Provider))
	{
		return;
	}

	const FGitLFSCommandHelpers Helpers(InPathToGitBinary, InRepositoryRoot);

	const FString& LfsUserName = Provider->GetLockUser();

	TMap<FString, FString> LockedFiles;
	TMap<FString, FString> Results = InResults;
	bool bCheckedLockedFiles = false;

	FString Result;

	// Iterate on all files explicitly listed in the command
	for (const FString& File : InFiles)
	{
		FGitLFSSourceControlState ControlState(File);
		ControlState.State.FileState = EGitLFSFileState::Unset;
		ControlState.State.TreeState = EGitLFSTreeState::Unset;
		ControlState.State.LockState = EGitLFSLockState::Unset;
		// Search the file in the list of status
		if (Results.RemoveAndCopyValue(File, Result))
		{
			EGitLFSFileState FileState;
			EGitLFSTreeState TreeState;
			ParseGitStatus(Result, FileState, TreeState);

#if UE_BUILD_DEBUG && GIT_DEBUG_STATUS
			UE_LOG(LogSourceControl, Log, TEXT("Status(%s) = '%s' => File:%d, Tree:%d"), *File, *Result, static_cast<int>(FileState), static_cast<int>(TreeState));
#endif

			ControlState.State.FileState = FileState;
			ControlState.State.TreeState = TreeState;
			if (ControlState.IsConflicted())
			{
				TArray<FString> InnerResults;
				const bool bResult = Helpers.RunLSFiles(true, File, InnerResults);
				if (bResult &&
					InnerResults.Num() == 3)
				{
					// Parse the unmerge status: extract the base revision (or the other branch?)
					FGitConflictStatusParser ConflictStatus(InnerResults);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
					ControlState.PendingResolveInfo.BaseFile = ConflictStatus.CommonAncestorFilename;
					ControlState.PendingResolveInfo.BaseRevision = ConflictStatus.CommonAncestorFileId;
					ControlState.PendingResolveInfo.RemoteFile = ConflictStatus.RemoteFilename;
					ControlState.PendingResolveInfo.RemoteRevision = ConflictStatus.RemoteFileId;
#else
					ControlState.PendingMergeBaseFileHash = ConflictStatus.CommonAncestorFileId;
#endif
				}
			}
		}
		else
		{
			ControlState.State.FileState = EGitLFSFileState::Unknown;
			// File not found in status
			if (FPaths::FileExists(File))
			{
				// usually means the file is unchanged,
				ControlState.State.TreeState = EGitLFSTreeState::Unmodified;
#if UE_BUILD_DEBUG && GIT_DEBUG_STATUS
				UE_LOG(LogSourceControl, Log, TEXT("Status(%s) not found but exists => unchanged"), *File);
#endif
			}
			else
			{
				// but also the case for newly created content: there is no file on disk until the content is saved for the first time
				ControlState.State.TreeState = EGitLFSTreeState::NotInRepo;
#if UE_BUILD_DEBUG && GIT_DEBUG_STATUS
				UE_LOG(LogSourceControl, Log, TEXT("Status(%s) not found and does not exists => new/not controled"), *File);
#endif
			}
		}

		if (!InUsingLfsLocking)
		{
			ControlState.State.LockState = EGitLFSLockState::Unlockable;
		}
		else
		{
			if (FGitLFSCommandHelpers::IsFileLFSLockable(File))
			{
				if (!bCheckedLockedFiles)
				{
					bCheckedLockedFiles = true;
					TArray<FString> ErrorMessages;
					FGitLFSLockedFilesCache::GetAllLocks(InRepositoryRoot, InPathToGitBinary, ErrorMessages, LockedFiles, false);
					FTSMessageLog SourceControlLog("SourceControl");
					for (int32 ErrorIndex = 0; ErrorIndex < ErrorMessages.Num(); ++ErrorIndex)
					{
						SourceControlLog.Error(FText::FromString(ErrorMessages[ErrorIndex]));
					}
				}
				if (LockedFiles.Contains(File))
				{
					ControlState.State.LockUser = LockedFiles[File];
					if (LfsUserName == ControlState.State.LockUser)
					{
						ControlState.State.LockState = EGitLFSLockState::Locked;
					}
					else
					{
						ControlState.State.LockState = EGitLFSLockState::LockedOther;
					}
				}
				else
				{
					ControlState.State.LockState = EGitLFSLockState::NotLocked;
#if UE_BUILD_DEBUG && GIT_DEBUG_STATUS
					UE_LOG(LogSourceControl, Log, TEXT("Status(%s) Not Locked"), *File);
#endif
				}
			}
			else
			{
				ControlState.State.LockState = EGitLFSLockState::Unlockable;
			}


#if UE_BUILD_DEBUG && GIT_DEBUG_STATUS
			UE_LOG(LogSourceControl, Log, TEXT("Status(%s) Locked by '%s'"), *File, *ControlState.State.LockUser);
#endif
		}
		OutStates.Add(File, MoveTemp(ControlState));
	}

	// The above cannot detect deleted assets since there is no file left to enumerate (either by the Content Browser or by git ls-files)
	// => so we also parse the status results to explicitly look for Deleted/Missing assets
	ParseDirectoryStatusResult(InUsingLfsLocking, Results, OutStates);
}

void FGitLFSSourceControlUtils::ParseGitVersion(const FString& InVersionString, FGitLFSVersion* OutVersion)
{
#if UE_BUILD_DEBUG
	// Parse "git version 2.31.1.vfs.0.3" into the string "2.31.1.vfs.0.3"
	const FString& TokenVersionStringPtr = InVersionString.RightChop(12);
	if (TokenVersionStringPtr.IsEmpty())
	{
		return;
	}

	// Parse the version into its numerical components
	TArray<FString> ParsedVersionString;
	TokenVersionStringPtr.ParseIntoArray(ParsedVersionString, TEXT("."));
	const int Num = ParsedVersionString.Num();
	if (Num < 3)
	{
		return;
	}

	if (!ParsedVersionString[0].IsNumeric() ||
		!ParsedVersionString[1].IsNumeric() ||
		!ParsedVersionString[2].IsNumeric())
	{
		return;
	}

	OutVersion->Major = FCString::Atoi(*ParsedVersionString[0]);
	OutVersion->Minor = FCString::Atoi(*ParsedVersionString[1]);
	OutVersion->Patch = FCString::Atoi(*ParsedVersionString[2]);

	if (Num >= 5)
	{
		// If labeled with fork
		if (!ParsedVersionString[3].IsNumeric())
		{
			OutVersion->Fork = ParsedVersionString[3];
			OutVersion->bIsFork = true;
			OutVersion->ForkMajor = FCString::Atoi(*ParsedVersionString[4]);
			if (Num >= 6)
			{
				OutVersion->ForkMinor = FCString::Atoi(*ParsedVersionString[5]);
				if (Num >= 7)
				{
					OutVersion->ForkPatch = FCString::Atoi(*ParsedVersionString[6]);
				}
			}
		}
	}

	if (OutVersion->bIsFork)
	{
		UE_LOG(LogSourceControl, Log, TEXT("Git version %d.%d.%d.%s.%d.%d.%d"), OutVersion->Major, OutVersion->Minor, OutVersion->Patch, *OutVersion->Fork, OutVersion->ForkMajor, OutVersion->ForkMinor, OutVersion->ForkPatch);
	}
	else
	{
		UE_LOG(LogSourceControl, Log, TEXT("Git version %d.%d.%d"), OutVersion->Major, OutVersion->Minor, OutVersion->Patch);
	}
#endif
}

void FGitLFSSourceControlUtils::ParseLSTreeOutput(const TArray<FString>& Output, FString& OutFileHash, int32& OutFileSize)
{
	if (Output.Num() == 0)
	{
		return;
	}

	const FString& FirstResult = Output[0];
	OutFileHash = FirstResult.Mid(12, 40);

	int32 TabIndex = -1;
	if (FirstResult.FindChar('\t', TabIndex))
	{
		const FString SizeString = FirstResult.Mid(53, TabIndex - 53);
		OutFileSize = FCString::Atoi(*SizeString);
	}
}

#undef LOCTEXT_NAMESPACE