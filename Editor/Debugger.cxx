#include "Debugger.hxx"


#ifndef SHIPPING
#include <iostream>	
class CNS_None {
public:	~CNS_None() {
#ifndef DEBUG
	rewind(stdin); (void)getchar(); 
#endif // !DEBUG
}
};

void _DebugStringOutput(const std::string& str)
{
	static bool bInit = true;
	static CNS_None none;
	if (bInit) {
		AllocConsole();
#pragma warning(push)
#pragma warning(disable: 4996)
		(void)freopen("CON", "r", stdin);     // •W€“ü—Í‚ÌŠ„‚è“–‚Ä
		(void)freopen("CON", "w", stdout);    // •W€o—Í‚ÌŠ„‚è“–‚Ä
#pragma warning(pop)
		bInit = false;
	}
	std::cout << str.c_str();
}
#endif // !SHIPPING

#ifdef DEBUG	

#endif // _DEBUG
