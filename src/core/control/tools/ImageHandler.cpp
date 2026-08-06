#include "ImageHandler.h"

#include <algorithm>  // for min, max
#include <cmath>      // for isfinite
#include <cstdint>    // for uint64_t
#include <fstream>
#include <limits>
#include <memory>  // for unique_ptr, make_unique
#include <sstream>
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move, pair
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>  // for GdkPixbufLoader
#include <glib.h>                   // for g_error_free, GError

#include "control/Control.h"                 // for Control
#include "control/tools/EditSelection.h"     // for EditSelection
#include "gui/MainWindow.h"                  // for MainWindow
#include "gui/PageView.h"                    // for XojPageView
#include "gui/XournalView.h"                 // for XournalView
#include "gui/dialog/XojOpenDlg.h"           // for showOpenImageDialog
#include "model/Document.h"                  // for Document
#include "model/ElementInsertionPosition.h"  // for InsertionOrder
#include "model/Image.h"
#include "model/Layer.h"            // for Layer
#include "model/PageRef.h"          // for PageRef
#include "model/XojPage.h"          // for XojPage
#include "undo/InsertUndoAction.h"  // for InsertUndoAction, InsertsUndoAction
#include "undo/UndoRedoHandler.h"   // for UndoRedoHandler
#include "util/Util.h"              // for npos
#include "util/XojMsgBox.h"         // for XojMsgBox
#include "util/i18n.h"              // for _
#include "util/raii/GObjectSPtr.h"  // for GObjectSPtr

namespace {
constexpr double IMAGE_GAP = 12.0;
constexpr double MIN_DRAGGED_SIZE = 1.0;
constexpr size_t MAX_FAILURES_IN_MESSAGE = 10;
constexpr size_t MAX_FORMAT_SNIFF_BYTES = 64 * 1024;
constexpr size_t MAX_BATCH_IMAGES = 64;
constexpr uint64_t MAX_BATCH_DECODED_PIXELS = uint64_t{1} << 26;

struct ImageLoadResult {
    std::unique_ptr<Image> image;
    std::string error;
};

struct ImageLoadFailure {
    fs::path path;
    std::string error;
};

auto isRecognizedImageData(std::string_view data, std::string& error) -> bool {
    xoj::util::GObjectSPtr<GdkPixbufLoader> loader(gdk_pixbuf_loader_new(), xoj::util::adopt);
    const size_t bytesToInspect = std::min(data.size(), MAX_FORMAT_SNIFF_BYTES);
    bool formatRecognized = false;

    for (size_t offset = 0; offset < bytesToInspect;) {
        const size_t chunkSize = std::min<size_t>(4096, bytesToInspect - offset);
        GError* loaderError = nullptr;
        const bool success = gdk_pixbuf_loader_write(
                loader.get(), reinterpret_cast<const guchar*>(data.data() + offset), chunkSize, &loaderError);
        if (!success) {
            error = loaderError ? loaderError->message : _("Unrecognized or damaged image data");
            if (loaderError) {
                g_error_free(loaderError);
            }
            gdk_pixbuf_loader_close(loader.get(), nullptr);
            return false;
        }
        if (gdk_pixbuf_loader_get_format(loader.get()) != nullptr) {
            formatRecognized = true;
            break;
        }
        offset += chunkSize;
    }

    // GdkPixbufLoader requires close() even when enough header data has already been supplied. Some formats are only
    // identified during close(), while an early close of a recognized large file may legitimately report truncation.
    GError* closeError = nullptr;
    gdk_pixbuf_loader_close(loader.get(), &closeError);
    formatRecognized = formatRecognized || gdk_pixbuf_loader_get_format(loader.get()) != nullptr;
    if (formatRecognized) {
        if (closeError) {
            g_error_free(closeError);
        }
        return true;
    }

    error = closeError ? closeError->message : _("Unrecognized or unsupported image format");
    if (closeError) {
        g_error_free(closeError);
    }
    return false;
}

auto tryCreateImageFromFile(const fs::path& path) -> ImageLoadResult {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {nullptr, _("Could not open the image file")};
    }

