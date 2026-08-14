// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdPhysicsEditorModule.h"

#include "SMmdImporterWindow.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FMmdPhysicsEditorModule"

void FMmdPhysicsEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		SMmdImporterWindow::TabId,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				[
					SNew(SMmdImporterWindow)
				];
		}))
		.SetDisplayName(LOCTEXT("TabTitle", "MMD Physics インポーター"))
		.SetTooltipText(LOCTEXT("TabTooltip", "mmd2gltf-gui が出力した .glb の物理をスケルタルメッシュへ配線します。"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	// Tools メニューへ項目を出す (移植元 Unity 版の「MMD Physics/インポーター」に対応)。
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		FToolMenuOwnerScoped OwnerScoped(TEXT("MmdPhysicsEditor"));
		if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools")))
		{
			FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("MmdPhysics"),
				LOCTEXT("MenuSection", "MMD Physics"));
			Section.AddMenuEntry(
				TEXT("OpenMmdPhysicsImporter"),
				LOCTEXT("MenuEntry", "MMD Physics インポーター"),
				LOCTEXT("MenuEntryTooltip", "物理を配線するウィンドウを開きます。"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					FGlobalTabmanager::Get()->TryInvokeTab(SMmdImporterWindow::TabId);
				})));
		}
	}));
}

void FMmdPhysicsEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SMmdImporterWindow::TabId);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMmdPhysicsEditorModule, MmdPhysicsEditor);
