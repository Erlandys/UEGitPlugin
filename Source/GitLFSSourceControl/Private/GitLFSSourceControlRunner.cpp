// Copyright Project Borealis

#include "GitLFSSourceControlRunner.h"

#include "GitLFSSourceControlModule.h"
#include "GitLFSSourceControlProvider.h"

#include "Async/Async.h"
#include "Operations/GitLFSFetch.h"

FGitLFSSourceControlRunner::FGitLFSSourceControlRunner()
{
	bRunThread = true;
	bRefreshSpawned = false;
	StopEvent = FPlatformProcess::GetSynchEventFromPool(true);
	Thread = FRunnableThread::Create(this, TEXT("GitSourceControlRunner"));
	OwnerReference = MakeShared<int32>();
}

FGitLFSSourceControlRunner::~FGitLFSSourceControlRunner()
{
	if (Thread)
	{
		Thread->Kill();
		delete StopEvent;
		delete Thread;
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool FGitLFSSourceControlRunner::Init()
{
	return true;
}

uint32 FGitLFSSourceControlRunner::Run()
{
	while (bRunThread)
	{
		StopEvent->Wait(30000);
		if (!bRunThread)
		{
			break;
		}

		// If we're not running the task already
		if (bRefreshSpawned)
		{
			continue;
		}

		// Flag that we're running the task already
		bRefreshSpawned = true;

		const auto ExecuteResult = Async(EAsyncExecution::TaskGraphMainThread, [this]
		{
			FGitLFSSourceControlModule* GitSourceControl = FGitLFSSourceControlModule::GetThreadSafe();

			// Module not loaded, bail. Usually happens when editor is shutting down, and this prevents a crash from bad timing.
			if (!GitSourceControl)
			{
				return ECommandResult::Failed;
			}

			const TSharedPtr<FGitLFSSourceControlProvider>& Provider = GitSourceControl->GetProvider();
			if (!ensure(Provider))
			{
				return ECommandResult::Failed;
			}

			const TSharedRef<FGitLFSFetchOperation> RefreshOperation = ISourceControlOperation::Create<FGitLFSFetchOperation>();
			RefreshOperation->bUpdateStatus = true;

			return Provider->ExecuteNoChangeList(
				RefreshOperation,
				{},
				EConcurrency::Asynchronous,
				FSourceControlOperationComplete::CreateRaw(this, &FGitLFSSourceControlRunner::OnSourceControlOperationComplete, TWeakPtr<int32>(OwnerReference)));
		});

		// Wait for result if not already completed
		if (bRefreshSpawned &&
			bRunThread)
		{
			// Get the result
			const ECommandResult::Type Result = ExecuteResult.Get();

			// If still not completed,
			if (bRefreshSpawned)
			{
				// mark failures as done, successes have to complete
				bRefreshSpawned = Result == ECommandResult::Succeeded;
			}
		}
	}

	return 0;
}

void FGitLFSSourceControlRunner::Stop()
{
	bRunThread = false;
	StopEvent->Trigger();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void FGitLFSSourceControlRunner::OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult, TWeakPtr<int32> WeakReference)
{
	UE_LOG(LogTemp, Warning, TEXT("1. Operation Complete"));
	// We want to be sure, that Runner is not destroyed yet
	if (!ensure(WeakReference.Pin()))
	{
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("2. Operation Complete"));

	// Mark task as done
	bRefreshSpawned = false;
}