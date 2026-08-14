// Copyright (c) 2026 masaka1024. MIT License.
// 移植元: Assets/MMD_Scripts/MmdPhysics/Pmx/MiniJson.cs

#include "MmdMiniJson.h"

namespace MmdPhysics
{
	TSharedPtr<MmdJsonValue> MiniJson::Parse(const FString& s)
	{
		int32 i = 0;
		TSharedPtr<MmdJsonValue> v = ParseValue(s, i);
		SkipWs(s, i);
		return v;
	}

	// --- アクセス補助 ---
	const FMmdJsonObject* MiniJson::Obj(const TSharedPtr<MmdJsonValue>& o)
	{
		return (o.IsValid() && o->Type == EMmdJsonType::Object) ? &o->AsObject : nullptr;
	}

	const FMmdJsonArray* MiniJson::Arr(const TSharedPtr<MmdJsonValue>& o)
	{
		return (o.IsValid() && o->Type == EMmdJsonType::Array) ? &o->AsArray : nullptr;
	}

	FString MiniJson::Str(const TSharedPtr<MmdJsonValue>& o)
	{
		return (o.IsValid() && o->Type == EMmdJsonType::String) ? o->AsString : FString();
	}

	double MiniJson::Num(const TSharedPtr<MmdJsonValue>& o)
	{
		if (!o.IsValid()) return 0.0;
		if (o->Type == EMmdJsonType::Number) return o->AsNumber;
		if (o->Type == EMmdJsonType::Bool) return o->AsBool ? 1.0 : 0.0;
		return 0.0;
	}

	int32 MiniJson::Int(const TSharedPtr<MmdJsonValue>& o)
	{
		// C# の Math.Round は既定で銀行家丸め (MidpointRounding.ToEven)。
		// JSON の index は整数値なので実害は出ないが、式を一致させておく。
		return static_cast<int32>(FMath::RoundHalfToEven(Num(o)));
	}

	float MiniJson::Flt(const TSharedPtr<MmdJsonValue>& o)
	{
		return static_cast<float>(Num(o));
	}

	TSharedPtr<MmdJsonValue> MiniJson::Get(const FMmdJsonObject* o, const FString& Key)
	{
		if (o == nullptr) return nullptr;
		const TSharedPtr<MmdJsonValue>* Found = o->Find(Key);
		return Found != nullptr ? *Found : nullptr;
	}

	// --- パース本体 ---
	TSharedPtr<MmdJsonValue> MiniJson::ParseValue(const FString& s, int32& i)
	{
		SkipWs(s, i);
		const TCHAR c = s[i];
		switch (c)
		{
		case TEXT('{'): return ParseObject(s, i);
		case TEXT('['): return ParseArray(s, i);
		case TEXT('"'):
		{
			TSharedPtr<MmdJsonValue> v = MakeShared<MmdJsonValue>();
			v->Type = EMmdJsonType::String;
			v->AsString = ParseString(s, i);
			return v;
		}
		case TEXT('t'):
		{
			i += 4;   // true
			TSharedPtr<MmdJsonValue> v = MakeShared<MmdJsonValue>();
			v->Type = EMmdJsonType::Bool; v->AsBool = true;
			return v;
		}
		case TEXT('f'):
		{
			i += 5;   // false
			TSharedPtr<MmdJsonValue> v = MakeShared<MmdJsonValue>();
			v->Type = EMmdJsonType::Bool; v->AsBool = false;
			return v;
		}
		case TEXT('n'):
			i += 4;   // null
			return nullptr;
		default: return ParseNumber(s, i);
		}
	}

	TSharedPtr<MmdJsonValue> MiniJson::ParseObject(const FString& s, int32& i)
	{
		TSharedPtr<MmdJsonValue> o = MakeShared<MmdJsonValue>();
		o->Type = EMmdJsonType::Object;
		i++; // {
		SkipWs(s, i);
		if (s[i] == TEXT('}')) { i++; return o; }
		while (true)
		{
			SkipWs(s, i);
			const FString Key = ParseString(s, i);
			SkipWs(s, i);
			i++; // :
			o->AsObject.Add(Key, ParseValue(s, i));
			SkipWs(s, i);
			const TCHAR c = s[i++];
			if (c == TEXT('}')) break;
			// c == ','
		}
		return o;
	}

	TSharedPtr<MmdJsonValue> MiniJson::ParseArray(const FString& s, int32& i)
	{
		TSharedPtr<MmdJsonValue> a = MakeShared<MmdJsonValue>();
		a->Type = EMmdJsonType::Array;
		i++; // [
		SkipWs(s, i);
		if (s[i] == TEXT(']')) { i++; return a; }
		while (true)
		{
			a->AsArray.Add(ParseValue(s, i));
			SkipWs(s, i);
			const TCHAR c = s[i++];
			if (c == TEXT(']')) break;
			// c == ','
		}
		return a;
	}

	FString MiniJson::ParseString(const FString& s, int32& i)
	{
		FString sb;
		i++; // opening quote
		while (true)
		{
			const TCHAR c = s[i++];
			if (c == TEXT('"')) break;
			if (c == TEXT('\\'))
			{
				const TCHAR e = s[i++];
				switch (e)
				{
				case TEXT('"'): sb.AppendChar(TEXT('"')); break;
				case TEXT('\\'): sb.AppendChar(TEXT('\\')); break;
				case TEXT('/'): sb.AppendChar(TEXT('/')); break;
				case TEXT('b'): sb.AppendChar(TEXT('\b')); break;
				case TEXT('f'): sb.AppendChar(TEXT('\f')); break;
				case TEXT('n'): sb.AppendChar(TEXT('\n')); break;
				case TEXT('r'): sb.AppendChar(TEXT('\r')); break;
				case TEXT('t'): sb.AppendChar(TEXT('\t')); break;
				case TEXT('u'):
				{
					const FString Hex = s.Mid(i, 4);
					const int32 Code = FCString::Strtoi(*Hex, nullptr, 16);
					i += 4;
					sb.AppendChar(static_cast<TCHAR>(Code));
					break;
				}
				default: sb.AppendChar(e); break;
				}
			}
			else sb.AppendChar(c);
		}
		return sb;
	}

	TSharedPtr<MmdJsonValue> MiniJson::ParseNumber(const FString& s, int32& i)
	{
		const int32 start = i;
		while (i < s.Len())
		{
			const TCHAR c = s[i];
			if (c == TEXT('-') || c == TEXT('+') || c == TEXT('.') || c == TEXT('e') || c == TEXT('E') ||
				(c >= TEXT('0') && c <= TEXT('9'))) i++;
			else break;
		}
		const FString Slice = s.Mid(start, i - start);
		TSharedPtr<MmdJsonValue> v = MakeShared<MmdJsonValue>();
		v->Type = EMmdJsonType::Number;
		// FCString::Atod はロケール非依存 (C# の CultureInfo.InvariantCulture と同等)。
		v->AsNumber = FCString::Atod(*Slice);
		return v;
	}

	void MiniJson::SkipWs(const FString& s, int32& i)
	{
		while (i < s.Len())
		{
			const TCHAR c = s[i];
			if (c == TEXT(' ') || c == TEXT('\t') || c == TEXT('\n') || c == TEXT('\r')) i++;
			else break;
		}
	}
}
