// Copyright (c) 2014-2023 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlProvider.h"

#include "GitLFSCommandHelpers.h"
#include "GitLFSMessageLog.h"
#include "GitLFSSourceControlChangelistState.h"
#include "GitLFSSourceControlMenu.h"
#include "GitLFSSourceControlModule.h"
#include "GitLFSSourceControlRunner.h"

#include "ISourceControlModule.h"
#include "ScopedSourceControlProgress.h"
#include "SGitLFSSourceControlSettings.h"
#include "SourceControlHelpers.h"
#include "SourceControlOperations.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/ObjectSaveContext.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

void FGitLFSSourceControlProvider::Init(bool bForceConnection)
{
	if (!GitSourceControlMenu)
	{
		GitSourceControlMenu = MakeShared<FGitLFSSourceControlMenu>();
	}

	// Init() is called multiple times at startup: do not check git each time
	if (!bGitAvailable)
	{
		const TSharedPtr<IPlugin> Plugin = FGitLFSSourceControlModule::GetPlugin();
		if (Plugin.IsValid())
		{
			UE_LOG(LogSourceControl, Log, TEXT("Git plugin '%s'"), *(Plugin->GetDescriptor().VersionName));
		}

		CheckGitAvailability();
	}

	UPackage::PackageSavedWithContextEvent.AddSP(this, &FGitLFSSourceControlProvider::OnPackageSaved);

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().OnAssetRenamed().AddSP(this, &FGitLFSSourceControlProvider::OnAssetRenamed);

	// bForceConnection: not used anymore
}

void FGitLFSSourceControlProvider::Close()
{
	// clear the cache
	StateCache.Empty();

	// Remove all extensions to the "Revision Control" menu in the Editor Toolbar
	GitSourceControlMenu->Unregister();

	bGitAvailable = false;
	bGitRepositoryFound = false;
	UserName.Empty();
	UserEmail.Empty();

	if (Runner)
	{
		Runner = nullptr;
	}
}

FText FGitLFSSourceControlProvider::GetStatusText() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("IsAvailable"), (IsEnabled() && IsAvailable()) ? LOCTEXT("Yes", "Yes") : LOCTEXT("No", "No"));
	Args.Add(TEXT("RepositoryName"), FText::FromString(PathToRepositoryRoot));
	Args.Add(TEXT("RemoteUrl"), FText::FromString(RemoteUrl));
	Args.Add(TEXT("UserName"), FText::FromString(UserName));
	Args.Add(TEXT("UserEmail"), FText::FromString(UserEmail));
	Args.Add(TEXT("BranchName"), FText::FromString(BranchName));
	Args.Add(TEXT("CommitId"), FText::FromString(CommitId.Left(8)));
	Args.Add(TEXT("CommitSummary"), FText::FromString(CommitSummary));

	FText FormattedError;
	const TArray<FText>& RecentErrors = GetLastErrors();
	if (RecentErrors.Num() > 0)
	{
		FFormatNamedArguments ErrorArgs;
		ErrorArgs.Add(TEXT("ErrorText"), RecentErrors[0]);

		FormattedError = FText::Format(LOCTEXT("GitErrorStatusText", "Error: {ErrorText}\n\n"), ErrorArgs);
	}

	Args.Add(TEXT("ErrorText"), FormattedError);

	return FText::Format(NSLOCTEXT("GitStatusText", "{ErrorText}Enabled: {IsAvailable}", "Local repository: {RepositoryName}\nRemote: {RemoteUrl}\nUser: {UserName}\nE-mail: {UserEmail}\n[{BranchName} {CommitId}] {CommitSummary}"), Args);
}

