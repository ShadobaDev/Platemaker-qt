// The tests' entry point: a QGuiApplication, so a test can lay out lettering (fonts need one), then
// GoogleTest. No QWidget: the target's rule (CMakeLists.txt) is about widgets, and this is Qt6::Gui.

#include <gtest/gtest.h>

#include <QGuiApplication>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
