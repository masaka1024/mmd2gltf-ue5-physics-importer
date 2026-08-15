// Copyright (c) 2026 masaka1024. MIT License.

#include "SMmdImporterWindow.h"

#include "MmdActorBuilder.h"
#include "MmdPhysicsWiring.h"
#include "MmdMaterialConversion.h"
#include "Engine/SkeletalMesh.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMmdImporterWindow"

const FName SMmdImporterWindow::TabId(TEXT("MmdPhysicsImporter"));

void SMmdImporterWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(8.0f)
		[
			SNew(SVerticalBox)

			// --- 言語切替 ---
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bUseEnglish ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bUseEnglish = (S == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(LOCTEXT("English", "English"))
				]
			]

			// --- 対象スケルタルメッシュ ---
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return L(TEXT("対象スケルタルメッシュ"), TEXT("Target Skeletal Mesh")); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(USkeletalMesh::StaticClass())
				.ObjectPath(this, &SMmdImporterWindow::GetMeshPath)
				.OnObjectChanged(this, &SMmdImporterWindow::OnMeshChanged)
				.AllowClear(true)
				.DisplayThumbnail(true)
			]

			// --- .glb パス ---
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return L(TEXT(".glb (mmd2gltf-gui の出力)"), TEXT(".glb (output of mmd2gltf-gui)")); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return FText::FromString(GlbPath); })
					.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { GlbPath = T.ToString(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Browse", "..."))
					.OnClicked(this, &SMmdImporterWindow::OnBrowseGlb)
				]
			]

			// --- 【1】物理を配線 ---
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.IsEnabled(this, &SMmdImporterWindow::CanWire)
				.OnClicked(this, &SMmdImporterWindow::OnWirePhysics)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return L(TEXT("1. 物理を配線 / 再配線"), TEXT("1. Wire / Re-wire Physics"));
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]()
				{
					return L(
						TEXT("Post-Process Anim Blueprint を作ってスケルタルメッシュに割り当てます。")
						TEXT("剛体とジョイントは自作 Bullet 互換エンジンで駆動され、UE の Chaos は使いません。")
						TEXT("体のボーン (ボーン追従) には書き戻さないので、アニメーションはそのまま残ります。"),
						TEXT("Creates a Post-Process Anim Blueprint and assigns it to the skeletal mesh. ")
						TEXT("Rigid bodies and joints are driven by the bundled Bullet-compatible engine; UE's Chaos is not used. ")
						TEXT("Bone-follow bones are never written back, so your animation is preserved."));
				})
			]

			// --- 【2】マテリアルを変換 ---
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.IsEnabled(this, &SMmdImporterWindow::CanWire)
				.OnClicked(this, &SMmdImporterWindow::OnConvertMaterials)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return L(TEXT("2. マテリアルを MMD トゥーンへ変換"), TEXT("2. Convert Materials to MMD Toon"));
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]()
				{
					return L(
						TEXT("マスターマテリアル M_MmdToon を生成し、材質ごとにインスタンスを作って割り当てます。")
						TEXT("Unlit + 自前の N·L トゥーンで、スフィアマップ (乗算/加算) にも対応します。")
						TEXT("alphaMode=BLEND の材質は M_MmdToonTranslucent (半透明) の方から作ります。")
						TEXT("共有トゥーン (toon01〜toon10) はモデルに含まれないので、")
						TEXT("別途プロジェクトへ取り込んでください (名前でプロジェクト全体から探します)。")
						TEXT("輪郭線を描く材質があれば M_MmdOutline も作ります。"),
						TEXT("Creates the M_MmdToon master material and one instance per MMD material. ")
						TEXT("Unlit with a hand-built N·L toon ramp; sphere maps (multiply/add) are supported. ")
						TEXT("Materials with alphaMode=BLEND are instanced from M_MmdToonTranslucent instead. ")
						TEXT("Shared toons (toon01..toon10) are not bundled with the model - import them into the ")
						TEXT("project yourself and they will be found by name. ")
						TEXT("M_MmdOutline is also created when any material draws an outline."));
				})
			]

			// --- 【3】アクターを生成 ---
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.IsEnabled_Lambda([this]() { return TargetMesh.IsValid(); })
				.OnClicked(this, &SMmdImporterWindow::OnBuildActor)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return L(TEXT("3. アクターを生成 (本体 + アニメーション + 輪郭線)"),
						TEXT("3. Build Actor (body + animation + outline)"));
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]()
				{
					return L(
						TEXT("BP_<メッシュ名> という Blueprint アクターを作ります。")
						TEXT("スケルタルメッシュ・アニメーション・輪郭線コンポーネント (MmdOutlineComponent) を")
						TEXT("組んだ状態で出てくるので、レベルへ置けば物理も輪郭線もモーションも効いた状態になります。")
						TEXT("アニメーションは同じスケルトンで再生できる AnimSequence を自動で探して割り当てます")
						TEXT("(VMD 入りの .glb なら Interchange が <メッシュ名>_Anim として取り込んでいます)。")
						TEXT("差し替えたいときは Mesh コンポーネントの Anim to Play を変更してください。")
						TEXT("輪郭線を描く材質が無いモデルには輪郭線コンポーネントを付けません。")
						TEXT("【2】のあとに実行してください (輪郭線の有無をマテリアルから見るため)。"),
						TEXT("Creates a Blueprint actor named BP_<MeshName> with the skeletal mesh, its animation and ")
						TEXT("the outline component (MmdOutlineComponent) already wired up. Drop it into a level and ")
						TEXT("physics, outlines and motion are all active. The animation is picked automatically from ")
						TEXT("the AnimSequences that play on the same skeleton (for a .glb with a VMD baked in, ")
						TEXT("Interchange imports it as <MeshName>_Anim). To use a different one, change Anim to Play ")
						TEXT("on the Mesh component. The outline component is skipped for models that draw no ")
						TEXT("outlines. Run this after step 2 (it reads the outline flag from the materials)."));
				})
			]

			// --- 状態表示 ---
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Visibility_Lambda([this]() { return StatusText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity_Lambda([this]()
					{
						return bStatusIsError ? FSlateColor(FLinearColor(1.0f, 0.4f, 0.4f)) : FSlateColor::UseForeground();
					})
					.Text_Lambda([this]() { return FText::FromString(StatusText); })
				]
			]
		]
	];
}