    std::string data;
    try {
        stream.exceptions(std::ios::badbit);
        std::ostringstream contents;
        contents << stream.rdbuf();
        data = contents.str();
    } catch (const std::ios_base::failure& e) {
        return {nullptr, std::string(_("Could not read the image file")) + ": " + e.what()};
    }

    if (data.empty()) {
        return {nullptr, _("The image file is empty")};
    }

    std::string formatError;
    if (!isRecognizedImageData(data, formatError)) {
        return {nullptr, std::move(formatError)};
    }

    auto image = std::make_unique<Image>();
    image->setImage(std::move(data));
    if (auto renderError = image->renderBuffer(); renderError.has_value()) {
        return {nullptr, std::move(*renderError)};
    }
    if (image->getImageSize() == Image::NOSIZE) {
        return {nullptr, _("Failed to determine the image size")};
    }
    return {std::move(image), {}};
}

void showImageLoadFailures(GtkWindow* parent, const std::vector<ImageLoadFailure>& failures) {
    if (failures.empty()) {
        return;
    }

    std::ostringstream message;
    message << _("Some images could not be loaded:") << '\n';
    const size_t shownFailures = std::min(failures.size(), MAX_FAILURES_IN_MESSAGE);
    for (size_t i = 0; i < shownFailures; ++i) {
        message << '\n' << failures[i].path.filename().string() << ": " << failures[i].error;
    }
    if (shownFailures < failures.size()) {
        message << "\n\n" << (failures.size() - shownFailures) << ' ' << _("more image(s) could not be loaded");
    }
    XojMsgBox::showErrorToUser(parent, message.str());
}

auto addImagesToDocument(std::vector<std::unique_ptr<Image>> images, const PageRef& page, Control* control,
                         bool addUndoAction) -> bool {
    if (images.empty()) {
        return false;
    }

    XournalView* xournal = control->getWindow()->getXournal();
    const auto pageNr = xournal->getCurrentPage();
    auto* view = xournal->getViewFor(pageNr);

    // The file chooser is asynchronous. Do not create an undo action containing pointers to images which are about to
    // be destroyed if the user changed the active page while the chooser was open.
    if (view == nullptr || view->getPage() != page) {
        g_warning("Active page changed while you selected the image. Aborting.");
        return false;
    }

    Layer* layer = page->getSelectedLayer();
    std::vector<const Element*> elementPointers;
    elementPointers.reserve(images.size());
    InsertionOrder insertionOrder;
    insertionOrder.reserve(images.size());
    for (auto& image: images) {
        elementPointers.emplace_back(image.get());
        insertionOrder.emplace_back(std::move(image));
    }

    auto selectionAndBounds =
            SelectionFactory::createFromFloatingElements(control, page, layer, view, std::move(insertionOrder));
    xournal->setSelection(selectionAndBounds.first.release());

    if (addUndoAction) {
        if (elementPointers.size() == 1) {
            control->getUndoRedoHandler()->addUndoAction(
                    std::make_unique<InsertUndoAction>(page, layer, elementPointers.front()));
        } else {
            control->getUndoRedoHandler()->addUndoAction(
                    std::make_unique<InsertsUndoAction>(page, layer, std::move(elementPointers)));
        }
    }
    return true;
}
}  // namespace

ImageHandler::ImageHandler(Control* control): control(control) {}

ImageHandler::~ImageHandler() = default;


void ImageHandler::chooseAndCreateImage(std::function<void(std::unique_ptr<Image>)> callback) {
    chooseAndCreateImages([cb = std::move(callback)](std::vector<std::unique_ptr<Image>> images) mutable {
        cb(std::move(images.front()));
    });
}

