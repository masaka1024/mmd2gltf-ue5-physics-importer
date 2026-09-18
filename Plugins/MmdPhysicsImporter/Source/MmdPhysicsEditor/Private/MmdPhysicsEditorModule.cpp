// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdPhysicsEditorModule.h"

#include "MmdPhysicsCoreLog.h"
#include "SMmdImporterWindow.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"


#define LOCTEXT_NAMESPACE "FMmdPhysicsEditorModule"


// ===================================================================
// 【全角数字】取り込み時に半角へ直す (プラグインの [0] ボタン経由)
//
//   PMX 標準の指ボーン (`右親指０` `右人指２` `左小指３` …) は名前に全角数字を含む。
//   下の LC_CTYPE の切り替えで仮名・漢字は SanitizeName を通せるようになったが、
//   **全角数字だけはどうやっても通せない**。
//
//     全角2  iswalpha=0  iswdigit=0  iswalnum=1   ← どちらの検査にも入らない
//     半角2  iswalpha=0  iswdigit=1
//
//   iswdigit が ASCII の 0-9 しか返さないのは POSIX の規定で、ロケールでは変わらない。
//   判定式 (`IsAlpha || IsDigit`) はエンジン側にあり、こちらからは変えられない。
//   放っておくと `右人指１` `右人指２` `右人指３` が揃って `右人指_` に潰れて衝突し、
//   衝突した分のトラックが取り込みで捨てられる (IA では 54 本中 18 本が脱落した)。
//
// ★対策: **取り込む直前に、全角数字を半角へ直した .glb の複製を作って食わせる**。
//   MmdGlbNameNormalize / インポーターウィンドウの [0] ボタン。
//   直すのはボーン名とモーフ名だけで、原本の .glb は 1 バイトも書き換えない。
//   エクスポーター (mmd2gltf-gui) も直さない ― あちらは原典 DATA を変えずに
//   保持するのが目的なので、UE の都合はこちら側で吸収する。
//
// ★取り込み後の照合は全角・半角のどちらでも通る。
//   参照する .glb は原本 (全角) のままでよい設計にしてあるため、
//   物理は AnimNode_MmdPhysics::FindBoneIndexForPmxName、
//   モーフは FMmdMorphAnimation::ApplyMorphCurves が半角化して引き直す。
//
// ★「Interchange のパイプラインでボーン名を直す」案は**成立しない**。実際に試して破棄した。
//   スケルトンのボーン名とアニメーションのトラック名はどちらもジョイントノードの
//   GetDisplayLabel() から取られるので揃うが、**スキンウェイトだけが取り残される**。
//   頂点ウェイトのボーン番号はメッシュのペイロードが持つ JointNames で張り替えられ、
//   そちらは翻訳器が .glb から直接読んだ名前 (= 全角のまま) だから。
//
//     // InterchangeSkeletalMeshFactory.cpp:600 付近
//     SourceRemapBoneIndex[LocalJointIndex] = LocalJointIndex;   // 既定は素通し
//     if (Bone.Name.Equals(LocalJointName))                      // 半角 vs 全角 → 一致しない
//     {
//         SourceRemapBoneIndex[LocalJointIndex] = RefBoneIndex;  // ここに到達しない
//     }
//
//   一致しないと張り替えが起きず、頂点が別のボーンに付いて**メッシュが崩れる**
//   (指が伸び縮みする)。指が静止するより悪い。
//   **ファイルそのものを差し替える**現行の方式なら、翻訳器もファクトリも同じ半角名を
//   見るので、この不整合は起こらない。
// ===================================================================

