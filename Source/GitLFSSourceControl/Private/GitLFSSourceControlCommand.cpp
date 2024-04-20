// Copyright (c) 2014-2023 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlCommand.h"

#include "Modules/ModuleManager.h"
#include "GitLFSSourceControlModule.h"
#include "GitLFSSourceControlUtils.h"

FGitLFSSourceControlCommand::FGitLFSSourceControlCommand(const TSharedRef<class ISourceControlOperation, ESPMode::ThreadSafe>& InOperation, const TSharedRef<class IGitLFSSourceControlWorker, ESPMode::ThreadSafe>& InWorker, const FSourceControlOperationComplete& InOperationCompleteDelegate)
	: Operation(InOperation)
	, Worker(InWorker)
	, OperationCompleteDelegate(InOperationCompleteDelegate)
	, bExecuteProcessed(0)
	, bCancelled(0)
	, bCommandSuccessful(false)
	, bAutoDelete(true)
	, Concurrency(EConcurrency::Synchronous)
{
	// cache the providers settings here
	const FGitLFSSourceControlModule& GitSourceControl = FGitLFSSourceControlModule::Get();
	const FGitLFSSourceControlProvider& Provider = GitSourceControl.GetProvider();
	PathToGitBinary = Provider.GetGitBinaryPath();
	bUsingGitLfsLocking = Provider.UsesCheckout();
	PathToRepositoryRoot = Provider.GetPathToRepositoryRoot();
	PathToGitRoot = Provider.GetPathToGitRoot();
}

void FGitLFSSourceControlCommand::UpdateRepositoryRootIfSubmodule(const TArray<FString>& AbsoluteFilePaths)
{
	PathToRepositoryRoot = GitLFSSourceControlUtils::ChangeRepositoryRootIfSubmodule(AbsoluteFilePaths, PathToRepositoryRoot);
}

bool FGitLFSSourceControlCommand::DoWork()
{
	bCommandSuccessful = Worker->Execute(*this);
	FPlatformAtomics::InterlockedExchange(&bExecuteProcessed, 1);

	return bCommandSuccessful;
}

void FGitLFSSourceControlCommand::Abandon()
{
	FPlatformAtomics::InterlockedExchange(&bExecuteProcessed, 1);
}

void FGitLFSSourceControlCommand::DoThreadedWork()
{
	Concurrency = EConcurrency::Asynchronous;
	DoWork();
}

void FGitLFSSourceControlCommand::Cancel()
{
	FPlatformAtomics::InterlockedExchange(&bCancelled, 1);
}

bool FGitLFSSourceControlCommand::IsCanceled() const
{
	return bCancelled != 0;
}

ECommandResult::Type FGitLFSSourceControlCommand::ReturnResults()
{
	// Save any messages that have accumulated
	for (const auto& String : ResultInfo.InfoMessages)
	{
		Operation->AddInfoMessge(FText::FromString(String));
	}
	for (const auto& String : ResultInfo.ErrorMessages)
	{
		Operation->AddErrorMessge(FText::FromString(String));
	}

	// run the completion delegate if we have one bound
	ECommandResult::Type Result = bCancelled ? ECommandResult::Cancelled : (bCommandSuccessful ? ECommandResult::Succeeded : ECommandResult::Failed);
	OperationCompleteDelegate.ExecuteIfBound(Operation, Result);

	return Result;
}
