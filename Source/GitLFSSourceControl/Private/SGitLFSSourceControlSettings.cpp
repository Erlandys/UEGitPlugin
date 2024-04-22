// Copyright (c) 2014-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "SGitLFSSourceControlSettings.h"

#include "GitLFSSourceControlModule.h"

#include "EditorDirectories.h"
#include "SourceControlOperations.h"
#include "Widgets/Input/SFilePathPicker.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#if GIT_ENGINE_VERSION < 501
#include "EditorStyleSet.h"
#endif

#define LOCTEXT_NAMESPACE "SGitLFSSourceControlSettings"

void SGitLFSSourceControlSettings::Construct(const FArguments& InArgs)
{
	InitialCommitMessage = LOCTEXT("InitialCommitMessage", "Initial commit");
	ReadmeContent = FText::FromString(FString(TEXT("# ")) + FApp::GetProjectName() + "\n\nDeveloped with Unreal Engine\n");

	ConstructBasedOnEngineVersion();
}

SGitLFSSourceControlSettings::~SGitLFSSourceControlSettings()
{
	RemoveInProgressNotification();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void SGitLFSSourceControlSettings::ConstructBasedOnEngineVersion( )
{
	const FText FileFilterType = NSLOCTEXT("GitSourceControl", "Executables", "Executables");
#if PLATFORM_WINDOWS
	const FString FileFilterText = FString::Printf(TEXT("%s (*.exe)|*.exe"), *FileFilterType.ToString());
#else
	const FString FileFilterText = FString::Printf(TEXT("%s"), *FileFilterType.ToString());
#endif

#if GIT_ENGINE_VERSION >= 500
#define ROW_LEFT(PADDING_HEIGHT) \
	+ SHorizontalBox::Slot() \
	.VAlign(VAlign_Center) \
	.HAlign(HAlign_Right) \
	.FillWidth(1.f) \
	.Padding(FMargin(0.f, 0.f, 16.f, PADDING_HEIGHT))

#define ROW_RIGHT(PADDING_HEIGHT) \
	+ SHorizontalBox::Slot() \
	.VAlign(VAlign_Center) \
	.FillWidth(2.f) \
	.Padding(FMargin(0.f, 0.f, 0.f, PADDING_HEIGHT))
#else
#define ROW_LEFT(PADDING_HEIGHT) \
	+ SHorizontalBox::Slot() \
	.FillWidth(1.f)

#define ROW_RIGHT(PADDING_HEIGHT) \
	+ SHorizontalBox::Slot() \
	.FillWidth(2.f)
#endif

#if GIT_ENGINE_VERSION >= 500
	const FSlateFontInfo Font = FAppStyle::GetFontStyle("NormalFont");
#else
	const FSlateFontInfo Font = FEditorStyle::GetFontStyle(TEXT("SourceControl.LoginWindow.Font"));
#endif

	auto VerticalBox =
		SNew(SVerticalBox)
		// Git Path
		+ SVerticalBox::Slot()
		.AutoHeight()
#if GIT_ENGINE_VERSION < 500
		.Padding(2.f)
		.VAlign(VAlign_Center)
#endif
		[
			SNew(SHorizontalBox)
			ROW_LEFT(10.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BinaryPathLabel", "Git Path"))
				.ToolTipText(LOCTEXT("BinaryPathLabel_Tooltip", "Path to Git binary"))
				.Font(Font)
			]
			ROW_RIGHT(10.f)
			[
				SNew(SFilePathPicker)
				.BrowseButtonImage(FEditorAppStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
				.BrowseButtonStyle(FEditorAppStyle::Get(), "HoverHintOnly")
				.BrowseDirectory(FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_OPEN))
				.BrowseTitle(LOCTEXT("BinaryPathBrowseTitle", "File picker..."))
				.FilePath(this, &SGitLFSSourceControlSettings::GetBinaryPathString)
				.FileTypeFilter(FileFilterText)
				.OnPathPicked(this, &SGitLFSSourceControlSettings::OnBinaryPathPicked)
			]
		]
		// Repository Root
		+ SVerticalBox::Slot()
#if GIT_ENGINE_VERSION < 500
		.AutoHeight()
		.Padding(2.0f)
		.VAlign(VAlign_Center)
#endif
		[
			SNew(SHorizontalBox)
			ROW_LEFT(10.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RepositoryRootLabel", "Root of the repository"))
				.ToolTipText(LOCTEXT("RepositoryRootLabel_Tooltip", "Path to the root of the Git repository"))
				.Font(Font)
			]
			ROW_RIGHT(10.f)
			[
				SNew(STextBlock)
				.Text(this, &SGitLFSSourceControlSettings::GetPathToRepositoryRoot)
				.ToolTipText(LOCTEXT("RepositoryRootLabel_Tooltip", "Path to the root of the Git repository"))
				.Font(Font)
			]
		]
		// User Name
		+ SVerticalBox::Slot()
#if GIT_ENGINE_VERSION < 500
		.AutoHeight()
		.Padding(2.0f)
		.VAlign(VAlign_Center)
#endif
		[
			SNew(SHorizontalBox)
			ROW_LEFT(10.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("UserNameLabel", "User Name"))
				.ToolTipText(LOCTEXT("UserNameLabel_Tooltip", "Git Username fetched from local config"))
				.Font(Font)
			]
			ROW_RIGHT(10.0f)
			[
				SNew(STextBlock)
				.Text(this, &SGitLFSSourceControlSettings::GetUserName)
				.ToolTipText(LOCTEXT("UserNameLabel_Tooltip", "Git Username fetched from local config"))
				.Font(Font)
			]
		]
		// Email
		+ SVerticalBox::Slot()
#if GIT_ENGINE_VERSION < 500
		.FillHeight(1.f)
		.Padding(2.f)
		.VAlign(VAlign_Center)
#endif
		[
			SNew(SHorizontalBox)
			ROW_LEFT(10.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("EmailLabel", "E-mail"))
				.ToolTipText(LOCTEXT("GitUserEmail_Tooltip", "Git E-mail fetched from local config"))
				.Font(Font)
			]
			ROW_RIGHT(10.f)
			[
				SNew(STextBlock)
				.Text(this, &SGitLFSSourceControlSettings::GetUserEmail)
				.ToolTipText(LOCTEXT("GitUserEmail_Tooltip", "Git E-mail fetched from local config"))
			]
		]
