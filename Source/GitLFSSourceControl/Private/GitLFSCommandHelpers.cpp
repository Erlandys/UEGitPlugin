// Fill out your copyright notice in the Description page of Project Settings.

#include "GitLFSCommandHelpers.h"
#include "GitLFSCommand.h"
#include "ISourceControlModule.h"
#include "Data/GitLFSLockedFilesCache.h"
#include "Data/GitLFSScopedTempFile.h"

TArray<FString> FGitLFSCommandHelpers::LockableTypes = {};

FGitLFSCommandHelpers::FGitLFSCommandHelpers(const FString& PathToGit, const FString& RepositoryRoot)
	: PathToGit(PathToGit)
	, RepositoryRoot(RepositoryRoot) // TODO:
{
}

FGitLFSCommandHelpers::FGitLFSCommandHelpers(const FGitLFSSourceControlProvider& Provider)
	: PathToGit(Provider.GetGitBinaryPath())
	, RepositoryRoot(Provider.GetPathToRepositoryRoot())
	, GitRoot(Provider.GetPathToGitRoot())
{
}

FGitLFSCommandHelpers::FGitLFSCommandHelpers(const FGitLFSSourceControlCommand& Command)
	: PathToGit(Command.PathToGitBinary)
	, RepositoryRoot(Command.PathToRepositoryRoot)
	, GitRoot(Command.PathToGitRoot)
{
}

FString FGitLFSCommandHelpers::GetConfig(const FString& Config) const
{
	TArray<FString> Output;
	const bool bSuccess = RUN_GIT_COMMAND("config")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(Config)
		.Results(Output);
	
	if (!bSuccess ||
		Output.Num() == 0)
	{
		return "";
	}

	return Output[0];
}

bool FGitLFSCommandHelpers::GetBranchName(FString& OutBranchName) const
{
	const FGitLFSSourceControlModule* GitSourceControl = FGitLFSSourceControlModule::GetThreadSafe();
	if (!GitSourceControl)
	{
		return false;
	}

	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl->GetProvider();
	if (!ensure(Provider))
	{
		return false;
	}

	if (!Provider->GetBranchName().IsEmpty())
	{
		OutBranchName = Provider->GetBranchName();
		return true;
	}

	TArray<FString> Output;
	bool bSuccess = RUN_GIT_COMMAND("symbolic-ref")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(TEXT("--short --quiet HEAD"))
		.Results(Output);
	if (bSuccess &&
		Output.Num() > 0)
	{
		OutBranchName = Output[0];
		return true;
	}

	Output = {};
	TArray<FString> Errors;
	bSuccess = GetLog({ TEXT("-1 --format=\"%h\"") }, {}, Output, Errors);
	if (bSuccess &&
		Output.Num() > 0)
	{
		OutBranchName = "HEAD detached at ";
		OutBranchName += Output[0];
	}

	return false;
}

bool FGitLFSCommandHelpers::GetRemoteBranchName(FString& OutBranchName) const
{
	const FGitLFSSourceControlModule* GitSourceControl = FGitLFSSourceControlModule::GetThreadSafe();
	if (!GitSourceControl)
	{
		return false;
	}

	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl->GetProvider();
	if (!ensure(Provider))
	{
		return false;
	}

	if (!Provider->GetRemoteBranchName().IsEmpty())
	{
		OutBranchName = Provider->GetRemoteBranchName();
		return true;
	}

	TArray<FString> InfoMessages;

	const bool bResults =
		RUN_GIT_COMMAND("rev-parse")
			.PathToGit(PathToGit)
			.RepositoryRoot(RepositoryRoot)
			.Parameter(TEXT("--abbrev-ref --symbolic-full-name @{u}"))
			.Results(InfoMessages);

	if (bResults &&
		InfoMessages.Num() > 0)
	{
		OutBranchName = InfoMessages[0];
	}

	if (!bResults)
	{
		static bool bRunOnce = true;
		if (bRunOnce)
		{
			UE_LOG(LogSourceControl, Warning, TEXT("Upstream branch not found for the current branch, skipping current branch for remote check. Please push a remote branch."));
			bRunOnce = false;
		}
	}

	return bResults;
}