bool FGitLFSSourceControlProvider::QueryStateBranchConfig(const FString& ConfigSrc, const FString& ConfigDest)
{
	// Check similar preconditions to Perforce (valid src and dest),
	if (ConfigSrc.Len() == 0 ||
		ConfigDest.Len() == 0)
	{
		return false;
	}

	if (!bGitAvailable ||
		!bGitRepositoryFound)
	{
		FTSMessageLog("SourceControl")
		.Error(LOCTEXT("StatusBranchConfigNoConnection", "Unable to retrieve status branch configuration from repo, no connection"));
		return false;
	}

	// Otherwise, we can assume that whatever our user is doing to config state branches is properly synced, so just copy.
	// TODO: maybe don't assume, and use git show instead?
	IFileManager::Get().Copy(*ConfigDest, *ConfigSrc);
	return true;
}

int32 FGitLFSSourceControlProvider::GetStateBranchIndex(const FString& StateBranchName) const
{
	// How do state branches indices work?
	// Order matters. Lower values are lower in the hierarchy, i.e., changes from higher branches get automatically merged down.
	// The higher branch is, the stabler it is, and has changes manually promoted up.

	// Check if we are checking the index of the current branch
	// UE uses FEngineVersion for the current branch name because of UEGames setup, but we want to handle otherwise for Git repos.
	TArray<FString> StatusBranchNames = GetStatusBranchNames();
	if (StateBranchName != FEngineVersion::Current().GetBranch())
	{
		// If we're not checking the current branch, then we don't need to do special handling.
		// If it is not a status branch, there is no message
		return StatusBranchNames.IndexOfByKey(StateBranchName);
	}

	const int32 CurrentBranchStatusIndex = StatusBranchNames.IndexOfByKey(BranchName);

	// If the user's current branch is tracked as a status branch, give the proper index
	if (CurrentBranchStatusIndex != INDEX_NONE)
	{
		return CurrentBranchStatusIndex;
	}

	// If the current branch is not a status branch, make it the highest branch
	// This is semantically correct, since if a branch is not marked as a status branch
	// it merges changes in a similar fashion to the highest status branch, i.e. manually promotes them
	// based on the user merging those changes in. and these changes always get merged from even the highest point
	// of the stream. i.e, promoted/stable changes are always up for consumption by this branch.
	return INT32_MAX;
}

ECommandResult::Type FGitLFSSourceControlProvider::GetState( const TArray<FString>& InFiles, TArray<TSharedRef<ISourceControlState>>& OutState, const EStateCacheUsage::Type InStateCacheUsage)
{
	if (!IsEnabled())
	{
		return ECommandResult::Failed;
	}

	if (InStateCacheUsage == EStateCacheUsage::ForceUpdate)
	{
		TArray<FString> ForceUpdate;
		for (const FString& Path : InFiles)
		{
			// Remove the path from the cache, so it's not ignored the next time we force check.
			// If the file isn't in the cache, force update it now.
			if (!RemoveFileFromIgnoreForceCache(Path))
			{
				ForceUpdate.Add(Path);
			}
		}

		if (ForceUpdate.Num() > 0)
		{
			Execute(ISourceControlOperation::Create<FUpdateStatus>(), ForceUpdate);
		}
	}

	const TArray<FString>& AbsoluteFiles = SourceControlHelpers::AbsoluteFilenames(InFiles);
	for (const FString& File : AbsoluteFiles)
	{
		OutState.Add(GetStateInternal(File));
	}

	return ECommandResult::Succeeded;
}

#if ENGINE_MAJOR_VERSION >= 5
ECommandResult::Type FGitLFSSourceControlProvider::GetState(const TArray<FSourceControlChangelistRef>& InChangelists, TArray<FSourceControlChangelistStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage)
{
	if (!IsEnabled())
	{
		return ECommandResult::Failed;
	}

	for (const FSourceControlChangelistRef& Changelist : InChangelists)
	{
		TSharedRef<FGitLFSSourceControlChangelist> GitChangelist = StaticCastSharedRef<FGitLFSSourceControlChangelist>(Changelist);
		OutState.Add(GetStateInternal(GitChangelist.Get()));
	}
	return ECommandResult::Succeeded;
}
#endif

