/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "control/ToolHandler.h"
#include "control/settings/ButtonConfig.h"
#include "control/settings/Settings.h"
#include "control/settings/SettingsEnums.h"
#include "gui/inputdevices/InputUtils.h"

namespace {
class NoopToolListener final: public ToolListener {
public:
    void toolColorChanged() override {}
    void changeColorOfSelection() override {}
    void toolSizeChanged() override {}
    void toolFillChanged() override {}
    void toolLineStyleChanged() override {}
    void toolChanged() override {}
};
}  // namespace

TEST(InputUtilsTest, UnconfiguredRightButtonKeepsToolbarTool) {
    Settings settings(fs::temp_directory_path() / "xournalpp-test-units_InputUtils.xml");
    NoopToolListener listener;
    ToolHandler toolHandler(&listener, nullptr, &settings);

    // Surface Slim Pen side-button events can arrive as mouse right-clicks on Wayland.
    ASSERT_EQ(TOOL_NONE, settings.getButtonConfig(BUTTON_MOUSE_RIGHT)->getAction());
    EXPECT_FALSE(InputUtils::applyButton(&toolHandler, &settings, BUTTON_MOUSE_RIGHT));
    EXPECT_EQ(TOOL_PEN, toolHandler.getToolType());
}

TEST(InputUtilsTest, UnconfiguredStylusButtonReturnsFromButtonToolToToolbarTool) {
    Settings settings(fs::temp_directory_path() / "xournalpp-test-units_InputUtils.xml");
    NoopToolListener listener;
    ToolHandler toolHandler(&listener, nullptr, &settings);

    settings.getButtonConfig(BUTTON_ERASER)->initButton(&toolHandler, BUTTON_ERASER);
    ASSERT_TRUE(InputUtils::applyButton(&toolHandler, &settings, BUTTON_ERASER));
    ASSERT_EQ(TOOL_ERASER, toolHandler.getToolType());

    ASSERT_EQ(TOOL_NONE, settings.getButtonConfig(BUTTON_STYLUS_ONE)->getAction());
    EXPECT_TRUE(InputUtils::applyButton(&toolHandler, &settings, BUTTON_STYLUS_ONE));
    EXPECT_EQ(TOOL_PEN, toolHandler.getToolType());
}
