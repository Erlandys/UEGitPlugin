// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "GitLFSSourceControlChangelist.h"
#include "ISourceControlProvider.h"
#include "IGitLFSSourceControlWorker.h"
#include "GitLFSSourceControlHelpers.h"

class FGitLFSSourceControlMenu;
class FGitLFSSourceControlState;
class FGitLFSSourceControlRunner;
class FGitLFSSourceControlChangelistState;

class FGitLFSSourceControlCommand;

DECLARE_DELEGATE_RetVal(TSharedRef<IGitLFSSourceControlWorker>, FGetGitSourceControlWorker)

/// Git version and capabilites extracted from the string "git version 2.11.0.windows.3"
struct FGitLFSVersion
{
	// Git version extracted from the string "git version 2.11.0.windows.3" (Windows), "git version 2.11.0" (Linux/Mac/Cygwin/WSL) or "git version 2.31.1.vfs.0.3" (Microsoft)
	int Major; // 2	Major version number
	int Minor; // 31	Minor version number
	int Patch; // 1	Patch/bugfix number
	bool bIsFork;
	FString Fork; // "vfs"
	int ForkMajor; // 0	Fork specific revision number
	int ForkMinor; // 3 
	int ForkPatch; // ?

	FGitLFSVersion()
		: Major(0)
		  , Minor(0)
		  , Patch(0)
		  , bIsFork(false)
		  , ForkMajor(0)
		  , ForkMinor(0)
		  , ForkPatch(0)
	{
	}
};

