#include "EditorAPI.h"
#include <QApplication>
#include "EditorMainWindow.h"

static QApplication* g_app = nullptr;

void StartEditor(int argc, char** argv)
{
    if (!g_app)
    {
        g_app = new QApplication(argc, argv);
    }

    EditorMainWindow* window = new EditorMainWindow();
    window->show();

    g_app->exec();
}