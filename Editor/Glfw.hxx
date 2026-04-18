//	ファイル名	：Glfw.hxx
//	  概  要	：
//	作	成	者	：daigo
//	 作成日時	：2026/03/30 18:51:14
//_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/

// =-=-= インクルードガード部 =-=-=
#ifndef _____Glfw_HXX_____
#define _____Glfw_HXX_____

// =-=-= インクルード部 =-=-=
#include <GLFW/glfw3.h>

class GLFW
{
public:
	GLFW(int width, int height, const std::string& title);
	~GLFW();

    void PollEvents();
    bool ShouldClose() const;

    GLFWwindow* GetNativeHandle() const; // Vulkanで使う

    int GetWidth() const;
    int GetHeight() const;

private:
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

private:
	GLFWwindow* m_window;
    int m_width;
    int m_height;
};

#endif // !_____Glfw_HXX_____