bool FGitLFSCommandHelpers::GetRemoteUrl(FString& OutRemoteUrl) const
{
	TArray<FString> Output;

	const bool bSuccess =
		RUN_GIT_COMMAND("remote")
			.PathToGit(PathToGit)
			.RepositoryRoot(RepositoryRoot)
			.Parameter(TEXT("get-url origin"))
			.Results(Output);

	if (bSuccess &&
		Output.Num() > 0)
	{
		OutRemoteUrl = Output[0];
		return true;
	}

	return false;
}

bool FGitLFSCommandHelpers::CheckLFSLockable(const TArray<FString>& Files, TArray<FString>& OutErrorMessages) const
{
	TArray<FString> Results;

	const bool bSuccess =
		RUN_GIT_COMMAND("check-attr")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(TEXT("lockable"))
		.Files(Files)
		.Results(Results)
		.Errors(OutErrorMessages);

	if (!bSuccess)
	{
		return false;
	}

	for (int32 Index = 0; Index < Files.Num(); Index++)
	{
		if (Results[Index].EndsWith("set"))
		{
			// Remove wildcard (*)
			const FString FileExt = Files[Index].RightChop(1);

			LockableTypes.Add(FileExt);
		}
	}

	return true;
}

bool FGitLFSCommandHelpers::GetRemoteBranchesWildcard(const FString& PatternMatch, TArray<FString>& OutBranchNames) const
{
	TArray<FString> InfoMessages;

	const bool bSuccess =
		RUN_GIT_COMMAND("branch")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(TEXT("--remotes --list"))
		.File(PatternMatch)
		.Results(InfoMessages);

	if (bSuccess &&
		InfoMessages.Num() > 0)
	{
		OutBranchNames = InfoMessages;
		return true;
	}

	static bool bRunOnce = true;
	if (bRunOnce)
	{
		UE_LOG(LogSourceControl, Warning, TEXT("No remote branches matching pattern \"%s\" were found."), *PatternMatch);
		bRunOnce = false;
	}

	return false;
}

bool FGitLFSCommandHelpers::FetchRemote(bool bUsingGitLfsLocking, TArray<FString>& OutResults, TArray<FString>& OutErrorMessages) const
{
	// Force refresh lock states
	if (bUsingGitLfsLocking)
	{
		TMap<FString, FString> Locks;
		FGitLFSLockedFilesCache::GetAllLocks(RepositoryRoot, PathToGit, OutErrorMessages, Locks, true);
	}

	const bool bSuccess =
		RUN_GIT_COMMAND("fetch")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(TEXT("--no-tags --prune"))
		.Results(OutResults)
		.Errors(OutErrorMessages);

	return bSuccess;
}

