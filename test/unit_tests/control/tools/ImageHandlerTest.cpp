#include <limits>
#include <utility>
#include <vector>

#include <config-test.h>
#include <gtest/gtest.h>

#include "control/tools/ImageHandler.h"
#include "model/Image.h"
#include "util/Rectangle.h"

namespace {
using Rectangle = xoj::util::Rectangle<double>;

TEST(ImageHandler, EmptyImageLayout) {
    EXPECT_TRUE(ImageHandler::calculateImageLayout({}, Rectangle{10.0, 20.0, 100.0, 200.0}, 600.0, 800.0).empty());
}

TEST(ImageHandler, SingleImageKeepsLegacyDraggedAreaLayout) {
    const auto layout =
            ImageHandler::calculateImageLayout({{400, 200}}, Rectangle{10.0, 20.0, 100.0, 100.0}, 600.0, 800.0);

    ASSERT_EQ(layout.size(), 1U);
    EXPECT_DOUBLE_EQ(layout[0].x, 10.0);
    EXPECT_DOUBLE_EQ(layout[0].y, 45.0);
    EXPECT_DOUBLE_EQ(layout[0].width, 100.0);
    EXPECT_DOUBLE_EQ(layout[0].height, 50.0);
}

TEST(ImageHandler, MultipleImagesArePlacedTopToBottomInsideDraggedArea) {
    const auto layout = ImageHandler::calculateImageLayout({{200, 100}, {200, 100}},
                                                           Rectangle{10.0, 20.0, 100.0, 100.0}, 600.0, 800.0);

    ASSERT_EQ(layout.size(), 2U);
    EXPECT_DOUBLE_EQ(layout[0].x, 16.0);
    EXPECT_DOUBLE_EQ(layout[0].y, 20.0);
    EXPECT_DOUBLE_EQ(layout[0].width, 88.0);
    EXPECT_DOUBLE_EQ(layout[0].height, 44.0);
    EXPECT_DOUBLE_EQ(layout[1].x, 16.0);
    EXPECT_DOUBLE_EQ(layout[1].y, 76.0);
    EXPECT_DOUBLE_EQ(layout[1].width, 88.0);
    EXPECT_DOUBLE_EQ(layout[1].height, 44.0);
    EXPECT_LE(layout[0].y + layout[0].height, layout[1].y);
    EXPECT_DOUBLE_EQ(layout[1].y + layout[1].height, 120.0);
}

TEST(ImageHandler, ClickLayoutDoesNotEnlargeImages) {
    const auto layout =
            ImageHandler::calculateImageLayout({{100, 50}, {50, 100}}, Rectangle{10.0, 20.0, 0.0, 0.0}, 200.0, 200.0);

    ASSERT_EQ(layout.size(), 2U);
    EXPECT_DOUBLE_EQ(layout[0].x, 10.0);
    EXPECT_DOUBLE_EQ(layout[0].y, 20.0);
    EXPECT_DOUBLE_EQ(layout[0].width, 100.0);
    EXPECT_DOUBLE_EQ(layout[0].height, 50.0);
    EXPECT_DOUBLE_EQ(layout[1].x, 10.0);
    EXPECT_DOUBLE_EQ(layout[1].y, 82.0);
    EXPECT_DOUBLE_EQ(layout[1].width, 50.0);
    EXPECT_DOUBLE_EQ(layout[1].height, 100.0);
    EXPECT_LE(layout[1].y + layout[1].height, 200.0);
}

TEST(ImageHandler, ClickLayoutShrinksTheGroupToRemainOnFinitePage) {
    const auto layout =
            ImageHandler::calculateImageLayout({{100, 100}, {100, 100}}, Rectangle{0.0, 0.0, 0.0, 0.0}, 100.0, 100.0);

    ASSERT_EQ(layout.size(), 2U);
    EXPECT_DOUBLE_EQ(layout[0].width, 44.0);
    EXPECT_DOUBLE_EQ(layout[0].height, 44.0);
    EXPECT_DOUBLE_EQ(layout[1].y, 56.0);
    EXPECT_DOUBLE_EQ(layout[1].y + layout[1].height, 100.0);
}

TEST(ImageHandler, ClickLayoutFlowsVerticallyOnEndlessCanvas) {
    const auto layout = ImageHandler::calculateImageLayout({{100, 100}, {100, 100}}, Rectangle{0.0, 80.0, 0.0, 0.0},
                                                           100.0, std::numeric_limits<double>::infinity());

    ASSERT_EQ(layout.size(), 2U);
    EXPECT_DOUBLE_EQ(layout[0].width, 100.0);
    EXPECT_DOUBLE_EQ(layout[0].height, 100.0);
    EXPECT_DOUBLE_EQ(layout[1].y, 192.0);
    EXPECT_DOUBLE_EQ(layout[1].y + layout[1].height, 292.0);
}

TEST(ImageHandler, LoadsAValidImageWithoutAnError) {
    std::string error;
    auto image = ImageHandler::createImageFromFile(fs::path(GET_TESTFILE(u8"images/r90.jpg")), error);

    ASSERT_NE(image, nullptr) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(image->getImageSize(), std::make_pair(130, 500));
}

TEST(ImageHandler, RejectsANonImageWithoutCrashing) {
    std::string error;
    auto image = ImageHandler::createImageFromFile(fs::path(GET_TESTFILE(u8"palettes/default.gpl")), error);

    EXPECT_EQ(image, nullptr);
    EXPECT_FALSE(error.empty());
}

}  // namespace
