/*
 * Xournal++
 *
 * Image Tool handler
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "model/PageRef.h"
#include "util/Rectangle.h"

#include "filesystem.h"

class Control;
class Image;

class ImageHandler final {
public:
    ImageHandler(Control* control);
    ~ImageHandler();

public:
    /**
     * inserts an image scaled to the given size
     */
    void insertImageWithSize(PageRef page, const xoj::util::Rectangle<double>& space);

    /**
     * Computes a top-to-bottom layout for one or more images. A non-empty selected space is used as the bounding box
     * of the complete group. For a click without a dragged space, the remaining page area is used and images are never
     * enlarged beyond their native size.
     */
    [[nodiscard]] static auto calculateImageLayout(const std::vector<std::pair<int, int>>& imageSizes,
                                                   const xoj::util::Rectangle<double>& space, double pageWidth,
                                                   double pageHeight) -> std::vector<xoj::util::Rectangle<double>>;

    /// Creates the image from the given file
    [[nodiscard]] static auto createImageFromFile(const fs::path& path) -> std::unique_ptr<Image>;
    /// Creates an image without showing a dialog; errorMessage is set on failure.
    [[nodiscard]] static auto createImageFromFile(const fs::path& path, std::string& errorMessage)
            -> std::unique_ptr<Image>;

    static bool addImageToDocument(std::unique_ptr<Image> img, PageRef page, Control* ctrl, bool addUndoAction);

    /// applies (potentially adjusted) width/height to the image: scale down (only if necessary) the image so that it
    /// then fits on the page
    static void automaticScaling(Image& img, PageRef page, int width, int height);
    /// Same as above, but width and height are inferred from the image file.
    static void automaticScaling(Image& img, PageRef page);

    /// lets the user choose an image file, creates the image and calls the callback
    void chooseAndCreateImage(std::function<void(std::unique_ptr<Image>)> callback);

    /// lets the user choose image files, creates every valid image and calls the callback once
    void chooseAndCreateImages(std::function<void(std::vector<std::unique_ptr<Image>>)> callback);

private:
    Control* control;
};
