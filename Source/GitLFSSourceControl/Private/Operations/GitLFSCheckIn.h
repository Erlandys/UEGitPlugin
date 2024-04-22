// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "IGitLFSSourceControlWorker.h"

// Commit (check-in) a set of files to the local depot.
class FGitLFSCheckInWorker : public IGitLFSSourceControlWorker
{
	GENERATED_WORKER_BODY("CheckIn")

public:
	//~ Begin IGitLFSSourceControlWorker Interface
	virtual bool Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers) override;
	//~ End IGitLFSSourceControlWorker Interface

private:
	static FText ParseCommitResults(const TArray<FString>& InResults);
};