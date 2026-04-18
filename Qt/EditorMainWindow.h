#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_EditorMainWindow.h"

class EditorMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    EditorMainWindow(QWidget *parent = nullptr);
    ~EditorMainWindow();

private:
    Ui::EditorMainWindowClass ui;
};

