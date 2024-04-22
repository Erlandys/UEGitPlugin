// Copyright Project Borealis

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "ISourceControlProvider.h"

class ISourceControlOperation;

class FGitLFSSourceControlRunner : public FRunnable
{
public:
	FGitLFSSourceControlRunner();
	virtual ~FGitLFSSourceControlRunner() override;

	//~ Begin FRunnable Interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	//~ End FRunnable Interface

	void OnSourceControlOperationComplete(const TSharedRef<ISourceControlOperation>& InOperation, ECommandResult::Type InResult, TWeakPtr<int32> WeakReference);

private:
	FRunnableThread* Thread;
	FEvent* StopEvent;
	bool bRunThread;
	bool bRefreshSpawned;
	TSharedPtr<int32> OwnerReference;
};