TArray<FSourceControlStateRef> FGitLFSSourceControlProvider::GetCachedStateByPredicate(TFunctionRef<bool(const FSourceControlStateRef&)> Predicate) const
{
	TArray<FSourceControlStateRef> Result;
	for (const auto& It : StateCache)
	{
		const FSourceControlStateRef& State = It.Value;
		if (Predicate(State))
		{
			Result.Add(State);
		}
	}
	return Result;
}


ECommandResult::Type FGitLFSSourceControlProvider::Execute(
	const FSourceControlOperationRef& InOperation,
	GIT_UE_500_ONLY(FSourceControlChangelistPtr InChangelist,)
	const TArray<FString>& InFiles,
	const EConcurrency::Type InConcurrency,
	const FSourceControlOperationComplete& InOperationCompleteDelegate)
{
	// Only Connect operation allowed while not Enabled (Repository found)
	if (!IsEnabled() &&
		InOperation->GetName() != "Connect")
	{
		(void) InOperationCompleteDelegate.ExecuteIfBound(InOperation, ECommandResult::Failed);
		return ECommandResult::Failed;
	}

	const TArray<FString>& AbsoluteFiles = SourceControlHelpers::AbsoluteFilenames(InFiles);

	// Query to see if we allow this operation
	const TSharedPtr<IGitLFSSourceControlWorker> Worker = CreateWorker(InOperation->GetName());
	if (!Worker.IsValid())
	{
		// this operation is unsupported by this revision control provider
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("OperationName"), FText::FromName(InOperation->GetName()));
		Arguments.Add(TEXT("ProviderName"), FText::FromName(GetName()));

		const FText Message(FText::Format(LOCTEXT("UnsupportedOperation", "Operation '{OperationName}' not supported by revision control provider '{ProviderName}'"), Arguments));

		FTSMessageLog("SourceControl").Error(Message);
		InOperation->AddErrorMessge(Message);

		(void) InOperationCompleteDelegate.ExecuteIfBound(InOperation, ECommandResult::Failed);
		return ECommandResult::Failed;
	}

	FGitLFSCommandHelpers Helpers(*this);

	const TSharedPtr<FGitLFSSourceControlCommand> Command = MakeShared<FGitLFSSourceControlCommand>(InOperation, Worker.ToSharedRef());
	Command->Files = AbsoluteFiles;
	Command->Ignoredfiles = Helpers.RemoveIgnoredFiles(Command->Files);
	Command->UpdateRepositoryRootIfSubmodule(AbsoluteFiles);
	Command->OperationCompleteDelegate = InOperationCompleteDelegate;

#if GIT_ENGINE_VERSION >= 500
	const TSharedPtr<FGitLFSSourceControlChangelist> ChangelistPtr = StaticCastSharedPtr<FGitLFSSourceControlChangelist>(InChangelist);
	Command->Changelist = ChangelistPtr ? ChangelistPtr.ToSharedRef().Get() : FGitLFSSourceControlChangelist();
#endif
	
	// fire off operation
	if (InConcurrency == EConcurrency::Synchronous)
	{
#if UE_BUILD_DEBUG
		UE_LOG(LogSourceControl, Log, TEXT("ExecuteSynchronousCommand(%s)"), *InOperation->GetName().ToString());
#endif
		return ExecuteSynchronousCommand(Command, InOperation->GetInProgressString(), false);
	}
	else
	{
#if UE_BUILD_DEBUG
		UE_LOG(LogSourceControl, Log, TEXT("IssueAsynchronousCommand(%s)"), *InOperation->GetName().ToString());
#endif
		return IssueCommand(Command);
	}
}

bool FGitLFSSourceControlProvider::CanCancelOperation(const FSourceControlOperationRef& InOperation) const
{
	// TODO: maybe support cancellation again?
#if 0
	for (int32 CommandIndex = 0; CommandIndex < CommandQueue.Num(); ++CommandIndex)
	{
		const FGitLFSSourceControlCommand& Command = *CommandQueue[CommandIndex];
		if (Command.Operation == InOperation)
		{
			check(Command.bAutoDelete);
			return true;
		}
	}
#endif

	// operation was not in progress!
	return false;
}

