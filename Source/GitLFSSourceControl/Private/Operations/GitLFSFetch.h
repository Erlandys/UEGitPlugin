// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "ISourceControlOperation.h"
#include "IGitLFSSourceControlWorker.h"

// Internal operation used to fetch from remote.
class FGitLFSFetchOperation : public ISourceControlOperation
{
public:
	//~ Begin ISourceControlOperation Interface
	virtual FName GetName() const override
	{
		return "Fetch";
	}

	virtual FText GetInProgressString() const override
	{
		return NSLOCTEXT("GitSourceControl", "SourceControl_Push", "Fetching from remote origin...");
	}
	//~ End ISourceControlOperation Interface

	bool bUpdateStatus = false;
};

// Copy or Move operation on a single file.
class FGitLFSFetchWorker : public IGitLFSSourceControlWorker
{
	GENERATED_WORKER_BODY("Fetch")

public:
	//~ Begin IGitLFSSourceControlWorker Interface
	virtual bool Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers) override;
	//~ End IGitLFSSourceControlWorker Interface

private:
	TMap<const FString, FGitLFSState> States;
};