class FGitLFSSourceControlProvider
	: public ISourceControlProvider
	, public TSharedFromThis<FGitLFSSourceControlProvider>
{
public:
	//~ Begin ISourceControlProvider Interface
	virtual void Init(bool bForceConnection = true) override;
	virtual void Close() override;
	virtual FText GetStatusText() const override;
	virtual bool QueryStateBranchConfig(const FString& ConfigSrc, const FString& ConfigDest) override;
	virtual int32 GetStateBranchIndex(const FString& StateBranchName) const override;
	virtual ECommandResult::Type GetState(const TArray<FString>& InFiles, TArray<FSourceControlStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage) override;
#if GIT_ENGINE_VERSION >= 500
	virtual ECommandResult::Type GetState(const TArray<FSourceControlChangelistRef>& InChangelists, TArray<FSourceControlChangelistStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage) override;
#endif
	virtual TArray<FSourceControlStateRef> GetCachedStateByPredicate(TFunctionRef<bool(const FSourceControlStateRef&)> Predicate) const override;
	virtual ECommandResult::Type Execute(
		const FSourceControlOperationRef& InOperation,
		GIT_UE_500_ONLY(FSourceControlChangelistPtr InChangelist,)
		const TArray<FString>& InFiles,
		EConcurrency::Type InConcurrency = EConcurrency::Synchronous,
		const FSourceControlOperationComplete& InOperationCompleteDelegate = FSourceControlOperationComplete()) override;
	virtual bool CanCancelOperation(const FSourceControlOperationRef& InOperation) const override;
	virtual void CancelOperation(const FSourceControlOperationRef& InOperation) override;
	virtual void Tick() override;
#if GIT_ENGINE_VERSION >= 500
	virtual TArray<FSourceControlChangelistRef> GetChangelists(EStateCacheUsage::Type InStateCacheUsage) override;
#endif
#if SOURCE_CONTROL_WITH_SLATE
	virtual TSharedRef<class SWidget> MakeSettingsWidget() const override;
#endif

	virtual bool IsEnabled() const override
	{
		return bGitRepositoryFound;
	}
	virtual bool IsAvailable() const override
	{
		return bGitRepositoryFound;
	}
	virtual const FName& GetName() const override
	{
		static FName Name("Git LFS 2");
		return Name;
	}
	virtual void RegisterStateBranches(const TArray<FString>& BranchNames, const FString& ContentRootIn) override
	{
		StatusBranchNamePatternsInternal = BranchNames;
	}
	virtual FDelegateHandle RegisterSourceControlStateChanged_Handle(const FSourceControlStateChanged::FDelegate& SourceControlStateChanged) override
	{
		return OnSourceControlStateChanged.Add(SourceControlStateChanged);
	}
	virtual void UnregisterSourceControlStateChanged_Handle(FDelegateHandle Handle) override
	{
		OnSourceControlStateChanged.Remove( Handle );
	}
	virtual bool UsesLocalReadOnlyState() const override
	{
		// Git LFS Lock uses read-only state
		return bUsingGitLfsLocking;
	}
	virtual bool UsesChangelists() const override
	{
		return true;
	}
	virtual bool UsesCheckout() const override
	{
		// Git LFS Lock uses read-only state
		return bUsingGitLfsLocking;
	}
#if GIT_ENGINE_VERSION >= 501
	virtual bool UsesFileRevisions() const override
	{
		return true;
	}
	virtual TOptional<bool> IsAtLatestRevision() const override
	{
		return TOptional<bool>();
	}
	virtual TOptional<int> GetNumLocalChanges() const override
	{
		return TOptional<int>();
	}
#endif
#if GIT_ENGINE_VERSION >= 502
	virtual bool AllowsDiffAgainstDepot() const override
	{
		return true;
	}
	virtual bool UsesUncontrolledChangelists() const override
	{
		return true;
	}
	virtual bool UsesSnapshots() const override
	{
		return false;
	}
#endif
#if GIT_ENGINE_VERSION >= 503
	virtual bool CanExecuteOperation(const FSourceControlOperationRef& InOperation) const override
	{
		return WorkersMap.Find(InOperation->GetName()) != nullptr;
	}
	virtual TMap<EStatus, FString> GetStatus() const override
	{
		TMap<EStatus, FString> Result;
		Result.Add(EStatus::Enabled, IsEnabled() ? TEXT("Yes") : TEXT("No") );
		Result.Add(EStatus::Connected, (IsEnabled() && IsAvailable()) ? TEXT("Yes") : TEXT("No") );
		Result.Add(EStatus::User, UserName);
		Result.Add(EStatus::Repository, PathToRepositoryRoot);
		Result.Add(EStatus::Remote, RemoteUrl);
		Result.Add(EStatus::Branch, BranchName);
		Result.Add(EStatus::Email, UserEmail);
		return Result;
	}
#endif

	virtual TArray<TSharedRef<ISourceControlLabel>> GetLabels(const FString& InMatchingSpec) const override
	{
		TArray< TSharedRef<ISourceControlLabel> > Tags;

		// NOTE list labels. Called by CrashDebugHelper() (to remote debug Engine crash)
		//					 and by SourceControlHelpers::AnnotateFile() (to add source file to report)
		// Reserved for internal use by Epic Games with Perforce only
		return Tags;
	}
	//~ End ISourceControlProvider Interface

	using ISourceControlProvider::Execute;

public:
	ECommandResult::Type ExecuteNoChangeList(
		const FSourceControlOperationRef& InOperation,
		const TArray<FString>& InFiles,
		EConcurrency::Type InConcurrency = EConcurrency::Synchronous,
		const FSourceControlOperationComplete& InOperationCompleteDelegate = FSourceControlOperationComplete());

	/**
	 * Check configuration, else standard paths, and run a Git "version" command to check the availability of the binary.
	 */
	void CheckGitAvailability();

	void OnPackageSaved(const FString& Filename, UPackage*, FObjectPostSaveContext);
	void OnAssetRenamed(const FAssetData& AssetData, const FString& OldName) const;

	/** Refresh Git settings from revision control settings */
	void UpdateSettings();

public:
	/**
	 * Find the .git/ repository and check its status.
	 */
	void CheckRepositoryStatus();

	/**
	 * Update repository status on Connect and UpdateStatus operations
	 */
	void UpdateRepositoryStatus(const FGitLFSSourceControlCommand& InCommand);

public:
	/**
	 * Helper function used to update state cache
	 */
	TSharedRef<FGitLFSSourceControlState> GetStateInternal(const FString& Filename);

	/**
	 * Helper function used to update changelists state cache
	 */
	TSharedRef<FGitLFSSourceControlChangelistState> GetStateInternal(const FGitLFSSourceControlChangelist& InChangelist);

public:
	/**
	 * Set list of error messages that occurred after last perforce command
	 */
	void SetLastErrors(const TArray<FText>& InErrors);

	/**
	 * Get list of error messages that occurred after last perforce command
	 */
	TArray<FText> GetLastErrors() const;

	/**
	 * Get number of error messages seen after running last perforce command
	 */
	int32 GetNumLastErrors() const;

public:
	/**
	 * Remove a named file from the state cache
	 */
	bool RemoveFileFromCache(const FString& Filename);

	/**
	 * Get files in cache
	 */
	TArray<FString> GetFilesInCache();

	bool AddFileToIgnoreForceCache(const FString& Filename);

	bool RemoveFileFromIgnoreForceCache(const FString& Filename);

public:
	TArray<FString> GetStatusBranchNames() const;

public:
	/**
	 * Helper function for Execute()
	 */
	TSharedPtr<IGitLFSSourceControlWorker> CreateWorker(const FName& OperationName) const;

	/**
	 * Helper function for running command synchronously.
	 */
	ECommandResult::Type ExecuteSynchronousCommand(const TSharedPtr<FGitLFSSourceControlCommand>& Command, const FText& Task, bool bSuppressResponseMsg);

	/**
	 * Issue a command asynchronously if possible.
	 */
	ECommandResult::Type IssueCommand(const TSharedPtr<FGitLFSSourceControlCommand>& Command, const bool bSynchronous = false);

	/**
	 * Output any messages this command holds
	 */
	static void OutputCommandMessages(const FGitLFSSourceControlCommand& InCommand);

public:
	/**
	 * Is git binary found and working.
	 */
	FORCEINLINE bool IsGitAvailable() const
	{
		return bGitAvailable;
	}

	/**
	 * Git version for feature checking
	 */
	FORCEINLINE const FGitLFSVersion& GetGitVersion() const
	{
		return GitVersion;
	}

	/**
	 * Path to the root of the Unreal revision control repository: usually the ProjectDir
	 */
	FORCEINLINE const FString& GetPathToRepositoryRoot() const
	{
		return PathToRepositoryRoot;
	}

	/**
	 * Path to the root of the Git repository: can be the ProjectDir itself, or any parent directory (found by the "Connect" operation)
	 */
	FORCEINLINE const FString& GetPathToGitRoot() const
	{
		return PathToGitRoot;
	}

	/**
	 * Gets the path to the Git binary
	 */
	FORCEINLINE const FString& GetGitBinaryPath() const
	{
		return PathToGitBinary;
	}

	/**
	 * Git config user.name
	 */
	FORCEINLINE const FString& GetUserName() const
	{
		return UserName;
	}

	/**
	 * Git config user.email
	 */
	FORCEINLINE const FString& GetUserEmail() const
	{
		return UserEmail;
	}

	/**
	 * Git remote origin url
	 */
	FORCEINLINE const FString& GetRemoteUrl() const
	{
		return RemoteUrl;
	}

	FORCEINLINE const FString& GetLockUser() const
	{
		return LockUser;
	}

	FORCEINLINE const FString& GetBranchName() const
	{
		return BranchName;
	}

	FORCEINLINE const FString& GetRemoteBranchName() const
	{
		return RemoteBranchName;
	}

	FORCEINLINE bool UsesGitLFSLocking() const
	{
		return bUsingGitLfsLocking;
	}

	/**
	 * Register a worker with the provider.
	 * This is used internally so the provider can maintain a map of all available operations.
	 */
	template <typename T>
	void RegisterWorker()
	{
		WorkersMap.Add(T::GetStaticName(), FGetGitSourceControlWorker::CreateLambda([]() -> TSharedRef<IGitLFSSourceControlWorker>
		{
			return MakeShared<T>();
		}));
	}

	/** Indicates editor binaries are to be updated upon next sync */
	bool bPendingRestart = false;

#if GIT_ENGINE_VERSION >= 500
	uint32 TicksUntilNextForcedUpdate = 0;
#endif

private:
	/** Is git binary found and working. */
	bool bGitAvailable = false;

	/** Is git repository found. */
	bool bGitRepositoryFound = false;

	/** Is LFS locking enabled? */
	bool bUsingGitLfsLocking = false;

	FString PathToGitBinary;

	FString LockUser;

	/** Critical section for thread safety of error messages that occurred after last perforce command */
	mutable FCriticalSection LastErrorsCriticalSection;

	/** List of error messages that occurred after last perforce command */
	TArray<FText> LastErrors;

	/** Path to the root of the Unreal revision control repository: usually the ProjectDir */
	FString PathToRepositoryRoot;

	/** Path to the root of the Git repository: can be the ProjectDir itself, or any parent directory (found by the "Connect" operation) */
	FString PathToGitRoot;

	/** Git config user.name (from local repository, else globally) */
	FString UserName;

	/** Git config user.email (from local repository, else globally) */
	FString UserEmail;

	/** Name of the current branch */
	FString BranchName;

	/** Name of the current remote branch */
	FString RemoteBranchName;

	/** URL of the "origin" default remote server */
	FString RemoteUrl;

	/** Current Commit full SHA1 */
	FString CommitId;

	/** Current Commit description's Summary */
	FString CommitSummary;

	/** State cache */
	TMap<FString, TSharedRef<FGitLFSSourceControlState>> StateCache;
	TMap<FGitLFSSourceControlChangelist, TSharedRef<FGitLFSSourceControlChangelistState>> ChangelistsStateCache;

	/** The currently registered revision control operations */
	TMap<FName, FGetGitSourceControlWorker> WorkersMap;

	/** Queue for commands given by the main thread */
	TArray<TSharedPtr<FGitLFSSourceControlCommand>> CommandQueue;

	/** For notifying when the revision control states in the cache have changed */
	FSourceControlStateChanged OnSourceControlStateChanged;

	/** Git version for feature checking */
	FGitLFSVersion GitVersion;

	/** Revision Control Menu Extension */
	TSharedPtr<FGitLFSSourceControlMenu> GitSourceControlMenu;

	/**
	 * Ignore these files when forcing status updates. We add to this list when we've just updated the status already.
	 * UE's SourceControl has a habit of performing a double status update, immediately after an operation.
	*/
	TArray<FString> IgnoreForceCache;

	/** Array of branch name patterns for status queries */
	TArray<FString> StatusBranchNamePatternsInternal;

	TUniquePtr<FGitLFSSourceControlRunner> Runner = nullptr;
};