void FGitLFSSourceControlProvider::CancelOperation(const FSourceControlOperationRef& InOperation)
{
	for (const TSharedPtr<FGitLFSSourceControlCommand> Command : CommandQueue)
	{
		if (Command->Operation == InOperation)
		{
			Command->Cancel();
			return;
		}
	}
}

void FGitLFSSourceControlProvider::Tick()
{
#if GIT_ENGINE_VERSION >= 500
	bool bStatesUpdated = TicksUntilNextForcedUpdate == 1;
	if (TicksUntilNextForcedUpdate > 0)
	{
		--TicksUntilNextForcedUpdate;
	}
#else
	bool bStatesUpdated = false;
#endif

	for (auto It = CommandQueue.CreateIterator(); It; ++It)
	{
		TSharedPtr<FGitLFSSourceControlCommand> Command = *It;
		if (!ensure(Command))
		{
			It.RemoveCurrent();
			continue;
		}

		if (Command->bExecuteProcessed)
		{
			// Remove command from the queue
			It.RemoveCurrent();

			if (!Command->IsCanceled())
			{
				// Update repository status on UpdateStatus operations
				UpdateRepositoryStatus(*Command);
			}

			// let command update the states of any files
			bStatesUpdated |= Command->Worker->UpdateStates();

			// dump any messages to output log
			OutputCommandMessages(*Command);

			// run the completion delegate callback if we have one bound
			if (!Command->IsCanceled())
			{
				Command->ReturnResults();
			}

			// only do one command per tick loop, as we dont want concurrent modification
			// of the command queue (which can happen in the completion delegate)
			break;
		}

		if (Command->bCancelled)
		{
			Command->ReturnResults();
			break;
		}
	}

	if (bStatesUpdated)
	{
		OnSourceControlStateChanged.Broadcast();
	}
}

#if ENGINE_MAJOR_VERSION >= 5
TArray<FSourceControlChangelistRef> FGitLFSSourceControlProvider::GetChangelists( EStateCacheUsage::Type InStateCacheUsage )
{
	if (!IsEnabled())
	{
		return {};
	}
	
	TArray<FSourceControlChangelistRef> Changelists;
	Algo::Transform(ChangelistsStateCache, Changelists, [](const auto& Pair)
	{
		return MakeShared<FGitLFSSourceControlChangelist, ESPMode::ThreadSafe>(Pair.Key);
	});

	return Changelists;
}
#endif

#if SOURCE_CONTROL_WITH_SLATE
TSharedRef<SWidget> FGitLFSSourceControlProvider::MakeSettingsWidget() const
{
	return SNew(SGitLFSSourceControlSettings);
}
#endif

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

ECommandResult::Type FGitLFSSourceControlProvider::ExecuteNoChangeList(
	const FSourceControlOperationRef& InOperation,
	const TArray<FString>& InFiles,
	EConcurrency::Type InConcurrency,
	const FSourceControlOperationComplete& InOperationCompleteDelegate)
{
	return Execute(
		InOperation,
		GIT_UE_500_ONLY(FSourceControlChangelistPtr(),)
		InFiles,
		InConcurrency,
		InOperationCompleteDelegate);
}

void FGitLFSSourceControlProvider::CheckGitAvailability()
{
	FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	PathToGitBinary = GitSourceControl.GetSettings().GetBinaryPath();
	if (PathToGitBinary.IsEmpty())
	{
		// Try to find Git binary, and update settings accordingly
		PathToGitBinary = FGitLFSSourceControlUtils::FindGitBinaryPath();
		if (!PathToGitBinary.IsEmpty())
		{
			GitSourceControl.GetSettings().SetBinaryPath(PathToGitBinary);
		}
	}

	bGitAvailable = !PathToGitBinary.IsEmpty();
	if (bGitAvailable)
	{
		UE_LOG(LogSourceControl, Log, TEXT("Using '%s'"), *PathToGitBinary);
		CheckRepositoryStatus();
	}
}