#if GIT_ENGINE_VERSION < 500
		// Separator
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.f)
		.VAlign(VAlign_Center)
		[
			SNew(SSeparator)
		]
		// Explanation text
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(2.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SGitLFSSourceControlSettings::MustInitializeGitRepository)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RepositoryNotFound", "Current Project is not contained in a Git Repository. Fill the form below to initialize a new Repository."))
				.ToolTipText(LOCTEXT("RepositoryNotFound_Tooltip", "No Repository found at the level or above the current Project"))
				.Font(Font)
			]
		]
		// Option to configure the URL of the default remote 'origin'
		// TODO: option to configure the name of the remote instead of the default origin
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SGitLFSSourceControlSettings::MustInitializeGitRepository)
			.ToolTipText(LOCTEXT("ConfigureOrigin_Tooltip", "Configure the URL of the default remote 'origin'"))
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ConfigureOrigin", "URL of the remote server 'origin'"))
				.Font(Font)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(2.f)
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.Text(this, &SGitLFSSourceControlSettings::GetRemoteUrl)
				.OnTextCommitted(this, &SGitLFSSourceControlSettings::OnRemoteUrlCommited)
				.Font(Font)
			]
		]
		// Option to add a proper .gitignore file (true by default)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SGitLFSSourceControlSettings::MustInitializeGitRepository)
			.ToolTipText(LOCTEXT("CreateGitIgnore_Tooltip", "Create and add a standard '.gitignore' file"))
			+ SHorizontalBox::Slot()
			.FillWidth(.1f)
			[
				SNew(SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				.OnCheckStateChanged(this, &SGitLFSSourceControlSettings::OnCheckedCreateGitIgnore)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(2.9f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CreateGitIgnore", "Add a .gitignore file"))
				.Font(Font)
			]
		]
		// Option to add a README.md file with custom content
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SGitLFSSourceControlSettings::MustInitializeGitRepository)
			.ToolTipText(LOCTEXT("CreateReadme_Tooltip", "Add a README.md file"))
			+ SHorizontalBox::Slot()
			.FillWidth(.1f)
			[
				SNew(SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				.OnCheckStateChanged(this, &SGitLFSSourceControlSettings::OnCheckedCreateReadme)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(.9f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CreateReadme", "Add a basic README.md file"))
				.Font(Font)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(2.f)
			.Padding(2.f)
			[
				SNew(SMultiLineEditableTextBox)
				.Text(this, &SGitLFSSourceControlSettings::GetReadmeContent)
				.OnTextCommitted(this, &SGitLFSSourceControlSettings::OnReadmeContentCommited)
				.IsEnabled(this, &SGitLFSSourceControlSettings::GetAutoCreateReadme)
				.SelectAllTextWhenFocused(true)
				.Font(Font)
			]
		]
		// Option to add a proper .gitattributes file for Git LFS (false by default)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SGitLFSSourceControlSettings::MustInitializeGitRepository)
			.ToolTipText(LOCTEXT("CreateGitAttributes_Tooltip", "Create and add a '.gitattributes' file to enable Git LFS for the whole 'Content/' directory (needs Git LFS extensions to be installed)."))
			+ SHorizontalBox::Slot()
			.FillWidth(.1f)
			[
				SNew(SCheckBox)
				.IsChecked(ECheckBoxState::Unchecked)
				.OnCheckStateChanged(this, &SGitLFSSourceControlSettings::OnCheckedCreateGitAttributes)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(2.9f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CreateGitAttributes", "Add a .gitattributes file to enable Git LFS"))
				.Font(Font)
			]
		]
#endif
		// LFS Config
		+ SVerticalBox::Slot()
		.AutoHeight()
#if GIT_ENGINE_VERSION < 500
		.Padding(2.0f)
		.VAlign(VAlign_Center)
#endif
		[
			SNew(SHorizontalBox)
#if GIT_ENGINE_VERSION >= 500
			ROW_LEFT(10.f)
			[
				SNew(SCheckBox)
				.IsChecked(IsUsingGitLfsLocking())
				.OnCheckStateChanged(this, &SGitLFSSourceControlSettings::OnCheckedUseGitLfsLocking)
				.IsEnabled(this, &SGitLFSSourceControlSettings::CanUseGitLfsLocking)
				.Content()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UseGitLfsLocking", "Uses Git LFS"))
					.ToolTipText(LOCTEXT("UseGitLfsLocking_Tooltip", "Uses Git LFS 2 File Locking workflow (CheckOut and Commit/Push)."))
				]
			]
#else
			.ToolTipText(LOCTEXT("UseGitLfsLocking_Tooltip", "Uses Git LFS 2 File Locking workflow (CheckOut and Commit/Push)."))
			+ SHorizontalBox::Slot()
			.FillWidth(0.1f)
			[
				SNew(SCheckBox)
				.IsChecked(IsUsingGitLfsLocking())
				.OnCheckStateChanged(this, &SGitLFSSourceControlSettings::OnCheckedUseGitLfsLocking)
				.IsEnabled(this, &SGitLFSSourceControlSettings::CanUseGitLfsLocking)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.9f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("UseGitLfsLocking", "Uses Git LFS 2 File Locking workflow"))
				.Font(Font)
			]
#endif
			ROW_RIGHT(10.f)
			[
				SNew(SEditableTextBox)
				.Text(this, &SGitLFSSourceControlSettings::GetLfsUserName)
				.OnTextCommitted(this, &SGitLFSSourceControlSettings::OnLfsUserNameCommited)
				.IsEnabled(this, &SGitLFSSourceControlSettings::GetIsUsingGitLfsLocking)
				.HintText(LOCTEXT("LfsUserName_Hint", "Username to lock files on the LFS server"))
			]
		]
		// [Optional] Initial Git Commit
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.ToolTipText(LOCTEXT("InitialGitCommit_Tooltip", "Make the initial Git commit"))
			.Visibility(this, &SGitLFSSourceControlSettings::MustInitializeGitRepository)
			+ SHorizontalBox::Slot()
			.FillWidth(.1f)
			[
				SNew(SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				.OnCheckStateChanged(this, &SGitLFSSourceControlSettings::OnCheckedInitialCommit)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(.9f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InitialGitCommit", "Make the initial Git commit"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(2.f)
			.Padding(2.f)
			[
				SNew(SMultiLineEditableTextBox)
				.Text(this, &SGitLFSSourceControlSettings::GetInitialCommitMessage)
				.OnTextCommitted(this, &SGitLFSSourceControlSettings::OnInitialCommitMessageCommited)
				.IsEnabled(this, &SGitLFSSourceControlSettings::GetAutoInitialCommit)
				.SelectAllTextWhenFocused(true)
			]
		]
		// [Optional] Initialize Project with Git
		+ SVerticalBox::Slot()
		.FillHeight(2.5f)
		.Padding(4.f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SGitLFSSourceControlSettings::MustInitializeGitRepository)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("GitInitRepository", "Initialize project with Git"))
				.ToolTipText(LOCTEXT("GitInitRepository_Tooltip", "Initialize current project as a new Git repository"))
				.OnClicked(this, &SGitLFSSourceControlSettings::OnClickedInitializeGitRepository)
				.IsEnabled(this, &SGitLFSSourceControlSettings::CanInitializeGitRepository)
				.HAlign(HAlign_Center)
				.ContentPadding(6.f)
			]
		];

