// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitLFSSourceControlChangelist.h"

FGitLFSSourceControlChangelist FGitLFSSourceControlChangelist::WorkingChangelist(TEXT("Working"), true);
FGitLFSSourceControlChangelist FGitLFSSourceControlChangelist::StagedChangelist(TEXT("Staged"), true);