void ImageHandler::chooseAndCreateImages(std::function<void(std::vector<std::unique_ptr<Image>>)> callback) {
    xoj::OpenDlg::showOpenImagesDialog(
            control->getGtkWindow(), control->getSettings(),
            [cb = std::move(callback), ctrl = control](std::vector<fs::path> paths) mutable {
                std::vector<std::unique_ptr<Image>> images;
                images.reserve(std::min(paths.size(), MAX_BATCH_IMAGES));
                std::vector<ImageLoadFailure> failures;
                uint64_t decodedPixels = 0;

                for (size_t i = 0; i < paths.size(); ++i) {
                    const auto& path = paths[i];
                    if (i >= MAX_BATCH_IMAGES) {
                        failures.emplace_back(ImageLoadFailure{path, _("The image batch is limited to 64 files")});
                        continue;
                    }

                    auto result = tryCreateImageFromFile(path);
                    if (result.image) {
                        const auto [width, height] = result.image->getImageSize();
                        const uint64_t pixels =
                                static_cast<uint64_t>(std::max(width, 1)) * static_cast<uint64_t>(std::max(height, 1));
                        if (pixels > MAX_BATCH_DECODED_PIXELS - decodedPixels) {
                            failures.emplace_back(
                                    ImageLoadFailure{path, _("The selected images exceed the safe memory budget")});
                            continue;
                        }
                        decodedPixels += pixels;
                        images.emplace_back(std::move(result.image));
                    } else {
                        failures.emplace_back(ImageLoadFailure{path, std::move(result.error)});
                    }
                }

                showImageLoadFailures(ctrl->getGtkWindow(), failures);
                if (!images.empty()) {
                    cb(std::move(images));
                }
            });
}

auto ImageHandler::createImageFromFile(const fs::path& path) -> std::unique_ptr<Image> {
    auto result = tryCreateImageFromFile(path);
    if (!result.image) {
        XojMsgBox::showErrorToUser(nullptr, result.error);
    }
    return std::move(result.image);
}

auto ImageHandler::createImageFromFile(const fs::path& path, std::string& errorMessage) -> std::unique_ptr<Image> {
    auto result = tryCreateImageFromFile(path);
    errorMessage = std::move(result.error);
    return std::move(result.image);
}

bool ImageHandler::addImageToDocument(std::unique_ptr<Image> img, PageRef page, Control* control, bool addUndoAction) {
    if (!img) {
        return false;
    }
    std::vector<std::unique_ptr<Image>> images;
    images.emplace_back(std::move(img));
    return addImagesToDocument(std::move(images), page, control, addUndoAction);
}

auto ImageHandler::calculateImageLayout(const std::vector<std::pair<int, int>>& imageSizes,
                                        const xoj::util::Rectangle<double>& space, double pageWidth, double pageHeight)
        -> std::vector<xoj::util::Rectangle<double>> {
    std::vector<xoj::util::Rectangle<double>> layout;
    if (imageSizes.empty()) {
        return layout;
    }

    const bool hasDraggedArea = space.width >= MIN_DRAGGED_SIZE && space.height >= MIN_DRAGGED_SIZE;
    const double availableWidth = std::max(0.0, hasDraggedArea ? space.width : std::max(0.0, pageWidth - space.x));
    const double availableHeight = std::max(0.0, hasDraggedArea ? space.height : std::max(0.0, pageHeight - space.y));

    double widestImage = 0.0;
    double totalImageHeight = 0.0;
    for (const auto& [width, height]: imageSizes) {
        widestImage = std::max(widestImage, static_cast<double>(std::max(width, 1)));
        totalImageHeight += static_cast<double>(std::max(height, 1));
    }

    double gap = 0.0;
    if (imageSizes.size() > 1) {
        // Keep at least three quarters of a very small selected area available for image content.
        gap = std::min(IMAGE_GAP, availableHeight / (4.0 * static_cast<double>(imageSizes.size() - 1)));
    }
    const double totalGapHeight = gap * static_cast<double>(imageSizes.size() - 1);
    const double heightForImages = std::max(0.0, availableHeight - totalGapHeight);

    double scale = std::min(availableWidth / widestImage, heightForImages / totalImageHeight);
    if (!hasDraggedArea) {
        scale = std::min(1.0, scale);
    }
    if (!std::isfinite(scale) || scale < 0.0) {
        scale = 0.0;
    }

    const double groupHeight = totalImageHeight * scale + totalGapHeight;
    double y = space.y + (hasDraggedArea ? (availableHeight - groupHeight) * 0.5 : 0.0);
    layout.reserve(imageSizes.size());
    for (const auto& [rawWidth, rawHeight]: imageSizes) {
        const double width = static_cast<double>(std::max(rawWidth, 1)) * scale;
        const double height = static_cast<double>(std::max(rawHeight, 1)) * scale;
        const double x = space.x + (hasDraggedArea ? (availableWidth - width) * 0.5 : 0.0);
        layout.emplace_back(xoj::util::Rectangle<double>{x, y, width, height});
        y += height + gap;
    }
    return layout;
}