FString SMmdImporterWindow::GetMeshPath() const
{
	return TargetMesh.IsValid() ? TargetMesh->GetPathName() : FString();
}

void SMmdImporterWindow::OnMeshChanged(const FAssetData& AssetData)
{
	TargetMesh = Cast<USkeletalMesh>(AssetData.GetAsset());
}

FReply SMmdImporterWindow::OnBrowseGlb()
{
	IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
	if (Platform == nullptr) return FReply::Handled();

	TArray<FString> Files;
	const bool bPicked = Platform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(SharedThis(this)),
		L(TEXT(".glb を選択"), TEXT("Select .glb")).ToString(),
		FPaths::ProjectDir(), TEXT(""), TEXT("glTF Binary (*.glb)|*.glb"),
		EFileDialogFlags::None, Files);

	if (bPicked && Files.Num() > 0)
	{
		GlbPath = FPaths::ConvertRelativePathToFull(Files[0]);
	}
	return FReply::Handled();
}

bool SMmdImporterWindow::CanWire() const
{
	return TargetMesh.IsValid() && !GlbPath.IsEmpty();
}

FReply SMmdImporterWindow::OnWirePhysics()
{
	const FMmdWireResult Result = FMmdPhysicsWiring::WirePhysics(TargetMesh.Get(), GlbPath);
	StatusText = Result.Message;
	bStatusIsError = !Result.bSuccess;
	return FReply::Handled();
}

FReply SMmdImporterWindow::OnConvertMaterials()
{
	const FMmdMaterialResult Result = FMmdMaterialConversion::ConvertMaterials(TargetMesh.Get(), GlbPath);
	StatusText = Result.Message;
	bStatusIsError = !Result.bSuccess;
	return FReply::Handled();
}

FReply SMmdImporterWindow::OnBuildActor()
{
	// .glb も渡す。表情モーフのアニメーションは UE の取り込みで落ちるので、
	// ここで .glb から直接読んで補うため (MmdMorphAnimation.h)。
	const FMmdActorResult Result = FMmdActorBuilder::BuildActor(TargetMesh.Get(), GlbPath);
	StatusText = Result.Message;
	bStatusIsError = !Result.bSuccess;
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
