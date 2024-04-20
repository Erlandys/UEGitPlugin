#include "GitLFSSourceControlChangelistState.h"

#define LOCTEXT_NAMESPACE "GitSourceControl.ChangelistState"

FName FGitLFSSourceControlChangelistState::GetIconName() const
{
	// Mimic P4V colors, returning the red icon if there are active file(s), the blue if the changelist is empty or all the files are shelved.
	return FName("SourceControl.Changelist");
}

FName FGitLFSSourceControlChangelistState::GetSmallIconName() const
{
	return GetIconName();
}

FText FGitLFSSourceControlChangelistState::GetDisplayText() const
{
	return FText::FromString(Changelist.GetName());
}

FText FGitLFSSourceControlChangelistState::GetDescriptionText() const
{
	return FText::FromString(Description);
}

FText FGitLFSSourceControlChangelistState::GetDisplayTooltip() const
{
	return LOCTEXT("Tooltip", "Tooltip");
}

const FDateTime& FGitLFSSourceControlChangelistState::GetTimeStamp() const
{
	return TimeStamp;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
const TArray<FSourceControlStateRef> FGitLFSSourceControlChangelistState::GetFilesStates() const
#else
const TArray<FSourceControlStateRef>& FGitLFSSourceControlChangelistState::GetFilesStates() const
#endif
{
	return Files;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
int32 FGitLFSSourceControlChangelistState::GetFilesStatesNum() const
{
	return Files.Num();
}
#endif

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
const TArray<FSourceControlStateRef> FGitLFSSourceControlChangelistState::GetShelvedFilesStates() const
#else
const TArray<FSourceControlStateRef>& FGitLFSSourceControlChangelistState::GetShelvedFilesStates() const
#endif
{
	return ShelvedFiles;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
int32 FGitLFSSourceControlChangelistState::GetShelvedFilesStatesNum() const
{
	return ShelvedFiles.Num();
}
#endif

FSourceControlChangelistRef FGitLFSSourceControlChangelistState::GetChangelist() const
{
	FGitSourceControlChangelistRef ChangelistCopy = MakeShareable( new FGitLFSSourceControlChangelist(Changelist));
	return StaticCastSharedRef<ISourceControlChangelist>(ChangelistCopy);
}

#undef LOCTEXT_NAMESPACE