void FGitLFSSourceControlProvider::OnPackageSaved(const FString& Filename, UPackage*, FObjectPostSaveContext)
{
	FGitLFSSourceControlUtils::UpdateFileStagingOnSaved(Filename);
}

void FGitLFSSourceControlProvider::OnAssetRenamed(const FAssetData& AssetData, const FString& OldName) const
{
	const TSharedPtr<FGitLFSSourceControlProvider> Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider.IsValid()) ||
		!Provider->IsGitAvailable())
	{
		return;
	}

	Provider->GetStateInternal(OldName)->LocalFilename = AssetData.GetObjectPathString();
}

void FGitLFSSourceControlProvider::UpdateSettings()
{
	const FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	bUsingGitLfsLocking = GitSourceControl.GetSettings().IsUsingGitLfsLocking();
	LockUser = GitSourceControl.GetSettings().GetLfsUserName();
}

void FGitLFSSourceControlProvider::CheckRepositoryStatus()
{
	GitSourceControlMenu->Register();

	// Make sure our settings our up to date
	UpdateSettings();

	// Find the path to the root Git directory (if any, else uses the ProjectDir)
	const FString PathToProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	PathToRepositoryRoot = PathToProjectDir;
	if (!FGitLFSSourceControlUtils::FindRootDirectory(PathToProjectDir, PathToGitRoot))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Failed to find valid Git root directory."));
		bGitRepositoryFound = false;
		return;
	}
	PathToRepositoryRoot = PathToGitRoot;

	if (!FGitLFSSourceControlUtils::CheckGitAvailability(PathToGitBinary, &GitVersion))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Failed to find valid Git executable."));
		bGitRepositoryFound = false;
		return;
	}

	TUniqueFunction<void()> InitFunc = [this]()
	{
		if (!IsInGameThread())
		{
			// Wait until the module interface is valid
			IModuleInterface* Module;
			do
			{
				Module = FGitLFSSourceControlModule::GetModule();
				FPlatformProcess::Sleep(0.0f);
			} while (!Module);
		}

		{
			const FGitLFSCommandHelpers Helpers(PathToGitBinary, PathToRepositoryRoot);

			// Get user name & email (of the repository, else from the global Git config)
			UserName = Helpers.GetConfig(TEXT("user.name"));
			UserEmail = Helpers.GetConfig(TEXT("user.email"));
		}
		
		TMap<FString, FGitLFSSourceControlState> States;
		auto ConditionalRepoInit = [this, &States]()
		{
			const FGitLFSCommandHelpers Helpers(PathToGitBinary, PathToRepositoryRoot);
			if (!Helpers.GetBranchName(BranchName))
			{
				return false;
			}

			Helpers.GetRemoteBranchName(RemoteBranchName);
			Helpers.GetRemoteUrl(RemoteUrl);

			const TArray<FString> Files{TEXT("*.uasset"), TEXT("*.umap")};
			TArray<FString> LockableErrorMessages;
			if (!Helpers.CheckLFSLockable(Files, LockableErrorMessages))
			{
				for (const FString& ErrorMessage : LockableErrorMessages)
				{
					UE_LOG(LogSourceControl, Error, TEXT("%s"), *ErrorMessage);
				}
			}

			const TArray<FString> ProjectDirs
			{
				FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()),
				FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()),
				FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())
			};

			TArray<FString> StatusErrorMessages;
			return FGitLFSSourceControlUtils::RunUpdateStatus(*this, ProjectDirs, StatusErrorMessages, States);
		};

		TFunction<void()> Func;
		if (ConditionalRepoInit())
		{
			Func = [States, this]()
			{
				TMap<const FString, FGitLFSState> NewStates;
				if (FGitLFSSourceControlUtils::CollectNewStates(States, NewStates))
				{
					FGitLFSSourceControlUtils::UpdateCachedStates(NewStates);
				}

				Runner = MakeUnique<FGitLFSSourceControlRunner>();
				bGitRepositoryFound = true;
			};
		}
		else
		{
			Func = [States, this]()
			{
				UE_LOG(LogSourceControl, Error, TEXT("Failed to update repo on initialization."));
				bGitRepositoryFound = false;
			};
		}

		if (FApp::IsUnattended() ||
			IsRunningCommandlet())
		{
			Func();
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, MoveTemp(Func));
		}
	};

	if (FApp::IsUnattended() || IsRunningCommandlet())
	{
		InitFunc();
	}
	else
	{
		AsyncTask(ENamedThreads::AnyHiPriThreadNormalTask, MoveTemp(InitFunc));
	}
}