bool FGitLFSCommandHelpers::PullOrigin(const TArray<FString>& InFiles, TArray<FString>& OutFiles, TArray<FString>& OutResults, TArray<FString>& OutErrorMessages) const
{
	const TSharedPtr<FGitLFSSourceControlProvider> Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider))
	{
		return false;
	}

	if (Provider->bPendingRestart)
	{
		const FText PullFailMessage(NSLOCTEXT("GitSourceControl", "Git_NeedBinariesUpdate_Msg",
			"Refused to Git Pull because your editor binaries are out of date.\n\n"
			"Without a binaries update, new assets can become corrupted or cause crashes due to format differences.\n\n"
			"Please exit the editor, and update the project."));
		const FText PullFailTitle(NSLOCTEXT("GitSourceControl", "Git_NeedBinariesUpdate_Title", "Binaries Update Required"));
		FMessageDialog::Open(EAppMsgType::Ok, PullFailMessage, GIT_UE_503_SWITCH(&PullFailTitle, PullFailTitle));
		UE_LOG(LogSourceControl, Log, TEXT("Pull failed because we need a binaries update"));
		return false;
	}

	const TSet<FString> AlreadyReloaded{ InFiles };

	// Get remote branch
	FString RemoteBranch;
	if (!GetRemoteBranchName(RemoteBranch))
	{
		// No remote to sync from
		return false;
	}

	// Get the list of files which will be updated (either ones we changed locally, which will get potentially rebased or merged, or the remote ones that will update)
	TArray<FString> DifferentFiles;
	if (!RunDiff({ TEXT("--name-only"), RemoteBranch }, DifferentFiles, OutErrorMessages))
	{
		return false;
	}

	// Nothing to pull
	if (DifferentFiles.Num() == 0)
	{
		return true;
	}

	const TArray<FString>& AbsoluteDifferentFiles = FGitLFSSourceControlUtils::AbsoluteFilenames(DifferentFiles, RepositoryRoot);

	if (AlreadyReloaded.Num() > 0)
	{
		OutFiles.Reserve(AbsoluteDifferentFiles.Num() - AlreadyReloaded.Num());
		for (const FString& File : AbsoluteDifferentFiles)
		{
			if (!AlreadyReloaded.Contains(File))
			{
				OutFiles.Add(File);
			}
		}
	}
	else
	{
		OutFiles.Append(AbsoluteDifferentFiles);
	}

	TArray<FString> Files;
	for (const FString& File : OutFiles)
	{
		if (IsFileLFSLockable(File))
		{
			Files.Add(File);
		}
	}

	const bool bShouldReload = Files.Num() > 0;
	TArray<UPackage*> PackagesToReload;
	if (bShouldReload)
	{
		const auto PackagesToReloadResult = Async(EAsyncExecution::TaskGraphMainThread, [Files]
		{
			return UnlinkPackages(Files);
		});
		PackagesToReload = PackagesToReloadResult.Get();
	}

	// Reset HEAD and index to remote
	TArray<FString> InfoMessages;
	const bool bSuccess =
		RUN_GIT_COMMAND("pull")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(TEXT("--rebase --autostash"))
		.Results(InfoMessages)
		.Errors(OutErrorMessages);

	if (bShouldReload)
	{
		const auto ReloadPackagesResult = Async(EAsyncExecution::TaskGraphMainThread, [PackagesToReload]
		{
			TArray<UPackage*> Packages = PackagesToReload;
			FGitLFSSourceControlUtils::ReloadPackages(Packages);
		});
		ReloadPackagesResult.Wait();
	}

	return bSuccess;
}

bool FGitLFSCommandHelpers::GetLocks(const FString& Params, const FString& LockUser, TMap<FString, FString>& OutLocks, TArray<FString>& OutErrorMessages)
{
	// Our cache expired, or they asked us to expire cache. Query locks directly from the remote server.
	TArray<FString> Results;
	
	const bool bSuccess =
		RUN_LFS_COMMAND("locks")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(Params)
		.Results(Results)
		.Errors(OutErrorMessages);

	if (!bSuccess)
	{
		return false;
	}

	for (const FString& Result : Results)
	{
		FString FileName;
		FString User;
		ParseGitLockLine(RepositoryRoot, Result, false, FileName, User);
#if UE_BUILD_DEBUG && GIT_DEBUG_STATUS
		UE_LOG(LogSourceControl, Log, TEXT("LockedFile(%s, %s)"), *FileName, *User);
#endif

		if (LockUser.IsEmpty() ||
			LockUser == User)
		{
			OutLocks.Add(MoveTemp(FileName), MoveTemp(User));
		}
	}

	return true;
}

