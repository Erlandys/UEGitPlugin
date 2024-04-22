// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "GitLFSSourceControlSettings.h"
#include "GitLFSSourceControlProvider.h"
#include "Interfaces/IPluginManager.h"

class FExtender;
struct FAssetData;

class FGitLFSSourceControlModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface

	/** Access the Git revision control settings */
	FGitLFSSourceControlSettings& GetSettings()
	{
		return GitSourceControlSettings;
	}

	const FGitLFSSourceControlSettings& GetSettings() const
	{
		return GitSourceControlSettings;
	}

	/** Save the Git revision control settings */
	void SaveSettings() const;

	/** Access the Git revision control provider */
	TSharedPtr<FGitLFSSourceControlProvider> GetProvider()
	{
		return Provider;
	}

	const TSharedPtr<FGitLFSSourceControlProvider>& GetProvider() const
	{
		return Provider;
	}

public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	*/
	static FORCEINLINE FGitLFSSourceControlModule& Get()
	{
		return FModuleManager::Get().LoadModuleChecked<FGitLFSSourceControlModule>("GitLFSSourceControl");
	}
	static FORCEINLINE IModuleInterface* GetModule()
	{
		return FModuleManager::Get().GetModule("GitLFSSourceControl");
	}

	static FORCEINLINE FGitLFSSourceControlModule* GetThreadSafe()
	{
		IModuleInterface* ModulePtr = FModuleManager::Get().GetModule("GitLFSSourceControl");
		if (!ModulePtr)
		{
			// Main thread should never have this unloaded.
			check(!IsInGameThread());
			return nullptr;
		}

		return static_cast<FGitLFSSourceControlModule*>(ModulePtr);
	}

	static FORCEINLINE TSharedPtr<IPlugin> GetPlugin()
	{
		return IPluginManager::Get().FindPlugin("GitLFSSourceControl");
	}

	/** Set list of error messages that occurred after last git command */
	static void SetLastErrors(const TArray<FText>& InErrors)
	{
		if (FGitLFSSourceControlModule* Module = FModuleManager::GetModulePtr<FGitLFSSourceControlModule>("GitLFSSourceControl"))
		{
			if (const TSharedPtr<FGitLFSSourceControlProvider> Provider = Module->GetProvider())
			{
				Provider->SetLastErrors(InErrors);
			}
		}
	}

private:
	TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);
	void CreateGitContentBrowserAssetMenu(FMenuBuilder& MenuBuilder, const TArray<FAssetData> SelectedAssets);
	void DiffAssetAgainstGitOriginBranch(const TArray<FAssetData> SelectedAssets, FString BranchName) const;
	static void DiffAgainstOriginBranch(UObject* InObject, const FString& InPackagePath, const FString& InPackageName, const FString& BranchName);

	static TSharedPtr<ISourceControlRevision> GetOriginRevisionOnBranch(
		const FGitLFSCommandHelpers& Helpers,
		const FString& RelativeFileName,
		TArray<FString>& OutErrorMessages);

	/** The one and only Git revision control provider */
	TSharedPtr<FGitLFSSourceControlProvider> Provider;

	/** The settings for Git revision control */
	FGitLFSSourceControlSettings GitSourceControlSettings;

#if ENGINE_MAJOR_VERSION >= 5
	// ContentBrowserDelegate Handles
	FDelegateHandle OnFilterChangedHandle;
	FDelegateHandle OnSearchBoxChangedHandle;
	FDelegateHandle OnAssetSelectionChangedHandle;
	FDelegateHandle OnSourcesViewChangedHandle;
	FDelegateHandle OnAssetPathChangedHandle;
#endif
	FDelegateHandle OnExtendAssetSelectionMenuHandle;
};
