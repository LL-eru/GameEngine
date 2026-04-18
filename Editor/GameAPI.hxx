//	ファイル名	：GameAPI.hxx
//	  概  要	：
//	作	成	者	：daigo
//	 作成日時	：2026/03/30 17:55:33
//_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/

// =-=-= インクルードガード部 =-=-=
#pragma once

struct GameState {
    int counter;
};

// DLLが提供する関数テーブル
struct GameAPI {
    void (*Init)(GameState*);
    void (*Update)(GameState*);
    void (*Uninit)(GameState*);
};