// Copyright Project Borealis

#include "GitLFSSourceControlRunner.h"

#include "GitLFSSourceControlModule.h"
#include "GitLFSSourceControlProvider.h"
#include "GitLFSSourceControlOperations.h"

#include "Async/Async.h"

FGitLFSSourceControlRunner::FGitLFSSourceControlRunner()
{
	bRunThread = true;
	bRefreshSpawned = false;
	StopEvent = FPlatformProcess::GetSynchEventFromPool(true);
	Thread = FRunnableThread::Create(this, TEXT("GitSourceControlRunner"));
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
		if (!bRefreshSpawned)
		{
			// Flag that we're running the task already
			bRefreshSpawned = true;
			const auto ExecuteResult = Async(EAsyncExecution::TaskGraphMainThread, [this] {
				FGitLFSSourceControlModule* GitSourceControl = FGitLFSSourceControlModule::GetThreadSafe();
				// Module not loaded, bail. Usually happens when editor is shutting down, and this prevents a crash from bad timing.
				if (!GitSourceControl)
				{
					return ECommandResult::Failed;
				}
				FGitLFSSourceControlProvider& Provider = GitSourceControl->GetProvider();
				TSharedRef<FGitLFSFetch, ESPMode::ThreadSafe> RefreshOperation = ISourceControlOperation::Create<FGitLFSFetch>();
				RefreshOperation->bUpdateStatus = true;
#if ENGINE_MAJOR_VERSION >= 5
				const ECommandResult::Type Result = Provider.Execute(RefreshOperation, FSourceControlChangelistPtr(), FGitLFSSourceControlModule::GetEmptyStringArray(), EConcurrency::Asynchronous,
					FSourceControlOperationComplete::CreateRaw(this, &FGitLFSSourceControlRunner::OnSourceControlOperationComplete));
#else
				const ECommandResult::Type Result = Provider.Execute(RefreshOperation, FGitLFSSourceControlModule::GetEmptyStringArray(), EConcurrency::Asynchronous,
					FSourceControlOperationComplete::CreateRaw(this, &FGitLFSSourceControlRunner::OnSourceControlOperationComplete));
#endif
				return Result;
				});
			// Wait for result if not already completed
			if (bRefreshSpawned && bRunThread)
			{
				// Get the result
				ECommandResult::Type Result = ExecuteResult.Get();
				// If still not completed,
				if (bRefreshSpawned)
				{
					// mark failures as done, successes have to complete
					bRefreshSpawned = Result == ECommandResult::Succeeded;
				}
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

void FGitLFSSourceControlRunner::OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	// Mark task as done
	bRefreshSpawned = false;
}
