#include "Glfw.hxx"
#include "Logger.hxx"

int main() {
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    GLFW mainwindow(1280, 720, "main window");
	GLFW subwindow(640, 360, "sub window");

	Logger::Init();

	Logger::AddSink(std::make_shared<ConsoleSink>());
	Logger::AddSink(std::make_shared<FileSink>("log.txt"));

    // 4. メインループ
    while (!mainwindow.ShouldClose() && !subwindow.ShouldClose()) {
        // イベント（キー入力など）を処理
        
        glfwPollEvents();
    }

	Logger::Uninit();

    return 0;
}
