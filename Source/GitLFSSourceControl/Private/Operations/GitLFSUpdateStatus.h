// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "IGitLFSSourceControlWorker.h"

// Get revision control status of files on local working copy.
class FGitLFSUpdateStatusWorker : public IGitLFSSourceControlWorker
{
	GENERATED_WORKER_BODY("UpdateStatus")

public:
	//~ Begin IGitLFSSourceControlWorker Interface
	virtual bool Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers) override;
	virtual bool UpdateStates() const override;
	//~ End IGitLFSSourceControlWorker Interface

private:
	TMap<FString, TArray<TSharedRef<FGitLFSSourceControlRevision>>> Histories;
};