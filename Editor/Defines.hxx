//	ファイル名	：defines.hxx
//	  概  要		：マクロ定義
//	作	成	者	：daigo
//	 作成日時	：2025/02/21 00:24:03
//_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/

#pragma once

// ビルドモード
#ifdef _DEBUG
#define DEBUG
#else
//#define SHIPPING
#endif // !_DEBUG

// 追加処理
#define _THREADPOOL_

// その他
#define _CRTDBG_MAP_ALLOC // メモリリーク検出

#define pRelese(pointer) { if (pointer) { delete pointer; pointer = nullptr; } } // ポインタの解放
