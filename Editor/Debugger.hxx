//	ファイル名	：debugger.hxx
//	  概  要		：デバッグの際に使用する処理です。
//	作	成	者	：daigo
//	 作成日時	：2025/02/23 21:46:38
//_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/

// !-!-! = 関数を直接参照せず、マクロを使用してください！ = !-!-!

// =-=-= インクルードガード部 =-=-=

#pragma once

#ifndef SHIPPING
	// DEBUGとRELEASEの場合の共通処理
#include <windows.h>
#include <string>
//#include <type_traits>
//#include <iostream>
// デバッグ用文字列の出力　※セミコロン付き
#define DebugString_(str) _DebugStringOutput(str);
// デバッグ用文字列の出力 cout版 ※セミコロン付き
#define DebugCOUT_(str) std::cout << str;

void _DebugStringOutput(const std::string& str);

#ifdef DEBUG
	// DEBUGの場合のみの処理

	// ポインタやbool,その他型のチェック
#define Check(value) _check(value)
// ここにたどり着いたら停止　※セミコロン付き
#define DebugBreakPoint_ DebugBreak();

template <typename T>
T& _check(const T& value) {
	if constexpr (std::is_same_v<T, bool>) {
		// このブロックは T が bool 型の場合にのみコンパイルされる
		if(!value) {
			_DebugStringOutput("■◆■◆■ !-!-!-! ■◆■◆■ ：falseが返されました。\n");
			DebugBreak();
		}
		return value;
	}
	else if constexpr (std::is_pointer_v<T>) {
		// このブロックは T がポインタ型の場合にのみコンパイルされる
		if(value == nullptr) {
			_DebugStringOutput("■◆■◆■ !-!-!-! ■◆■◆■ ：nullptrが返されました。\n");
			DebugBreak();
		}
		return value;
	}
	else if constexpr (std::is_same_v<T, HRESULT>) {
		// このブロックは T が HRESULT 型の場合にのみコンパイルされる
		if (FAILED(value)) {
			_DebugStringOutput("■◆■◆■ !-!-!-! ■◆■◆■ ：HRESULTが失敗しました。\n");
			DebugBreak();
		}
		return value;
	}
	else {
		// その他の型の場合
		_DebugStringOutput("■◆■◆■ !-!-!-! ■◆■◆■ ：不明な型です。例外処理を実装してください。\n");
		DebugBreak();
		return false;
	}
}

#else // !DEBUG
	// RELEASEでありSHIPPINGでもDEBUGでもない場合の処理

#endif // DEBUG
#else // SHIPPING
	// SHIPPINGの場合のみの処理

#define DebugString_(str);
#define DebugCOUT_(str);

#endif // !SHIPPING
#ifndef DEBUG
// RELEASEとSHIPPINGの場合の共通処理

#define Check(value) value
#define DebugBreakPoint_ 

#endif // !DEBUG