#if GIT_ENGINE_VERSION >= 500
	ChildSlot
	[
		VerticalBox
	];
#else
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryBottom"))
		.Padding(FMargin(0.f, 3.f, 0.f, 0.f))
		[
			VerticalBox
		]
	];
#endif

	// TODO [RW] The UE5 GUI for the two optional initial git support functionalities has not been tested
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FString SGitLFSSourceControlSettings::GetBinaryPathString() const
{
	return FGitLFSSourceControlModule::Get().GetSettings().GetBinaryPath();
}

void SGitLFSSourceControlSettings::OnBinaryPathPicked( const FString& PickedPath ) const
{
	FGitLFSSourceControlModule& Module = FGitLFSSourceControlModule::Get();
	if (!Module.GetSettings().SetBinaryPath(FPaths::ConvertRelativePathToFull(PickedPath)))
	{
		return;
	}

	const TSharedPtr<FGitLFSSourceControlProvider> Provider = Module.GetProvider();
	if (!Provider)
	{
		return;
	}

	// Re-Check provided git binary path for each change
	Provider->CheckGitAvailability();
	if (Provider->IsGitAvailable())
	{
		Module.SaveSettings();
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FText SGitLFSSourceControlSettings::GetPathToRepositoryRoot() const
{
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider))
	{
		return {};
	}

	return FText::FromString(Provider->GetPathToRepositoryRoot());
}

