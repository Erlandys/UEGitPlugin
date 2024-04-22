// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "IGitLFSSourceControlWorker.h"

// Delete a file and remove it from revision control.
class FGitLFSRevertWorker : public IGitLFSSourceControlWorker
{
	GENERATED_WORKER_BODY("Revert")

public:
	//~ Begin IGitLFSSourceControlWorker Interface
	virtual bool Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers) override;
	//~ End IGitLFSSourceControlWorker Interface

private:
	static void GetMissingVsExistingFiles(
		const TArray<FString>& InFiles,
		TArray<FString>& OutMissingFiles,
		TArray<FString>& OutAllExistingFiles,
		TArray<FString>& OutOtherThanAddedExistingFiles);
};