bool FGitLFSCommandHelpers::GetStatusNoLocks(
	const bool bAll,
	const TArray<FString>& Files,
	TArray<FString>& OutFiles,
	TArray<FString>& OutErrors) const
{
	return
		RUN_GIT_COMMAND("--no-optional-locks status")
			.PathToGit(PathToGit)
			.RepositoryRoot(RepositoryRoot)
			.Parameters({ TEXT("--porcelain"), bAll ? TEXT("-uall") : TEXT("") })
			.Files(Files)
			.Results(OutFiles)
			.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::GetLog(const TArray<FString>& Parameters, const TArray<FString>& Files, TArray<FString>& OutResult, TArray<FString>& OutErrors) const
{
	return
		RUN_GIT_COMMAND("log")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameters(Parameters)
		.Files(Files)
		.Results(OutResult)
		.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::GetCommitInfo(FString& OutCommitId, FString& OutCommitSummary) const
{
	TArray<FString> Output;
	TArray<FString> Errors;

	const bool bSuccess = GetLog({ TEXT("-1 --format=\"%H %s\"") }, {}, Output, Errors);
	if (!bSuccess ||
		Output.Num() == 0)
	{
		return false;
	}

	OutCommitId = Output[0].Left(40);
	OutCommitSummary = Output[0].RightChop(41);

	return true;
}

bool FGitLFSCommandHelpers::RunReset(const bool bHard, TArray<FString>& OutResult, TArray<FString>& OutErrors) const
{
	return
		RUN_GIT_COMMAND("reset")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(bHard ? TEXT("--hard") : TEXT(""))
		.Results(OutResult)
		.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::RunClean(
	const bool bForce,
	const bool bRemoveDirectories,
	TArray<FString>& OutResult,
	TArray<FString>& OutErrors) const
{
	TArray<FString> Parameters;
	if (bForce)
	{
		Parameters.Add(TEXT("-f"));
	}
	if (bRemoveDirectories)
	{
		Parameters.Add(TEXT("-d"));
	}

	return
		RUN_GIT_COMMAND("clean")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameters(Parameters)
		.Results(OutResult)
		.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::RunRemove(const TArray<FString>& Files, TArray<FString>& OutResult, TArray<FString>& OutErrors) const
{
	if (Files.Num() == 0)
	{
		return true;
	}

	return
		RUN_GIT_COMMAND("rm")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Files(Files)
		.Results(OutResult)
		.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::RunCheckout(const TArray<FString>& Files, TArray<FString>& OutResult, TArray<FString>& OutErrors) const
{
	if (Files.Num() == 0)
	{
		return true;
	}

	return
		RUN_GIT_COMMAND("checkout")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Files(Files)
		.Results(OutResult)
		.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::LockFiles(
	const TArray<FString>& Files,
	TArray<FString>& OutResult,
	TArray<FString>& OutErrors) const
{
	if (Files.Num() == 0)
	{
		return true;
	}

	return
		RUN_LFS_COMMAND("lock")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Files(Files)
		.Results(OutResult)
		.Errors(OutErrors);

}

bool FGitLFSCommandHelpers::UnlockFiles(
	const TArray<FString>& Files,
	const bool bAbsolutePaths,
	TArray<FString>& OutResult,
	TArray<FString>& OutErrors) const
{
	if (Files.Num() == 0)
	{
		return true;
	}

	TArray<FString> ConvertedPaths;
	if (bAbsolutePaths)
	{
		ConvertedPaths = FGitLFSSourceControlUtils::RelativeFilenames(Files, GitRoot);
	}
	else
	{
		ConvertedPaths = FGitLFSSourceControlUtils::AbsoluteFilenames(Files, GitRoot);
	}
	const TArray<FString>& AbsolutePaths = bAbsolutePaths ? Files : ConvertedPaths;
	const TArray<FString>& RelativePaths = bAbsolutePaths ? ConvertedPaths : Files;

	static bool bSuccess =
		RUN_LFS_COMMAND("unlock")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Files(RelativePaths)
		.Results(OutResult)
		.Errors(OutErrors);

	if (bSuccess)
	{ 
		for (const FString& File : AbsolutePaths)
		{
			FGitLFSLockedFilesCache::RemoveLockedFile(File);
		}
	}

	return bSuccess;
}

bool FGitLFSCommandHelpers::RunAdd(
	const bool bAll,
	const TArray<FString>& Files,
	TArray<FString>& OutResult,
	TArray<FString>& OutErrors) const
{
	if (Files.Num() == 0 &&
		!bAll)
	{
		return true;
	}

	return 
		RUN_GIT_COMMAND("add")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(bAll ? TEXT("-A") : TEXT(""))
		.Files(Files)
		.Results(OutResult)
		.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::RunCommit(
	const FGitLFSScopedTempFile& TempFile,
	const TArray<FString>& Files,
	TArray<FString>& OutResult,
	TArray<FString>& OutErrors) const
{
	return
		RUN_GIT_COMMAND("commit")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(TEXT("--file=\"") + FPaths::ConvertRelativePathToFull(TempFile.GetFilename()) + TEXT("\""))
		.Files(Files)
		.Results(OutResult)
		.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::RunPush(
	const TArray<FString>& Parameters,
	TArray<FString>& OutResult,
	TArray<FString>& OutErrors) const
{
	return
		RUN_GIT_COMMAND("push")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameters(Parameters)
		.Results(OutResult)
		.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::RunDiff(
	const TArray<FString>& Parameters,
	TArray<FString>& OutResult,
	TArray<FString>& OutErrors) const
{
	return
		RUN_GIT_COMMAND("diff")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameters(Parameters)
		.Results(OutResult)
		.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::RunLSTree(
	const TArray<FString>& Parameters,
	const FString& File,
	TArray<FString>& OutResult,
	TArray<FString>& OutErrors) const
{
	return
		RUN_GIT_COMMAND("ls-tree")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameters(Parameters)
		.File(File)
		.Results(OutResult)
		.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::RunRestore(
	bool bStaged,
	const TArray<FString>& Files,
	TArray<FString>& OutResult,
	TArray<FString>& OutErrors) const
{
	return
		RUN_GIT_COMMAND("restore")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(bStaged ? TEXT("--staged") : TEXT(""))
		.Files(Files)
		.Results(OutResult)
		.Errors(OutErrors);
}

bool FGitLFSCommandHelpers::RunLSRemote(const bool bPrintRemoteURL, const bool bOnlyBranches) const
{
	TArray<FString> Parameters;
	if (!bPrintRemoteURL)
	{
		Parameters.Add(TEXT("-q"));
	}
	if (bOnlyBranches)
	{
		Parameters.Add(TEXT("-h"));
	}

	return
		RUN_GIT_COMMAND("ls-remote")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameters(Parameters);
}

bool FGitLFSCommandHelpers::RunStash(const bool bSave) const
{
	return
		RUN_GIT_COMMAND("stash")
		.Parameter(bSave ? TEXT("save \"Stashed by Unreal Engine Git Plugin\"") : TEXT("pop"))
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot);
}

bool FGitLFSCommandHelpers::RunShow(TArray<FString>& OutInfoMessages, TArray<FString>& OutErrors) const
{
	return
		RUN_GIT_COMMAND("stash")
		.Parameter(TEXT("--date=raw --pretty=medium"))
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Results(OutInfoMessages)
		.Errors(OutErrors);
}

void FGitLFSCommandHelpers::RunInit()
{
	RUN_GIT_COMMAND("init")
	.PathToGit(PathToGit)
	.RepositoryRoot(RepositoryRoot);
}

void FGitLFSCommandHelpers::RunAddOrigin(const FString& URL)
{
	RUN_GIT_COMMAND("remote")
	.Parameter("add origin " + URL)
	.PathToGit(PathToGit)
	.RepositoryRoot(RepositoryRoot);
}

void FGitLFSCommandHelpers::RunLFSInstall()
{
	RUN_LFS_COMMAND("install")
	.PathToGit(PathToGit)
	.RepositoryRoot(RepositoryRoot);
}

bool FGitLFSCommandHelpers::RunGitVersion(FString& Output) const
{
	return
		RUN_GIT_COMMAND("version")
		.PathToGit(PathToGit)
		.RepositoryRoot("")
		.ResultString(Output);
}

bool FGitLFSCommandHelpers::RunLSFiles(const bool bUnmerged, const FString& File, TArray<FString>& Output) const
{
	return
		RUN_GIT_COMMAND("ls-files")
		.PathToGit(PathToGit)
		.RepositoryRoot(RepositoryRoot)
		.Parameter(bUnmerged ? TEXT("--unmerged") : TEXT(""))
		.File(File)
		.Results(Output);
}

TArray<FString> FGitLFSCommandHelpers::RemoveIgnoredFiles(TArray<FString>& Files)
{
	TArray<FString> IgnoredFiles;
	for (auto It = Files.CreateIterator(); It; ++It)
	{
		const bool bIsIgnored =
			RUN_GIT_COMMAND("check-ignore")
			.PathToGit(PathToGit)
			.RepositoryRoot(RepositoryRoot)
			.File(*It);

		if (bIsIgnored)
		{
			IgnoredFiles.Add(*It);
			It.RemoveCurrent();
		}
	}

	return IgnoredFiles;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool FGitLFSCommandHelpers::IsFileLFSLockable(const FString& InFile)
{
	for (const FString& Type : LockableTypes)
	{
		if (InFile.EndsWith(Type))
		{
			return true;
		}
	}
	return false;
}

void FGitLFSCommandHelpers::ParseGitLockLine(
	const FString& RepositoryRoot,
	const FString& Line,
	const bool bAbsolutePath,
	FString& OutFileName,
	FString& OutUser)
{
	TArray<FString> Informations;
	Line.ParseIntoArray(Informations, TEXT("\t"), true);

	if (Informations.Num() >= 2)
	{
		// Trim whitespace from the end of the filename
		Informations[0].TrimEndInline();
		// Trim whitespace from the end of the username
		Informations[1].TrimEndInline();

		if (!bAbsolutePath)
		{
			OutFileName = FPaths::ConvertRelativePathToFull(RepositoryRoot, Informations[0]);
		}
		else
		{
			OutFileName = Informations[0];
		}

		// Filename ID (or we expect it to be the username, but it's empty, or is the ID, we have to assume it's the current user)
		if (Informations.Num() == 2 ||
			Informations[1].IsEmpty() ||
			Informations[1].StartsWith(TEXT("ID:")))
		{
			// TODO: thread safety
			OutUser = FGitLFSSourceControlModule::Get().GetProvider()->GetLockUser();
		}
		// Filename Username ID
		else
		{
			OutUser = MoveTemp(Informations[1]);
		}
	}
}

TArray<UPackage*> FGitLFSCommandHelpers::UnlinkPackages(const TArray<FString>& PackageNames)
{
	// UE-COPY: ContentBrowserUtils::SyncPathsFromSourceControl()
	if (PackageNames.Num() == 0)
	{
		return {};
	}

	TArray<UPackage*> LoadedPackages;

	TArray<FString> PackagesToUnlink;
	for (const FString& FileName : PackageNames)
	{
		FString PackageName;
		if (FPackageName::TryConvertFilenameToLongPackageName(FileName, PackageName))
		{
			PackagesToUnlink.Add(*PackageName);
		}
	}

	// Form a list of loaded packages to reload...
	LoadedPackages.Reserve(PackagesToUnlink.Num());
	for (const FString& PackageName : PackagesToUnlink)
	{
		if (UPackage* Package = FindPackage(nullptr, *PackageName))
		{
			LoadedPackages.Add(Package);

			// Detach the linkers of any loaded packages so that SCC can overwrite the files...
			if (!Package->IsFullyLoaded())
			{
				FlushAsyncLoading();
				Package->FullyLoad();
			}

			ResetLoaders(Package);
		}
	}
	return LoadedPackages;
}