FText SGitLFSSourceControlSettings::GetUserName() const
{
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider))
	{
		return {};
	}

	return FText::FromString(Provider->GetUserName());
}

FText SGitLFSSourceControlSettings::GetUserEmail() const
{
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider))
	{
		return {};
	}

	return FText::FromString(Provider->GetUserEmail());
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

EVisibility SGitLFSSourceControlSettings::MustInitializeGitRepository() const
{
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider))
	{
		return EVisibility::Collapsed;
	}

	const bool bGitAvailable = Provider->IsGitAvailable();
	const bool bGitRepositoryFound = Provider->IsEnabled();
#if 0
	return bGitAvailable && !bGitRepositoryFound ? EVisibility::Visible : EVisibility::Collapsed;
#else
	return EVisibility::Collapsed;
#endif
}

bool SGitLFSSourceControlSettings::CanInitializeGitRepository() const
{
	FGitLFSSourceControlModule& Module = FGitLFSSourceControlModule::Get();
	const TSharedPtr<FGitLFSSourceControlProvider>& Provider = Module.GetProvider();
	if (!ensure(Provider))
	{
		return false;
	}

	const bool bGitAvailable = Provider->IsGitAvailable();
	const bool bGitRepositoryFound = Provider->IsEnabled();
	const FString& LfsUserName = Module.GetSettings().GetLfsUserName();
	const bool bIsUsingGitLfsLocking = Provider->UsesCheckout();
	const bool bGitLfsConfigOk = !bIsUsingGitLfsLocking || !LfsUserName.IsEmpty();
	const bool bInitialCommitConfigOk = !bAutoInitialCommit || !InitialCommitMessage.IsEmpty();
#if 0
	return (bGitAvailable && !bGitRepositoryFound && bGitLfsConfigOk && bInitialCommitConfigOk);
#else
	return false;
#endif
}