void ImageHandler::automaticScaling(Image& img, PageRef page, int width, int height) {
    double zoom = 1;
    double x = img.getX();
    double y = img.getY();

    if (x + width > page->getWidth() || y + height > page->getHeight()) {
        double const maxZoomX = (page->getWidth() - x) / width;
        double const maxZoomY = (page->getHeight() - y) / height;
        zoom = std::min(maxZoomX, maxZoomY);
    }

    img.setWidth(width * zoom);
    img.setHeight(height * zoom);
}

void ImageHandler::automaticScaling(Image& img, PageRef page) {
    auto [width, height] = img.getImageSize();
    automaticScaling(img, page, width, height);
}


void ImageHandler::insertImageWithSize(PageRef page, const xoj::util::Rectangle<double>& space) {
    chooseAndCreateImages([space, page, ctrl = control](std::vector<std::unique_ptr<Image>> images) {
        std::vector<std::pair<int, int>> imageSizes;
        imageSizes.reserve(images.size());
        for (const auto& image: images) {
            imageSizes.emplace_back(image->getImageSize());
        }

        const double layoutHeight =
                page->isInfiniteCanvas() ? std::numeric_limits<double>::infinity() : page->getHeight();
        const auto layout = calculateImageLayout(imageSizes, space, page->getWidth(), layoutHeight);
        for (size_t i = 0; i < images.size(); ++i) {
            images[i]->setX(layout[i].x);
            images[i]->setY(layout[i].y);
            images[i]->setWidth(layout[i].width);
            images[i]->setHeight(layout[i].height);
        }

        if (page->isInfiniteCanvas() && !layout.empty()) {
            auto* xournal = ctrl->getWindow()->getXournal();
            auto* activeView = xournal->getViewFor(xournal->getCurrentPage());
            if (activeView == nullptr || activeView->getPage() != page) {
                g_warning("Active page changed while you selected the images. Aborting.");
                return;
            }

            double requiredWidth = page->getWidth();
            double requiredHeight = page->getHeight();
            for (const auto& rect: layout) {
                requiredWidth = std::max(requiredWidth, rect.x + rect.width + IMAGE_GAP);
                requiredHeight = std::max(requiredHeight, rect.y + rect.height + IMAGE_GAP);
            }

            if (requiredWidth > page->getWidth() || requiredHeight > page->getHeight()) {
                auto* document = ctrl->getDocument();
                document->lock();
                const size_t pageNo = document->indexOf(page);
                if (pageNo != npos) {
                    page->setSize(requiredWidth, requiredHeight);
                }
                document->unlock();
                if (pageNo != npos) {
                    ctrl->firePageSizeChanged(pageNo);
                }
            }
        }

        addImagesToDocument(std::move(images), page, ctrl, true);
    });
}