void FGitLFSSourceControlProvider::UpdateRepositoryStatus(const FGitLFSSourceControlCommand& InCommand)
{
	// For all operations running UpdateStatus, get Commit information:
	if (!InCommand.CommitId.IsEmpty())
	{
		CommitId = InCommand.CommitId;
		CommitSummary = InCommand.CommitSummary;
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TSharedRef<FGitLFSSourceControlState> FGitLFSSourceControlProvider::GetStateInternal(const FString& Filename)
{
	if (TSharedRef<FGitLFSSourceControlState>* State = StateCache.Find(Filename))
	{
		// found cached item
		return *State;
	}

	// cache an unknown state for this item
	TSharedRef<FGitLFSSourceControlState> NewState = MakeShared<FGitLFSSourceControlState>(Filename);
	StateCache.Add(Filename, NewState);
	return NewState;
}

TSharedRef<FGitLFSSourceControlChangelistState> FGitLFSSourceControlProvider::GetStateInternal(const FGitLFSSourceControlChangelist& InChangelist)
{
	if (TSharedRef<FGitLFSSourceControlChangelistState>* State = ChangelistsStateCache.Find(InChangelist))
	{
		// found cached item
		return *State;
	}

	// cache an unknown state for this item
	TSharedRef<FGitLFSSourceControlChangelistState> NewState = MakeShared<FGitLFSSourceControlChangelistState>(InChangelist);
	ChangelistsStateCache.Add(InChangelist, NewState);
	return NewState;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void FGitLFSSourceControlProvider::SetLastErrors(const TArray<FText>& InErrors)
{
	FScopeLock Lock(&LastErrorsCriticalSection);
	LastErrors = InErrors;
}

TArray<FText> FGitLFSSourceControlProvider::GetLastErrors() const
{
	FScopeLock Lock(&LastErrorsCriticalSection);
	TArray<FText> Result = LastErrors;
	return Result;
}

int32 FGitLFSSourceControlProvider::GetNumLastErrors() const
{
	FScopeLock Lock(&LastErrorsCriticalSection);
	return LastErrors.Num();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool FGitLFSSourceControlProvider::RemoveFileFromCache(const FString& Filename)
{
	return StateCache.Remove(Filename) > 0;
}

TArray<FString> FGitLFSSourceControlProvider::GetFilesInCache()
{
	TArray<FString> Files;
	for (const auto& It : StateCache)
	{
		Files.Add(It.Key);
	}
	return Files;
}

bool FGitLFSSourceControlProvider::AddFileToIgnoreForceCache(const FString& Filename)
{
	return IgnoreForceCache.Add(Filename) > 0;
}

bool FGitLFSSourceControlProvider::RemoveFileFromIgnoreForceCache(const FString& Filename)
{
	return IgnoreForceCache.Remove(Filename) > 0;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TArray<FString> FGitLFSSourceControlProvider::GetStatusBranchNames() const
{
	TArray<FString> StatusBranches;
	if (PathToGitBinary.IsEmpty() ||
		PathToRepositoryRoot.IsEmpty())
	{
		return StatusBranches;
	}

	const FGitLFSCommandHelpers Helpers(PathToGitBinary, PathToRepositoryRoot);

	for (const FString& Pattern : StatusBranchNamePatternsInternal)
	{
		TArray<FString> Matches;
		const bool bSuccess = Helpers.GetRemoteBranchesWildcard(Pattern, Matches);
		if (bSuccess &&
			Matches.Num() > 0)
		{
			for (const FString& Match : Matches)
			{
				StatusBranches.Add(Match.TrimStartAndEnd());	
			}
		}
	}
	
	return StatusBranches;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TSharedPtr<IGitLFSSourceControlWorker> FGitLFSSourceControlProvider::CreateWorker(const FName& OperationName) const
{
	if (const FGetGitSourceControlWorker* Operation = WorkersMap.Find(OperationName))
	{
		return Operation->Execute();
	}

	return nullptr;
}

ECommandResult::Type FGitLFSSourceControlProvider::ExecuteSynchronousCommand(const TSharedPtr<FGitLFSSourceControlCommand>& Command, const FText& Task, const bool bSuppressResponseMsg)
{
	ECommandResult::Type Result = ECommandResult::Failed;

	FText TaskText = Task;

	// Display the progress dialog
	if (bSuppressResponseMsg)
	{
		TaskText = FText::GetEmpty();
	}

	// Display the progress dialog if a string was provided
	{
		FScopedSourceControlProgress Progress(TaskText);

		// Issue the command asynchronously...
		IssueCommand(Command);

		int32 IterationIndex = 0;

		// ... then wait for its completion (thus making it synchronous)
		while (
			!Command->IsCanceled() &&
			CommandQueue.Contains(Command))
		{
			// Tick the command queue and update progress.
			Tick();

			if (IterationIndex >= 20)
			{
				Progress.Tick();
				IterationIndex = 0;
			}

			IterationIndex++;

			// Sleep for a bit so we don't busy-wait so much.
			FPlatformProcess::Sleep(0.01f);
		}

		if (Command->bCancelled)
		{
			Result = ECommandResult::Cancelled;
		}
		if (Command->bCommandSuccessful)
		{
			Result = ECommandResult::Succeeded;
		}
		else if (!bSuppressResponseMsg)
		{
			FMessageDialog::Open( EAppMsgType::Ok, LOCTEXT("Git_ServerUnresponsive", "Git command failed. Please check your connection and try again, or check the output log for more information.") );
			UE_LOG(LogSourceControl, Error, TEXT("Command '%s' Failed!"), *Command->Operation->GetName().ToString());
		}
	}

	return Result;
}

ECommandResult::Type FGitLFSSourceControlProvider::IssueCommand(const TSharedPtr<FGitLFSSourceControlCommand>& Command, const bool bSynchronous)
{
	if (!bSynchronous &&
		GThreadPool)
	{
		// Queue this to our worker thread(s) for resolving.
		// When asynchronous, any callback gets called from Tick().
		GThreadPool->AddQueuedWork(&*Command);
		CommandQueue.Add(Command);
		return ECommandResult::Succeeded;
	}

	UE_LOG(LogSourceControl, Log, TEXT("There are no threads available to process the revision control command '%s'. Running synchronously."), *Command->Operation->GetName().ToString());

	Command->bCommandSuccessful = Command->DoWork();

	(void) Command->Worker->UpdateStates();

	OutputCommandMessages(*Command);

	// Callback now if present. When asynchronous, this callback gets called from Tick().
	return Command->ReturnResults();
}

void FGitLFSSourceControlProvider::OutputCommandMessages(const FGitLFSSourceControlCommand& InCommand)
{
	FTSMessageLog SourceControlLog("SourceControl");

	for (int32 ErrorIndex = 0; ErrorIndex < InCommand.ResultInfo.ErrorMessages.Num(); ++ErrorIndex)
	{
		SourceControlLog.Error(FText::FromString(InCommand.ResultInfo.ErrorMessages[ErrorIndex]));
	}

	for (int32 InfoIndex = 0; InfoIndex < InCommand.ResultInfo.InfoMessages.Num(); ++InfoIndex)
	{
		SourceControlLog.Info(FText::FromString(InCommand.ResultInfo.InfoMessages[InfoIndex]));
	}
}

#undef LOCTEXT_NAMESPACE
