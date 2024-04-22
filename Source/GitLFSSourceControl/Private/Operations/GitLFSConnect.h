// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "IGitLFSSourceControlWorker.h"

// Called when first activated on a project, and then at project load time.
// Look for the root directory of the git repository (where the ".git/" subdirectory is located).
class FGitLFSConnectWorker : public IGitLFSSourceControlWorker
{
	GENERATED_WORKER_BODY("Connect")

public:
	//~ Begin IGitLFSSourceControlWorker Interface
	virtual bool Execute(FGitLFSSourceControlCommand& Command, FGitLFSCommandHelpers& Helpers) override;
	virtual bool UpdateStates() const override
	{
		return false;
	}
	//~ End IGitLFSSourceControlWorker Interface
};