// ===================================================================
// 【日本語名と LC_CTYPE】Mac のエディタでは起動時に LC_CTYPE を UTF-8 にする
//
//   AnimSequence の中身は Sequencer のデータモデルで、ボーンは FK ControlRig の
//   コントロール `<ボーン名>_CONTROL` として持たれる。その名前は
//   URigHierarchy::SanitizeName を通り、合否判定が FChar::IsAlpha (= ::iswalpha)。
//   iswalpha は C ロケールだと非 ASCII に false を返すので、日本語のボーン名は
//   '_' へ潰れて衝突し、衝突したトラックは捨てられる
//   (実測: 動くボーン 54 本中 48 本が脱落)。モーフ名も同じ理由で弾かれる。
//
// ★取り込みの間だけ切り替える方式 (FMmdScopedUtf8CType) では**足りない**。
//   SanitizeName は取り込み時だけでなく、AnimSequence を**ロードするたび**にも走る
//   (UAnimSequence::OnAnimModelLoaded → RemoveBoneTracksMissingFromSkeleton。
//    FK リグはロード時に作り直される)。そこが C ロケールだと、
//     - ロードのたびに `_____CONTROL が見つかりません` が出る
//     - メモリ上の FK リグで日本語のカーブ名が衝突し、その後のカーブ編集で
//       AnimSequencerController.cpp の check に当たって**エディタごと落ちる**
//   (実測: MmdPhysics.Editor.BuildActor がクラッシュ)。ロードはゲームスレッドのどこからでも
//   起きるので、スコープでは覆えない。だから Mac のエディタでは起動時に切り替える。
//
// ★Mac のエディタに限る (#if PLATFORM_MAC && WITH_EDITOR)。
//   Windows (MSVC) の iswalpha は C ロケールでも非 ASCII を分類すると見込んでいるが**未検証**。
//   FMmdScopedUtf8CType は Windows 向けの保険として取り込みの入口に残してある。
//
// ★既知の副作用: エンジンの自動テスト System::Core::Misc::Char が
//   `Locale is "C.UTF-8" but should be "C"` で落ちる (ロケールを "C" 前提にしているテスト)。
//
// ★LC_CTYPE だけを触ること。LC_ALL や LC_NUMERIC を変えてはいけない。
//   UE は小数点の書式で C ロケールを前提にしており、動かすと数値のパースが壊れる。
//
// ★全角数字 (０-９) はどのロケールでも iswalpha=0 / iswdigit=0 で通らない
//   (C.UTF-8 / UTF-8 / ja_JP.UTF-8 で実測)。そちらは上の【全角数字】の半角化で扱う。
// ===================================================================

#if PLATFORM_MAC && WITH_EDITOR
#include <locale.h>
#include <wctype.h>

namespace
{
	void RaiseCTypeLocaleForJapaneseNames()
	{
		// C.UTF-8 → UTF-8 の順。どちらも文字種だけを UTF-8 にする中立なロケール
		// (ja_JP のような言語固有のものは要らない。判定表は同じ)。
		const char* const Candidates[] = { "C.UTF-8", "UTF-8" };
		for (const char* Candidate : Candidates)
		{
			const char* const Applied = ::setlocale(LC_CTYPE, Candidate);
			if (Applied == nullptr) continue;

			// ロケール名が付いただけで判定が変わらない環境もあるので、結果で見る。
			if (::iswalpha(static_cast<wint_t>(0x3042)) != 0)   // 'あ'
			{
				UE_LOG(LogMmdPhysics, Log,
					TEXT("[MmdPhysics] LC_CTYPE を '%hs' にしました (Mac エディタのみ)。")
					TEXT("日本語のボーン名・モーフ名が Control Rig の名前検査を通ります。"),
					Applied);
				return;
			}
		}

		::setlocale(LC_CTYPE, "C");
		UE_LOG(LogMmdPhysics, Warning,
			TEXT("[MmdPhysics] UTF-8 の LC_CTYPE (C.UTF-8 / UTF-8) を適用できませんでした。")
			TEXT("日本語のボーン名・モーフ名は '_' に潰れて衝突し、")
			TEXT("アニメーションのトラックが取り込みで捨てられます。"));
	}
}
#endif // PLATFORM_MAC && WITH_EDITOR

void FMmdPhysicsEditorModule::StartupModule()
{
#if PLATFORM_MAC && WITH_EDITOR
	// ★アセットのロード (マップのロード等) より前に済ませる必要がある。
	RaiseCTypeLocaleForJapaneseNames();
#endif

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
