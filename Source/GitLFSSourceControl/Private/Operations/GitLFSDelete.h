// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "GitLFSSourceControlUtils.h"
#include "IGitLFSSourceControlWorker.h"

// Delete a file and remove it from revision control.
class FGitLFSDeleteWorker : public IGitLFSSourceControlWorker
{
	GENERATED_WORKER_BODY("Delete")

public:
	//~ Begin IGitLFSSourceControlWorker Interface
	virtual bool Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers) override;
	//~ End IGitLFSSourceControlWorker Interface
};