bool SGitLFSSourceControlSettings::CanUseGitLfsLocking() const
{
	// TODO LFS SRombauts : check if .gitattributes file is present and if Content/ is already tracked!
	const bool bGitAttributesCreated = true;
	return (bAutoCreateGitAttributes || bGitAttributesCreated);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FReply SGitLFSSourceControlSettings::OnClickedInitializeGitRepository()
{
	FGitLFSSourceControlModule& Module = FGitLFSSourceControlModule::Get();
	const TSharedPtr<FGitLFSSourceControlProvider> Provider = Module.GetProvider();
	if (!ensure(Provider))
	{
		return FReply::Handled();
	}

	const FString& PathToGitBinary = Module.GetSettings().GetBinaryPath();
	const FString PathToProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	FGitLFSCommandHelpers Helpers(PathToGitBinary, PathToProjectDir);

	// 1.a. Synchronous (very quick) "git init" operation: initialize a Git local repository with a .git/ subdirectory
	Helpers.RunInit();
	// 1.b. Synchronous (very quick) "git remote add" operation: configure the URL of the default remote server 'origin' if specified
	if (!RemoteUrl.IsEmpty())
	{
		Helpers.RunAddOrigin(RemoteUrl.ToString());
	}

	// Check the new repository status to enable connection (branch, user e-mail)
	Provider->CheckGitAvailability();

	if (Provider->IsAvailable())
	{
		// List of files to add to Revision Control (.uproject, Config/, Content/, Source/ files and .gitignore/.gitattributes if any)
		TArray<FString> ProjectFiles;
		ProjectFiles.Add(FPaths::ProjectContentDir());
		ProjectFiles.Add(FPaths::ProjectConfigDir());
		ProjectFiles.Add(FPaths::GetProjectFilePath());
		if (FPaths::DirectoryExists(FPaths::GameSourceDir()))
		{
			ProjectFiles.Add(FPaths::GameSourceDir());
		}

		if (bAutoCreateGitIgnore)
		{
			// 2.a. Create a standard ".gitignore" file with common patterns for a typical Blueprint & C++ project
			const FString GitIgnoreFilename = FPaths::Combine(FPaths::ProjectDir(), TEXT(".gitignore"));
			const FString GitIgnoreContent = TEXT("Binaries\nDerivedDataCache\nIntermediate\nSaved\n.vscode\n.vs\n*.VC.db\n*.opensdf\n*.opendb\n*.sdf\n*.sln\n*.suo\n*.xcodeproj\n*.xcworkspace\n*.log");
			if (FFileHelper::SaveStringToFile(GitIgnoreContent, *GitIgnoreFilename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				ProjectFiles.Add(GitIgnoreFilename);
			}
		}

		if (bAutoCreateReadme)
		{
			// 2.b. Create a "README.md" file with a custom description
			const FString ReadmeFilename = FPaths::Combine(FPaths::ProjectDir(), TEXT("README.md"));
			if (FFileHelper::SaveStringToFile(ReadmeContent.ToString(), *ReadmeFilename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				ProjectFiles.Add(ReadmeFilename);
			}
		}

		if (bAutoCreateGitAttributes)
		{
			// 2.c. Synchronous (very quick) "lfs install" operation: needs only to be run once by user
			Helpers.RunLFSInstall();

			// 2.d. Create a ".gitattributes" file to enable Git LFS (Large File System) for the whole "Content/" subdir
			const FString GitAttributesFilename = FPaths::Combine(FPaths::ProjectDir(), TEXT(".gitattributes"));
			FString GitAttributesContent;
			if (Provider->UsesCheckout())
			{
				// Git LFS 2.x File Locking mechanism
				GitAttributesContent = TEXT("Content/** filter=lfs diff=lfs merge=lfs -text lockable\n");
			}
			else
			{
				GitAttributesContent = TEXT("Content/** filter=lfs diff=lfs merge=lfs -text\n");
			}

			if (FFileHelper::SaveStringToFile(GitAttributesContent, *GitAttributesFilename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				ProjectFiles.Add(GitAttributesFilename);
			}
		}

		// 3. Add files to Revision Control: launch an asynchronous MarkForAdd operation
		LaunchMarkForAddOperation(ProjectFiles);

		// 4. The CheckIn will follow, at completion of the MarkForAdd operation
		Provider->CheckRepositoryStatus();
	}
	return FReply::Handled();
}

void SGitLFSSourceControlSettings::OnCheckedCreateGitIgnore(const ECheckBoxState NewCheckedState)
{
	bAutoCreateGitIgnore = NewCheckedState == ECheckBoxState::Checked;
}

void SGitLFSSourceControlSettings::OnCheckedCreateGitAttributes(const ECheckBoxState NewCheckedState)
{
	bAutoCreateGitAttributes = NewCheckedState == ECheckBoxState::Checked;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void SGitLFSSourceControlSettings::OnCheckedCreateReadme(const ECheckBoxState NewCheckedState)
{
	bAutoCreateReadme = NewCheckedState == ECheckBoxState::Checked;
}

bool SGitLFSSourceControlSettings::GetAutoCreateReadme() const
{
	return bAutoCreateReadme;
}

void SGitLFSSourceControlSettings::OnReadmeContentCommited(const FText& InText, ETextCommit::Type)
{
	ReadmeContent = InText;
}

FText SGitLFSSourceControlSettings::GetReadmeContent() const
{
	return ReadmeContent;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void SGitLFSSourceControlSettings::OnCheckedUseGitLfsLocking(const ECheckBoxState NewCheckedState)
{
	FGitLFSSourceControlModule& Module = FGitLFSSourceControlModule::Get();
	Module.GetSettings().SetUsingGitLfsLocking(NewCheckedState == ECheckBoxState::Checked);
	Module.GetSettings().SaveSettings();

	if (const TSharedPtr<FGitLFSSourceControlProvider> Provider = Module.GetProvider())
	{
		Provider->UpdateSettings();
	}
}

ECheckBoxState SGitLFSSourceControlSettings::IsUsingGitLfsLocking() const
{
	return GetIsUsingGitLfsLocking() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool SGitLFSSourceControlSettings::GetIsUsingGitLfsLocking() const
{
	return FGitLFSSourceControlModule::Get().GetSettings().IsUsingGitLfsLocking();
}

void SGitLFSSourceControlSettings::OnLfsUserNameCommited(const FText& InText, ETextCommit::Type)
{
	FGitLFSSourceControlModule& Module = FGitLFSSourceControlModule::Get();
	Module.GetSettings().SetLfsUserName(InText.ToString());
	Module.GetSettings().SaveSettings();

	if (const TSharedPtr<FGitLFSSourceControlProvider> Provider = Module.GetProvider())
	{
		Provider->UpdateSettings();
	}
}

FText SGitLFSSourceControlSettings::GetLfsUserName() const
{
	FGitLFSSourceControlModule& Module = FGitLFSSourceControlModule::Get();
	const FString LFSUserName = Module.GetSettings().GetLfsUserName();
	if (LFSUserName.IsEmpty())
	{
		const FText& UserName = GetUserName();
		Module.GetSettings().SetLfsUserName(UserName.ToString());
		Module.GetSettings().SaveSettings();

		if (const TSharedPtr<FGitLFSSourceControlProvider> Provider = Module.GetProvider())
		{
			Provider->UpdateSettings();
		}
		return UserName;
	}

	return FText::FromString(LFSUserName);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void SGitLFSSourceControlSettings::OnCheckedInitialCommit(const ECheckBoxState NewCheckedState)
{
	bAutoInitialCommit = NewCheckedState == ECheckBoxState::Checked;
}

bool SGitLFSSourceControlSettings::GetAutoInitialCommit() const
{
	return bAutoInitialCommit;
}

void SGitLFSSourceControlSettings::OnInitialCommitMessageCommited(const FText& InText, ETextCommit::Type InCommitType)
{
	InitialCommitMessage = InText;
}

FText SGitLFSSourceControlSettings::GetInitialCommitMessage() const
{
	return InitialCommitMessage;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void SGitLFSSourceControlSettings::OnRemoteUrlCommited(const FText& InText, ETextCommit::Type)
{
	RemoteUrl = InText;
}

FText SGitLFSSourceControlSettings::GetRemoteUrl() const
{
	return RemoteUrl;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void SGitLFSSourceControlSettings::LaunchMarkForAddOperation(const TArray<FString>& InFiles)
{
	const TSharedPtr<FGitLFSSourceControlProvider> Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider))
	{
		return;
	}

	const TSharedRef<FMarkForAdd> MarkForAddOperation = ISourceControlOperation::Create<FMarkForAdd>();
	const ECommandResult::Type Result = Provider->ExecuteNoChangeList(
		MarkForAddOperation,
		InFiles,
		EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateSP(this, &SGitLFSSourceControlSettings::OnSourceControlOperationComplete));

	if (Result == ECommandResult::Succeeded)
	{
		DisplayInProgressNotification(MarkForAddOperation);
	}
	else
	{
		DisplayFailureNotification(MarkForAddOperation);
	}
}

void SGitLFSSourceControlSettings::LaunchCheckInOperation()
{
	const TSharedPtr<FGitLFSSourceControlProvider> Provider = FGitLFSSourceControlModule::Get().GetProvider();
	if (!ensure(Provider))
	{
		return;
	}

	const TSharedRef<FCheckIn> CheckInOperation = ISourceControlOperation::Create<FCheckIn>();
	CheckInOperation->SetDescription(InitialCommitMessage);

	const ECommandResult::Type Result = Provider->ExecuteNoChangeList(
		CheckInOperation,
		{},
		EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateSP(this, &SGitLFSSourceControlSettings::OnSourceControlOperationComplete));

	if (Result == ECommandResult::Succeeded)
	{
		DisplayInProgressNotification(CheckInOperation);
	}
	else
	{
		DisplayFailureNotification(CheckInOperation);
	}
}

void SGitLFSSourceControlSettings::OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, const ECommandResult::Type InResult)
{
	RemoveInProgressNotification();

	// Report result with a notification
	if (InResult == ECommandResult::Succeeded)
	{
		DisplaySuccessNotification(InOperation);
	}
	else
	{
		DisplayFailureNotification(InOperation);
	}

	if (InOperation->GetName() == "MarkForAdd" &&
		InResult == ECommandResult::Succeeded &&
		bAutoInitialCommit)
	{
		// 4. optional initial Asynchronous commit with custom message: launch a "CheckIn" Operation
		LaunchCheckInOperation();
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void SGitLFSSourceControlSettings::DisplayInProgressNotification(const FSourceControlOperationRef& InOperation)
{
	FNotificationInfo Info(InOperation->GetInProgressString());
	Info.bFireAndForget = false;
	Info.ExpireDuration = 0.0f;
	Info.FadeOutDuration = 1.0f;

	OperationInProgressNotification = FSlateNotificationManager::Get().AddNotification(Info);

	if (OperationInProgressNotification.IsValid())
	{
		OperationInProgressNotification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void SGitLFSSourceControlSettings::RemoveInProgressNotification()
{
	if (OperationInProgressNotification.IsValid())
	{
		OperationInProgressNotification.Pin()->ExpireAndFadeout();
		OperationInProgressNotification.Reset();
	}
}

void SGitLFSSourceControlSettings::DisplaySuccessNotification(const FSourceControlOperationRef& InOperation)
{
	const FText NotificationText = FText::Format(LOCTEXT("InitialCommit_Success", "{0} operation was successfull!"), FText::FromName(InOperation->GetName()));

	FNotificationInfo Info(NotificationText);
	Info.bUseSuccessFailIcons = true;
	Info.Image = FEditorAppStyle::GetBrush(TEXT("NotificationList.SuccessImage"));

	FSlateNotificationManager::Get().AddNotification(Info);
}

void SGitLFSSourceControlSettings::DisplayFailureNotification(const FSourceControlOperationRef& InOperation)
{
	const FText NotificationText = FText::Format(LOCTEXT("InitialCommit_Failure", "Error: {0} operation failed!"), FText::FromName(InOperation->GetName()));

	FNotificationInfo Info(NotificationText);
	Info.ExpireDuration = 8.0f;

	FSlateNotificationManager::Get().AddNotification(Info);
}

#undef LOCTEXT_NAMESPACE
