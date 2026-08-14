// Copyright (c) 2026 masaka1024. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * プラグイン全体で共有するログカテゴリ。
 *
 * 移植元は例外を投げず警告を貯めて処理を継続する設計（GlbPhysicsReader の warnings リスト）。
 * その方針を踏襲し、致命的でない不整合はここへ Warning として出す。
 */
MMDPHYSICSCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogMmdPhysics, Log, All);
