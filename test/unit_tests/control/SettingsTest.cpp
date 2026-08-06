/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <limits>

#include <gtest/gtest.h>

#include "control/settings/Settings.h"

TEST(SettingsTest, testLoadDoesNotThrowForNonExistingFilePath) {
    const fs::path path = fs::temp_directory_path() / "xournalpp-test-units_Settings_nonExisting.xml";
    fs::remove(path);
    Settings settings{path};
    EXPECT_NO_THROW(settings.load());
    fs::remove(path);
}

TEST(SettingsTest, StabilizerParametersRejectNonFiniteAndOutOfRangeValues) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_Settings_stabilizerBounds.xml";
    Settings settings(outPath);
    settings.transactionStart();

    settings.setStabilizerMinCutoff(-1.0);
    EXPECT_DOUBLE_EQ(0.1, settings.getStabilizerMinCutoff());
    settings.setStabilizerMinCutoff(std::numeric_limits<double>::quiet_NaN());
    EXPECT_DOUBLE_EQ(0.1, settings.getStabilizerMinCutoff());

    settings.setStabilizerBeta(1.0);
    EXPECT_DOUBLE_EQ(0.1, settings.getStabilizerBeta());
    settings.setStabilizerBeta(std::numeric_limits<double>::infinity());
    EXPECT_DOUBLE_EQ(0.1, settings.getStabilizerBeta());

    settings.setStabilizerPredictionTime(-1.0);
    EXPECT_DOUBLE_EQ(4.0, settings.getStabilizerPredictionTime());
    settings.setStabilizerPredictionTime(100.0);
    EXPECT_DOUBLE_EQ(20.0, settings.getStabilizerPredictionTime());
    settings.setStabilizerPredictionTime(std::numeric_limits<double>::quiet_NaN());
    EXPECT_DOUBLE_EQ(20.0, settings.getStabilizerPredictionTime());

    settings.setStabilizerAveragingMethod(static_cast<StrokeStabilizer::AveragingMethod>(999));
    EXPECT_EQ(StrokeStabilizer::AveragingMethod::NONE, settings.getStabilizerAveragingMethod());

    settings.transactionEnd();
    fs::remove(outPath);
}

// Rudimentary test for Settings save/load - very crude
TEST(SettingsTest, testReadWrite) {
    auto saveReloadTest = [&](const fs::path& dir) {
        std::cout << "Test saving in " << dir << std::endl;
        const fs::path outPath = dir / "xournalpp-test-units_Settings_testReadWrite.xml";
        if (fs::exists(outPath)) {
            std::cout << "Removing file (already exist): " << dir << std::endl;
            fs::remove(outPath);
        };

        Settings settings(outPath);
        settings.transactionStart();
        settings.setAudioDisabled(true);               // bool
        settings.setDefaultSaveName(u8"foo/bar€_%H");  // u8string
        settings.setPreferredLocale("es");             // string
        PageTemplateSettings tp;
        tp.parse("xoj/"
                 "template\ncopyLastPageSettings=false\ncopyLastPageSize=true\nsize=5.123x8.764\nbackgroundType="
                 "cµßtom\nbackgroundTypeConfig=m1=3,®ændomString=↓↓↓\nbackgroundColor=#abcdef\n");
        settings.setPageTemplateSettings(tp);                                                  // string
        settings.setDisplayDpi(123);                                                           // int
        settings.setStabilizerDrag(3.1415);                                                    // double
        settings.setStabilizerMinCutoff(0.75);                                                 // double
        settings.setStabilizerBeta(0.012);                                                     // double
        settings.setStabilizerPredictionTime(18.0);                                            // double
        settings.setStabilizerAveragingMethod(StrokeStabilizer::AveragingMethod::GOOGLE_INK);  // enum
        settings.setBackgroundColor(Color(123, 45, 67));                                       // Color
        settings.setColorPaletteSetting("foo/bar€_palette");                                   // path
        settings.setEraserVisibility(ERASER_VISIBILITY_HOVER);                                 // enum
        settings.setFont(XojFont{"myfontname italic 34"});                                     // Font
        settings.latexSettings.editorFont = XojFont{"myfonttest 52"};                          // Font
        settings.setPreloadPagesAfter(145);                                                    // unsigned int
        settings.transactionEnd();                                                             // calls save()

        Settings loaded(outPath);
        loaded.load();

        // For each type, we test one that has been changed and one that should be default
        EXPECT_EQ(settings.isAudioDisabled(), loaded.isAudioDisabled());                                  // bool
        EXPECT_EQ(settings.isAutoloadPdfXoj(), loaded.isAutoloadPdfXoj());                                // bool
        EXPECT_EQ(settings.getDefaultSaveName(), loaded.getDefaultSaveName());                            // u8string
        EXPECT_EQ(settings.getDefaultPdfExportName(), loaded.getDefaultPdfExportName());                  // u8string
        EXPECT_EQ(settings.getPreferredLocale(), loaded.getPreferredLocale());                            // string
        EXPECT_EQ(settings.getPageTemplateSettings(), loaded.getPageTemplateSettings());                  // string
        EXPECT_EQ(settings.getDisplayDpi(), loaded.getDisplayDpi());                                      // int
        EXPECT_EQ(settings.getAddHorizontalSpaceAmountLeft(), loaded.getAddHorizontalSpaceAmountLeft());  // int
        EXPECT_EQ(settings.getStabilizerDrag(), loaded.getStabilizerDrag());                              // double
        EXPECT_EQ(settings.getStabilizerMinCutoff(), loaded.getStabilizerMinCutoff());                    // double
        EXPECT_EQ(settings.getStabilizerBeta(), loaded.getStabilizerBeta());                              // double
        EXPECT_EQ(settings.getStabilizerPredictionTime(), loaded.getStabilizerPredictionTime());          // double
        EXPECT_EQ(StrokeStabilizer::AveragingMethod::GOOGLE_INK,
                  loaded.getStabilizerAveragingMethod());                                                   // enum
        EXPECT_EQ(settings.getCursorHighlightBorderWidth(), loaded.getCursorHighlightBorderWidth());        // double
        EXPECT_EQ(settings.getBackgroundColor(), loaded.getBackgroundColor());                              // Color
        EXPECT_EQ(settings.getActiveSelectionColor(), loaded.getActiveSelectionColor());                    // Color
        EXPECT_EQ(settings.getColorPaletteSetting(), loaded.getColorPaletteSetting());                      // path
        EXPECT_EQ(settings.getLastOpenPath(), loaded.getLastOpenPath());                                    // path
        EXPECT_EQ(settings.getEraserVisibility(), loaded.getEraserVisibility());                            // enum
        EXPECT_EQ(settings.getActiveViewMode(), loaded.getActiveViewMode());                                // enum
        EXPECT_EQ(settings.getFont().getName(), loaded.getFont().getName());                                // Font
        EXPECT_EQ(settings.getFont().getSize(), loaded.getFont().getSize());                                // Font
        EXPECT_EQ(settings.latexSettings.editorFont.getName(), loaded.latexSettings.editorFont.getName());  // Font
        EXPECT_EQ(settings.latexSettings.editorFont.getSize(), loaded.latexSettings.editorFont.getSize());  // Font
        EXPECT_EQ(settings.getPreloadPagesAfter(), loaded.getPreloadPagesAfter());    // unsigned int
        EXPECT_EQ(settings.getPreloadPagesBefore(), loaded.getPreloadPagesBefore());  // unsigned int

        fs::remove(outPath);
    };
    saveReloadTest(fs::temp_directory_path());
}
